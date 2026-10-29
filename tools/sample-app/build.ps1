# Build the test fixture app (board package + PC half) and a local registry for it.
# Then start fenixOS with FENIX_REGISTRY set to the printed folder to add it from Add app.
# Usage: pwsh tools/sample-app/build.ps1
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot/../.."
Push-Location $root
try {
  New-Item -ItemType Directory -Force target/sample | Out-Null
  cargo run -q --release -p fenix-pkg -- build tools/sample-app/board -o target/sample/sample.fxp
  if ($LASTEXITCODE) { throw "fenix-pkg failed" }
  cargo build -q --release -p fenix-sample
  if ($LASTEXITCODE) { throw "fenix-sample failed" }
  python tools/registry.py --local target/sample-registry tools/sample-app/manifest.json `
    --board target/sample/sample.fxp --pc target/release/fenix-sample.exe
  if ($LASTEXITCODE) { throw "registry failed" }
  "FENIX_REGISTRY=$root\target\sample-registry"
} finally {
  Pop-Location
}
