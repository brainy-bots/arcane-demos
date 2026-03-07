# Run manager + one cluster for the demo. Uses a fixed cluster UUID so GET /join returns the cluster this process runs.
# Prereqs: Redis not required for single-cluster (no neighbors).
#   Manager: GET http://localhost:8081/join -> { cluster_id, server_host, server_port }.
#   Cluster: WebSocket ws://127.0.0.1:8080 receives STATE_UPDATE (EntityStateDelta JSON) each tick.
#   DEMO_ENTITIES seeds demo agents (path-like movement). Use -EntityCount for benchmark.
#
# Usage: .\scripts\run_demo.ps1
#        .\scripts\run_demo.ps1 -EntityCount 100
#        .\scripts\run_demo.ps1 -EntityCount 200 -ClusterLogPath E:\path\to\cluster.log  # for benchmark capture
#        .\scripts\run_demo.ps1 -EntityCount 1000 -StressRadius 500   # push limit: same-place stress (entities confined to radius 500)

param([int] $EntityCount = 200, [string] $ClusterLogPath = "", [double] $StressRadius = 0)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$ArcaneRepo = Resolve-Path (Join-Path $RepoRoot "..\arcane")
$DemoClusterId = "550e8400-e29b-41d4-a716-446655440000"

Write-Host "Building manager (arcane repo) and cluster-demo (this repo)..."
$ErrorActionPreference = "Continue"
Push-Location $ArcaneRepo
cargo build -p arcane-infra --bin arcane-manager --features manager 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Pop-Location; $ErrorActionPreference = "Stop"; throw "Manager build failed. Ensure arcane repo is at $ArcaneRepo." }
Pop-Location
Push-Location $RepoRoot
cargo build -p arcane-demo --bin arcane-cluster-demo 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Pop-Location; $ErrorActionPreference = "Stop"; throw "Cluster-demo build failed." }
$ErrorActionPreference = "Stop"
Pop-Location

$ManagerExe = Join-Path $ArcaneRepo "target\debug\arcane-manager.exe"
$ClusterExe = Join-Path $RepoRoot "target\debug\arcane-cluster-demo.exe"
if (-not (Test-Path $ManagerExe)) { Write-Error "Not found: $ManagerExe (build failed?)" }
if (-not (Test-Path $ClusterExe)) { Write-Error "Not found: $ClusterExe (build failed?)" }

Write-Host "Starting manager on http://localhost:8081 (assigns cluster $DemoClusterId -> 127.0.0.1:8080)"
Start-Process powershell -WorkingDirectory $ArcaneRepo -ArgumentList "-NoExit", "-Command",
  "`$env:MANAGER_CLUSTER_ID='$DemoClusterId'; `$env:MANAGER_SERVER_HOST='127.0.0.1'; `$env:MANAGER_SERVER_PORT='8080'; `$env:MANAGER_HTTP_PORT='8081'; & '$ManagerExe'"

Start-Sleep -Seconds 2

if ($StressRadius -gt 0) { $env:STRESS_RADIUS = "$StressRadius"; Write-Host "Stress mode: STRESS_RADIUS=$StressRadius (same-place)" }
Write-Host "Starting cluster (WebSocket ws://127.0.0.1:8080, DEMO_ENTITIES=$EntityCount). Ctrl+C to stop."
$env:CLUSTER_ID = $DemoClusterId
$env:CLUSTER_WS_PORT = "8080"
$env:DEMO_ENTITIES = "$EntityCount"
if ($StressRadius -le 0) { Remove-Item Env:\STRESS_RADIUS -ErrorAction SilentlyContinue }
Push-Location $RepoRoot
if ($ClusterLogPath) {
    & $ClusterExe 2>&1 | Tee-Object -FilePath $ClusterLogPath -Append
} else {
    & $ClusterExe
}
Pop-Location
