# Launch two Arcane Demo game clients so you can see each client's movement replicated to the other.
# Both connect to the same manager/cluster; each sends PLAYER_STATE and receives STATE_UPDATE with
# all entities (demo agents + the other player). Move in one window and watch the other window
# to see the replicated character.
#
# Prereqs: Backend running (.\scripts\run_demo.ps1). Close any existing game windows if you want a clean run.
#
# Usage (from repo root):
#   .\scripts\run_two_clients.ps1           # Build once, launch 2 game windows (backend must already be running)
#   .\scripts\run_two_clients.ps1 -StartBackend   # Start manager+cluster if not running, then launch 2 clients

param([switch] $StartBackend)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$UnrealProjectDir = Join-Path $RepoRoot "Unreal\ArcaneDemo"
$UprojectPath = Join-Path $UnrealProjectDir "ArcaneDemo.uproject"
$ManagerUrl = "http://127.0.0.1:8081"

function Test-ServersRunning {
    param([string]$ManagerBaseUrl)
    $joinUrl = "$ManagerBaseUrl/join"
    try {
        $r = Invoke-WebRequest -Uri $joinUrl -UseBasicParsing -TimeoutSec 3 -ErrorAction Stop
        if ($r.StatusCode -ne 200) { return $false }
        $json = $r.Content | ConvertFrom-Json
        $port = $json.server_port
        if (-not $port) { return $true }
        $tcp = $null
        try {
            $tcp = New-Object System.Net.Sockets.TcpClient
            $tcp.ConnectAsync($json.server_host, $port).Wait(2000) | Out-Null
            return $tcp.Connected
        } finally { if ($tcp) { try { $tcp.Dispose() } catch { } } }
    } catch { return $false }
}

if ($StartBackend) {
    if (Test-ServersRunning -ManagerBaseUrl $ManagerUrl) {
        Write-Host "Manager and cluster already running."
    } else {
        Write-Host "Starting manager + cluster..."
        $runDemo = Join-Path $RepoRoot "scripts\run_demo.ps1"
        Start-Process powershell -ArgumentList "-NoExit", "-File", $runDemo -WorkingDirectory $RepoRoot
        Start-Sleep -Seconds 20
        $retries = 5
        while ($retries -gt 0) {
            if (Test-ServersRunning -ManagerBaseUrl $ManagerUrl) {
                Write-Host "  Backend OK."
                break
            }
            $retries--
            Write-Host "  Waiting for backend..."
            Start-Sleep -Seconds 3
        }
        if (-not (Test-ServersRunning -ManagerBaseUrl $ManagerUrl)) {
            Write-Error "Backend did not become reachable. Check the manager and cluster windows."
        }
    }
} else {
    if (-not (Test-ServersRunning -ManagerBaseUrl $ManagerUrl)) {
        Write-Host "Manager/cluster not reachable at $ManagerUrl. Start them with: .\scripts\run_demo.ps1" -ForegroundColor Yellow
        Write-Host "Or run this script with -StartBackend to start them automatically." -ForegroundColor Yellow
        exit 1
    }
}

Write-Host "Building Arcane Demo..."
& (Join-Path $UnrealProjectDir "BuildArcaneDemo.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$json = Get-Content $UprojectPath -Raw | ConvertFrom-Json
$ver = $json.EngineAssociation
if (-not $ver) { $ver = "5.7" }
$ueCandidates = @(
    "E:\UE_$ver",
    "C:\Program Files\Epic Games\UE_$ver",
    "D:\Program Files\Epic Games\UE_$ver",
    "E:\Program Files\Epic Games\UE_$ver",
    "$env:ProgramFiles\Epic Games\UE_$ver",
    "C:\Epic Games\UE_$ver",
    "D:\Epic Games\UE_$ver",
    "E:\Epic Games\UE_$ver"
)
if ($env:UE_PATH) { $ueCandidates = @($env:UE_PATH) + $ueCandidates }
$UnrealEditorExe = $null
foreach ($ue in $ueCandidates) {
    $exe = Join-Path $ue "Engine\Binaries\Win64\UnrealEditor.exe"
    if (Test-Path $exe) { $UnrealEditorExe = $exe; break }
}
if (-not $UnrealEditorExe) {
    Write-Error "UnrealEditor.exe not found. Set UE_PATH or install UE $ver."
}

Write-Host "Launching first client..."
Start-Process -FilePath $UnrealEditorExe -ArgumentList "`"$UprojectPath`"", "-game", "-windowed", "-log" -WorkingDirectory $UnrealProjectDir
Start-Sleep -Seconds 3
Write-Host "Launching second client..."
Start-Process -FilePath $UnrealEditorExe -ArgumentList "`"$UprojectPath`"", "-game", "-windowed", "-log" -WorkingDirectory $UnrealProjectDir

Write-Host ""
Write-Host "Two game windows should open. Both connect to $ManagerUrl and see the same world."
Write-Host "Move in one window; the other window will show your character moving (replicated via server)."
Write-Host "Close each game window when done."
