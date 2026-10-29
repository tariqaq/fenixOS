# Run a command with at most 4 cores' worth of CPU, at normal priority (user request, P11).
# This pwsh process joins a job object with a hard CPU rate cap, and every child (cargo, rustc,
# node, ninja, gcc...) inherits the job. Threads still run on any core; only the total is capped.
# CARGO_BUILD_JOBS=4 keeps cargo from starting more compilers than the cap can feed.
# Usage: pwsh tools/capped.ps1 <command> [args...]
#   pwsh tools/capped.ps1 pnpm -C app tauri build --no-bundle
#   pwsh tools/capped.ps1 pwsh -NoProfile -File board/idf.ps1 build
# No param block: arguments such as pnpm's -C must reach the command untouched.
if (-not $args) { Write-Error "usage: pwsh tools/capped.ps1 <command> [args...]"; exit 2 }
$Command = $args

$Cores = 4
Add-Type -Namespace Fenix -Name Job -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)]
public struct RateInfo { public uint ControlFlags; public uint CpuRate; }
[DllImport("kernel32.dll", SetLastError = true)]
public static extern IntPtr CreateJobObjectW(IntPtr attrs, IntPtr name);
[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref RateInfo info, uint size);
[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
[DllImport("kernel32.dll")]
public static extern IntPtr GetCurrentProcess();
'@

$all = [Environment]::ProcessorCount
if ($all -gt $Cores) {
  $job = [Fenix.Job]::CreateJobObjectW([IntPtr]::Zero, [IntPtr]::Zero)
  # JobObjectCpuRateControlInformation = 15; ENABLE (1) | HARD_CAP (4); rate in 1/100 % of all CPUs.
  $info = New-Object Fenix.Job+RateInfo
  $info.ControlFlags = 5
  $info.CpuRate = [uint32]($Cores * 10000 / $all)
  $ok = [Fenix.Job]::SetInformationJobObject($job, 15, [ref]$info, 8) -and
        [Fenix.Job]::AssignProcessToJobObject($job, [Fenix.Job]::GetCurrentProcess())
  if (-not $ok) { Write-Warning "CPU cap not applied (error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }
}
$env:CARGO_BUILD_JOBS = "$Cores"
$exe, $rest = $Command
& $exe @rest
exit $LASTEXITCODE
