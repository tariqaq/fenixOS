# Build the board simulator with MSVC + Ninja (both come with Visual Studio's C++ tools).
# Output: board/sim/build/fenix_sim.exe
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$sim = $PSScriptRoot
$out = Join-Path $sim 'build'
New-Item -ItemType Directory -Force $out | Out-Null
# vcvars64.bat chokes on PATH entries with parentheses, so start from a clean PATH.
$bat = Join-Path $out 'build.cmd'
@"
@echo off
set PATH=C:\Windows\system32;C:\Windows;C:\Windows\System32\WindowsPowerShell\v1.0
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cmake -S "$sim" -B "$out" -G Ninja -DCMAKE_BUILD_TYPE=Release >nul || exit /b 1
cmake --build "$out"
"@ | Set-Content -Encoding ascii $bat
cmd /c $bat
exit $LASTEXITCODE
