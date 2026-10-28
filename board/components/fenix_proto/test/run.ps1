# Compile and run the C++ protocol host test with MSVC.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$root = (Resolve-Path "$PSScriptRoot\..").Path
$out = Join-Path $env:TEMP 'fenix_proto_test'
New-Item -ItemType Directory -Force $out | Out-Null
# vcvars64.bat chokes on PATH entries with parentheses, so start from a clean PATH.
$bat = Join-Path $out 'build.cmd'
@"
@echo off
set PATH=C:\Windows\system32;C:\Windows;${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "$out"
cl /nologo /std:c++17 /EHsc /W4 /I "$root\include" "$root\fenix_proto.cpp" "$PSScriptRoot\test_vectors.cpp" /Fe:t.exe
"@ | Set-Content -Encoding ascii $bat
cmd /c $bat | Where-Object { $_ -notmatch '^(fenix_proto|test_vectors)\.cpp$' }
if ($LASTEXITCODE -ne 0) { Write-Host 'compile failed'; exit 1 }
& "$out\t.exe" (Resolve-Path "$root\..\..\..\proto\vectors.txt").Path
exit $LASTEXITCODE
