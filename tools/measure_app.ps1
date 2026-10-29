# Footprint of the running fenixOS PC app: private working set and CPU, summed over fenixos.exe
# and only its own WebView2 processes (their command line names our exe; other apps run
# msedgewebview2.exe too). Same convention as Task Manager's "Memory" column. PLAN §4.4.
# Usage: pwsh tools/measure_app.ps1 [-Seconds 10] [-Exe fenixos.exe]
param([int]$Seconds = 10, [string]$Exe = "fenixos.exe")

function Get-Ours {
  $main = Get-CimInstance Win32_Process -Filter "Name = '$Exe'"
  if (-not $main) { return @() }
  $web = Get-CimInstance Win32_Process -Filter "Name = 'msedgewebview2.exe'" |
    Where-Object { $_.CommandLine -match "--webview-exe-name=$([regex]::Escape($Exe))" }
  @($main) + @($web)
}

$procs = Get-Ours
if (-not $procs) { Write-Host "$Exe is not running"; exit 1 }
# Private working set per PID from the perf counter (Get-Process has no private WS property).
$ids = $procs.ProcessId
$ws = 0
$counters = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process | Where-Object { $ids -contains $_.IDProcess }
foreach ($c in $counters) { $ws += [double]$c.WorkingSetPrivate }
$cpu0 = ($ids | ForEach-Object { (Get-Process -Id $_ -ErrorAction SilentlyContinue).TotalProcessorTime.TotalMilliseconds } | Measure-Object -Sum).Sum
Start-Sleep -Seconds $Seconds
$cpu1 = ($ids | ForEach-Object { (Get-Process -Id $_ -ErrorAction SilentlyContinue).TotalProcessorTime.TotalMilliseconds } | Measure-Object -Sum).Sum
$cores = [Environment]::ProcessorCount
$cpuPct = [math]::Round(($cpu1 - $cpu0) / ($Seconds * 1000 * $cores) * 100, 2)
"{0} processes, private working set {1} MB, CPU {2} % of all {3} logical processors over {4} s" -f `
  $procs.Count, [math]::Round($ws / 1MB, 1), $cpuPct, $cores, $Seconds
