# Run verification against multi-cluster (Redis + 3 clusters). Use this to verify Step 1 & 3 of DEMO_TODO.
# 1) Starts manager + 3 clusters via run_demo_multi.ps1 (4 new windows). run_demo_multi tries Redis via docker compose if available.
# 2) Waits for manager and cluster to be reachable.
# 3) Runs verification (build, launch game, sequence, capture, close game). Does NOT close the 4 backend windows.
#
# Prereqs: Redis on 127.0.0.1:6379 (e.g. docker compose up -d from repo root). Ports 8080, 8082, 8084, 8081 free.
#          Close any existing single-cluster (run_demo.ps1) windows first.
#
# Usage (from repo root):
#   .\scripts\verification-loop\run_verification_multi.ps1
#   .\scripts\verification-loop\run_verification_multi.ps1 -Force   # skip prompt when manager already running

param([switch] $CloseBackend, [switch] $Force)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$RunDemoMulti = Join-Path $RepoRoot "scripts\run_demo_multi.ps1"
$RunVerification = Join-Path $PSScriptRoot "run_verification.ps1"
$ManagerUrl = "http://127.0.0.1:8081"

function Test-ServersRunning {
    param([string]$ManagerBaseUrl)
    $joinUrl = "$ManagerBaseUrl/join"
    try {
        $r = Invoke-WebRequest -Uri $joinUrl -UseBasicParsing -TimeoutSec 3 -ErrorAction Stop
        if ($r.StatusCode -ne 200) { return $false }
        $json = $r.Content | ConvertFrom-Json
        $host_ = $json.server_host
        $port = $json.server_port
        if (-not $port) { return $true }
        $tcp = $null
        try {
            $tcp = New-Object System.Net.Sockets.TcpClient
            $tcp.ConnectAsync($host_, $port).Wait(2000) | Out-Null
            return $tcp.Connected
        } finally {
            if ($tcp) { try { $tcp.Dispose() } catch { } }
        }
    } catch {
        return $false
    }
}

# If manager is already running, it might be single-cluster — warn unless -Force
if (-not $Force -and (Test-ServersRunning -ManagerBaseUrl $ManagerUrl)) {
    Write-Host "WARNING: Manager is already reachable at $ManagerUrl. If that is single-cluster (run_demo.ps1), close it first so multi-cluster can use the same ports." -ForegroundColor Yellow
    $reply = Read-Host "Continue anyway? (y/N)"
    if ($reply -notmatch "^[yY]") { exit 1 }
}

# Start multi-cluster (4 windows); -SkipRedisCheck avoids interactive prompt if Redis is down
Write-Host "Starting multi-cluster (manager + 3 clusters)..."
& $RunDemoMulti -SkipRedisCheck
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Wait for manager and cluster to be ready
Write-Host "Waiting for manager and cluster (up to 45s)..."
$deadline = [DateTime]::UtcNow.AddSeconds(45)
while ([DateTime]::UtcNow -lt $deadline) {
    if (Test-ServersRunning -ManagerBaseUrl $ManagerUrl) {
        Write-Host "  Manager and cluster OK."
        break
    }
    Start-Sleep -Seconds 2
}
if (-not (Test-ServersRunning -ManagerBaseUrl $ManagerUrl)) {
    Write-Host "Manager or cluster did not become reachable. Check the 4 backend windows for errors."
    exit 1
}

Start-Sleep -Seconds 2

# Run verification without starting backend (multi-cluster is already running)
Write-Host "Running verification (build, launch game, sequence, capture)..."
& $RunVerification -BuildAndRunGame -CloseAfter
$vExit = $LASTEXITCODE

if ($CloseBackend) {
    Write-Host "CloseBackend: close the 4 multi-cluster windows manually (manager + 3 clusters)."
}

exit $vExit
