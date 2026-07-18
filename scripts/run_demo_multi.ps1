# Run the REAL meta-control-layer stack: manager (graph brain + RouterCore + gated flips)
# + N node-demo clusters over Redis. The manager decides ownership from live state keys
# (arcane:state:<id>), publishes per-node inbox frames (arcane:inbox:<id>), and answers
# /join from live assignments.
#
# Prereqs: Redis on 127.0.0.1:6379 (script tries 'docker compose up -d'). Ports 8080/8082/8084 + 8081.
#
# Test-regime knobs (pass-through env for the manager):
#   -JoinPolicy   least-loaded (default) | first-cluster | round-robin
#                 first-cluster = everyone starts on cluster A; Arcane must spread them (the
#                 "fix the overload" regime — most interesting to watch).
#   -CapacityFactor  1.5 default; ~1.0 = force even spread; big = pack together.
#   -EntitiesPerCluster  demo bots per cluster (default 50; use -EntitiesPerCluster 0 for
#                 player-only testing).
#
#   .\scripts\run_demo_multi.ps1 -JoinPolicy first-cluster -CapacityFactor 1.0

param(
    [switch] $SkipRedisCheck,
    [string] $JoinPolicy = "least-loaded",
    [double] $CapacityFactor = 1.5,
    [int]    $EntitiesPerCluster = 50
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$ArcaneRepo = (Resolve-Path (Join-Path $RepoRoot "arcane")).Path

$ClusterA = "550e8400-e29b-41d4-a716-446655440001"
$ClusterB = "550e8400-e29b-41d4-a716-446655440002"
$ClusterC = "550e8400-e29b-41d4-a716-446655440003"
$ManagerClusters = "${ClusterA}:127.0.0.1:8080,${ClusterB}:127.0.0.1:8082,${ClusterC}:127.0.0.1:8084"
$RedisUrl = "redis://127.0.0.1:6379"

# Start Redis if docker compose is available (optional; skip if Redis already running)
if (Get-Command docker -ErrorAction SilentlyContinue) {
    Push-Location $RepoRoot
    $ErrorActionPreference = "Continue"
    docker compose up -d 2>&1 | Out-Null
    $ErrorActionPreference = "Stop"
    Pop-Location
    Start-Sleep -Seconds 2
}

# Ensure Redis is reachable (state keys + inbox frames + replication all need it)
$redisOk = $false
try {
    $tcp = New-Object System.Net.Sockets.TcpClient
    $tcp.ConnectAsync("127.0.0.1", 6379).Wait(3000) | Out-Null
    $redisOk = $tcp.Connected
    $tcp.Close()
} catch { }
if (-not $redisOk) {
    if ($SkipRedisCheck) {
        Write-Host "WARNING: Redis not reachable at 127.0.0.1:6379. The control plane will not work." -ForegroundColor Yellow
    } else {
        Write-Host ""
        Write-Host "WARNING: Redis is not reachable at 127.0.0.1:6379. The meta control layer needs it." -ForegroundColor Yellow
        Write-Host "  Start Redis (e.g. from repo root: docker compose up -d), then re-run." -ForegroundColor Yellow
        Write-Host ""
        $reply = Read-Host "Continue anyway? (y/N)"
        if ($reply -notmatch "^[yY]") { exit 1 }
    }
}

Write-Host "Building manager (migration features) and node-demo..."
$buildErr = $null
$ErrorActionPreference = "Continue"
Push-Location $ArcaneRepo
cargo build -p arcane-infra --bin arcane-manager --features manager,migration 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { $buildErr = "arcane-manager build failed. Ensure arcane repo at $ArcaneRepo." }
Pop-Location
Push-Location $RepoRoot
cargo build -p arcane-demo --bin arcane-node-demo 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { $buildErr = "arcane-node-demo build failed" }
$ErrorActionPreference = "Stop"
Pop-Location
if ($buildErr) { Write-Error $buildErr }

$ManagerExe = Join-Path $ArcaneRepo "target\debug\arcane-manager.exe"
$NodeExe = Join-Path $RepoRoot "target\debug\arcane-node-demo.exe"
if (-not (Test-Path $ManagerExe)) { Write-Error "Not found: $ManagerExe (run build first)" }
if (-not (Test-Path $NodeExe)) { Write-Error "Not found: $NodeExe (run build first)" }

Write-Host "Starting cluster A (ws://127.0.0.1:8080)..."
Start-Process powershell -WorkingDirectory $RepoRoot -ArgumentList "-NoExit", "-Command",
  "`$env:NODE_ID='$ClusterA'; `$env:NODE_WS_PORT='8080'; `$env:REDIS_URL='$RedisUrl'; `$env:NEIGHBOR_IDS='$ClusterB,$ClusterC'; `$env:DEMO_ENTITIES='$EntitiesPerCluster'; & '$NodeExe'"

Start-Sleep -Seconds 1
Write-Host "Starting cluster B (ws://127.0.0.1:8082)..."
Start-Process powershell -WorkingDirectory $RepoRoot -ArgumentList "-NoExit", "-Command",
  "`$env:NODE_ID='$ClusterB'; `$env:NODE_WS_PORT='8082'; `$env:REDIS_URL='$RedisUrl'; `$env:NEIGHBOR_IDS='$ClusterA,$ClusterC'; `$env:DEMO_ENTITIES='$EntitiesPerCluster'; & '$NodeExe'"

Start-Sleep -Seconds 1
Write-Host "Starting cluster C (ws://127.0.0.1:8084)..."
Start-Process powershell -WorkingDirectory $RepoRoot -ArgumentList "-NoExit", "-Command",
  "`$env:NODE_ID='$ClusterC'; `$env:NODE_WS_PORT='8084'; `$env:REDIS_URL='$RedisUrl'; `$env:NEIGHBOR_IDS='$ClusterA,$ClusterB'; `$env:DEMO_ENTITIES='$EntitiesPerCluster'; & '$NodeExe'"

Start-Sleep -Seconds 2
Write-Host "Starting manager (control loop + /join on http://127.0.0.1:8081, policy=$JoinPolicy, capacity=$CapacityFactor)..."
Start-Process powershell -WorkingDirectory $ArcaneRepo -ArgumentList "-NoExit", "-Command",
  "`$env:MANAGER_CLUSTERS='$ManagerClusters'; `$env:MANAGER_HTTP_PORT='8081'; `$env:REDIS_URL='$RedisUrl'; `$env:MANAGER_JOIN_POLICY='$JoinPolicy'; `$env:MANAGER_CAPACITY_FACTOR='$CapacityFactor'; `$env:MANAGER_CADENCE_MS='1000'; & '$ManagerExe'"

Write-Host ""
Write-Host "All 4 windows: 3 nodes + manager (the real control plane: state keys -> graph -> partition -> gated flips -> inbox frames)."
Write-Host "Connect Unreal to http://127.0.0.1:8081/join. Watch the manager window for cycle summaries and migrations."
Write-Host "Try: .\scripts\run_demo_multi.ps1 -JoinPolicy first-cluster -CapacityFactor 1.0  (everyone starts on A; Arcane spreads them)"
