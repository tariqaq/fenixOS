# Build the sample package with fenix-pkg, then compile and run the C++ reader host test with MSVC.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$root = (Resolve-Path "$PSScriptRoot\..").Path
$repo = (Resolve-Path "$root\..\..\..").Path
$out = Join-Path $env:TEMP 'fenix_pkg_test'
New-Item -ItemType Directory -Force $out | Out-Null
cargo run -q -p fenix-pkg --manifest-path "$repo\Cargo.toml" -- build "$repo\tools\sample-app\board" -o "$out\sample.fxp"
if ($LASTEXITCODE -ne 0) { Write-Host 'fenix-pkg build failed'; exit 1 }
# vcvars64.bat chokes on PATH entries with parentheses, so start from a clean PATH.
$bat = Join-Path $out 'build.cmd'
@"
@echo off
set PATH=C:\Windows\system32;C:\Windows;${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "$out"
cl /nologo /std:c++17 /EHsc /W4 /I "$root\include" "$root\fenix_pkg.cpp" "$PSScriptRoot\test_pkg.cpp" /Fe:t.exe
"@ | Set-Content -Encoding ascii $bat
cmd /c $bat | Where-Object { $_ -notmatch '^(fenix_pkg|test_pkg)\.cpp$' }
if ($LASTEXITCODE -ne 0) { Write-Host 'compile failed'; exit 1 }
& "$out\t.exe" "$out\sample.fxp"
exit $LASTEXITCODE
