# Run idf.py with the ESP-IDF v5.5.5 environment (installed by EIM at C:\Espressif).
# Usage: pwsh -NoProfile -File board/idf.ps1 build
# EIM's own activation script fails to parse in pwsh 7, so the env is set here directly.
$T = 'C:\Espressif\tools'
# idf.py refuses to run when launched from Git Bash (MSYSTEM set).
Remove-Item env:MSYSTEM -ErrorAction SilentlyContinue
$env:IDF_PATH = 'C:\Espressif\v5.5.5\esp-idf'
$env:IDF_TOOLS_PATH = $T
$env:IDF_PYTHON_ENV_PATH = "$T\python\v5.5.5\venv"
$env:IDF_COMPONENT_LOCAL_STORAGE_URL = "file://$T"
$env:ESP_ROM_ELF_DIR = "$T\esp-rom-elfs\20241011/"
$env:IDF_CCACHE_ENABLE = '1'
$env:ESP_IDF_VERSION = '5.5'
$env:PATH = @(
  "$T\ccache\4.12.1\ccache-4.12.1-windows-x86_64",
  "$T\cmake\3.30.2\bin",
  "$T\esp-rom-elfs\20241011",
  "$T\idf-exe\1.0.3",
  "$T\ninja\1.12.1",
  "$T\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin",
  "$T\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin",
  "$T\python\v5.5.5\venv\Scripts",
  $env:PATH
) -join ';'
Set-Location $PSScriptRoot
& "$T\python\v5.5.5\venv\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" @args
exit $LASTEXITCODE
