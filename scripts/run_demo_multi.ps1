# Run manager + multiple clusters with Redis replication. Clients get round-robin assignment; each cluster
# subscribes to neighbors and merges state so Unreal sees full view. Colorize by cluster_id to show ownership.
# Prereqs: Redis (script tries 'docker compose up -d' if available). Ports 8080, 8082, 8084, 8081 must be free.
#   Manager: GET http://localhost:8081/join -> { cluster_id, server_host, server_port } (round-robin of 3 clusters).
#   Clusters: 127.0.0.1:8080, :8082, :8084. Each has DEMO_ENTITIES automated agents and receives neighbor state via Redis.
#
#   -SkipRedisCheck  Skip Redis reachability check and prompt (for use by run_verification_multi.ps1 when Redis is started separately).

param([switch] $SkipRedisCheck)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$ArcaneRepo = (Resolve-Path (Join-Path $RepoRoot "..\arcane")).Path

$ClusterA = "550e8400-e29b-41d4-a716-446655440001"
$ClusterB = "550e8400-e29b-41d4-a716-446655440002"
$ClusterC = "550e8400-e29b-41d4-a716-446655440003"
$ManagerClusters = "${ClusterA}:127.0.0.1:8080,${ClusterB}:127.0.0.1:8082,${ClusterC}:127.0.0.1:8084"
$RedisUrl = "redis://127.0.0.1:6379"

# Per-cluster demo agents. 3 clusters × this = total simulated characters (Maverick-style; goal: scale past their 400–1000).
$DemoEntitiesPerCluster = 50

# Start Redis if docker compose is available (optional; skip if Redis already running)
if (Get-Command docker -ErrorAction SilentlyContinue) {
    Push-Location $RepoRoot
    $ErrorActionPreference = "Continue"
    docker compose up -d 2>&1 | Out-Null
    $ErrorActionPreference = "Stop"
    Pop-Location
    Start-Sleep -Seconds 2
}

# Ensure Redis is reachable (replication will not work without it)
$redisOk = $false
try {
    $tcp = New-Object System.Net.Sockets.TcpClient
    $tcp.ConnectAsync("127.0.0.1", 6379).Wait(3000) | Out-Null
    $redisOk = $tcp.Connected
    $tcp.Close()
} catch { }
if (-not $redisOk) {
    if ($SkipRedisCheck) {
        Write-Host "WARNING: Redis not reachable at 127.0.0.1:6379. Multi-cluster replication may not work." -ForegroundColor Yellow
    } else {
        Write-Host ""
        Write-Host "WARNING: Redis is not reachable at 127.0.0.1:6379. Multi-cluster replication will not work." -ForegroundColor Yellow
        Write-Host "  Start Redis (e.g. from repo root: docker compose up -d) and ensure port 6379 is free, then re-run this script." -ForegroundColor Yellow
        Write-Host ""
        $reply = Read-Host "Continue anyway? (y/N)"
        if ($reply -notmatch "^[yY]") { exit 1 }
    }
}

Write-Host "Building manager (arcane repo) and cluster-demo (this repo)..."
$buildErr = $null
$ErrorActionPreference = "Continue"
Push-Location $ArcaneRepo
cargo build -p arcane-infra --bin arcane-manager --features manager 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { $buildErr = "arcane-manager build failed. Ensure arcane repo at $ArcaneRepo." }
Pop-Location
Push-Location $RepoRoot
cargo build -p arcane-demo --bin arcane-cluster-demo 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { $buildErr = "arcane-cluster-demo build failed" }
$ErrorActionPreference = "Stop"
Pop-Location
if ($buildErr) { Write-Error $buildErr }

$ManagerExe = Join-Path $ArcaneRepo "target\debug\arcane-manager.exe"
$ClusterExe = Join-Path $RepoRoot "target\debug\arcane-cluster-demo.exe"
if (-not (Test-Path $ManagerExe)) { Write-Error "Not found: $ManagerExe (run build first)" }
if (-not (Test-Path $ClusterExe)) { Write-Error "Not found: $ClusterExe (run build first)" }

Write-Host "Starting manager on http://localhost:8081 (round-robin across 3 clusters)"
Start-Process powershell -WorkingDirectory $ArcaneRepo -ArgumentList "-NoExit", "-Command",
  "`$env:MANAGER_CLUSTERS='$ManagerClusters'; `$env:MANAGER_HTTP_PORT='8081'; & '$ManagerExe'"

Start-Sleep -Seconds 2

Write-Host "Starting cluster A (ws://127.0.0.1:8080, neighbors B,C)..."
Start-Process powershell -WorkingDirectory $RepoRoot -ArgumentList "-NoExit", "-Command",
  "`$env:CLUSTER_ID='$ClusterA'; `$env:CLUSTER_WS_PORT='8080'; `$env:REDIS_URL='$RedisUrl'; `$env:NEIGHBOR_IDS='$ClusterB,$ClusterC'; `$env:DEMO_ENTITIES='$DemoEntitiesPerCluster'; & '$ClusterExe'"

Start-Sleep -Seconds 1
Write-Host "Starting cluster B (ws://127.0.0.1:8082, neighbors A,C)..."
Start-Process powershell -WorkingDirectory $RepoRoot -ArgumentList "-NoExit", "-Command",
  "`$env:CLUSTER_ID='$ClusterB'; `$env:CLUSTER_WS_PORT='8082'; `$env:REDIS_URL='$RedisUrl'; `$env:NEIGHBOR_IDS='$ClusterA,$ClusterC'; `$env:DEMO_ENTITIES='$DemoEntitiesPerCluster'; & '$ClusterExe'"

Start-Sleep -Seconds 1
Write-Host "Starting cluster C (ws://127.0.0.1:8084, neighbors A,B)..."
Start-Process powershell -WorkingDirectory $RepoRoot -ArgumentList "-NoExit", "-Command",
  "`$env:CLUSTER_ID='$ClusterC'; `$env:CLUSTER_WS_PORT='8084'; `$env:REDIS_URL='$RedisUrl'; `$env:NEIGHBOR_IDS='$ClusterA,$ClusterB'; `$env:DEMO_ENTITIES='$DemoEntitiesPerCluster'; & '$ClusterExe'"

Write-Host ""
Write-Host "All 4 windows: manager + 3 clusters. Connect Unreal to http://127.0.0.1:8081/join; you will be assigned to one cluster and see all entities colorized by cluster. Close each window to stop."
