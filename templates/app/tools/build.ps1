# Build the app into dist/: app.fxp (board half) and, when app/pc exists, app.exe (PC half).
# Needs fenix-pkg: cargo install --git https://github.com/tariqaq/fenixOS --tag v0.1.13 fenix-pkg
# Usage: pwsh tools/build.ps1
$ErrorActionPreference = "Stop"
Push-Location "$PSScriptRoot/.."
try {
  New-Item -ItemType Directory -Force dist | Out-Null
  fenix-pkg build app/board -o dist/app.fxp
  if ($LASTEXITCODE) { throw "fenix-pkg failed" }
  if (Test-Path app/pc/Cargo.toml) {
    cargo build --release -p app
    if ($LASTEXITCODE) { throw "cargo build failed" }
    Copy-Item target/release/app.exe dist/app.exe
  }
  python tools/app.py check
  if ($LASTEXITCODE) { throw "manifest check failed" }
} finally {
  Pop-Location
}
