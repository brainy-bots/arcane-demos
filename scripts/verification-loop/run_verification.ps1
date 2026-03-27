# Run the AI verification loop (experiment). Optionally verify/start servers, build, launch game or EDITOR+PIE, then capture.
# Flow when -BuildAndRunGame: (1) Verify manager + cluster, (2) Build, (3) Launch game (UnrealEditor -game), (4) Run sequence, capture.
# Flow when -RunPIE: (1) Verify manager + cluster, (2) Build, (3) Launch EDITOR (no -game), (4) Wait for editor load, (5) Run sequence (starts with play,wait:...), capture. Use this to capture the same PIE context where humanoids are not visible.
# Sequence can be passed as -Sequence or read from scripts/verification-loop/sequence.txt (so the AI can write that file).
#
# Prereqs:
#   - pip install -r scripts/verification-loop/requirements-capture.txt
#   - Manager + cluster: .\scripts\run_demo.ps1 (or use -StartBackend to start them automatically).
#   - For multi-cluster: start Redis + run_demo_multi.ps1 first, then run this without -StartBackend.
#
# Usage (from repo root):
#   # Full loop: verify servers, build, launch GAME, run sequence, capture, close
#   .\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter
#
#   # Full loop: launch EDITOR, send Play (PIE), then run sequence and capture (so AI can see PIE)
#   .\scripts\verification-loop\run_verification.ps1 -RunPIE -CloseAfter
#
#   # Sequence from file (AI can edit scripts/verification-loop/sequence.txt)
#   .\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter
#
#   # Skip server check (e.g. no backend needed)
#   .\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -SkipServerCheck -CloseAfter
#
#   # SpacetimeDB mode: use SpacetimeDB backend + game -UseSpacetimeDBNetworking (run "spacetime start" in another terminal first, or use -StartBackend to start simulator)
#   .\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter -UseSpacetimeDB
#   .\scripts\verification-loop\run_verification.ps1 -BuildAndRunGame -CloseAfter -UseSpacetimeDB -StartBackend

param(
    [string] $ExePath = "",
    [switch] $Launch,
    [switch] $CloseAfter,
    [switch] $BuildAndRunGame,
    [switch] $RunPIE,
    [string] $Keys = "",
    [string] $Sequence = "",
    [string] $SequenceFile = "",
    [int]    $InitialDelaySeconds = 5,
    [int]    $StartupDelaySeconds = 5,
    [int]    $EditorLoadSeconds = 90,
    [int]    $PIEStartupSeconds = 45,
    [string] $WindowTitle = "ArcaneDemo",
    [switch] $StartBackend,
    [switch] $SkipServerCheck,
    [switch] $UseSpacetimeDB,
    [switch] $Beep,
    [switch] $BlockInput,
    [switch] $MinimizeAfterInput,
    [int]    $WaitSeconds = 45,
    [string] $ManagerUrl = "http://127.0.0.1:8081",
    [string[]] $GameArgs = @()
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$UnrealProjectDir = Join-Path $RepoRoot "Unreal\ArcaneDemo"
$UprojectPath = Join-Path $UnrealProjectDir "ArcaneDemo.uproject"
$CaptureDir = Join-Path $PSScriptRoot "capture"
$PythonScript = Join-Path $PSScriptRoot "capture_game.py"
if (-not $SequenceFile) { $SequenceFile = Join-Path $PSScriptRoot "sequence.txt" }

if (-not (Test-Path $PythonScript)) {
    Write-Error "Not found: $PythonScript"
}

$GameProcess = $null
$SpacetimeDBBackendProcess = $null

# ---- 1) Backend: Arcane (manager + cluster) or SpacetimeDB (spacetime start + simulator)
# This check does NOT start any process or open any terminal; it only does in-process HTTP + TCP (or TCP for SpacetimeDB).
function Test-SpacetimeDBRunning {
    param([int] $Port = 3000)
    try {
        $tcp = New-Object System.Net.Sockets.TcpClient
        $tcp.ConnectAsync("127.0.0.1", $Port).Wait(2000) | Out-Null
        $ok = $tcp.Connected
        try { $tcp.Dispose() } catch { }
        return $ok
    } catch {
        return $false
    }
}

function Test-ServersRunning {
    param([string]$ManagerBaseUrl)
    $joinUrl = "$ManagerBaseUrl/join"
    try {
        $r = Invoke-WebRequest -Uri $joinUrl -UseBasicParsing -TimeoutSec 3 -ErrorAction Stop
        if ($r.StatusCode -ne 200) { return $false }
        $json = $r.Content | ConvertFrom-Json
        $host_ = $json.server_host
        $port = $json.server_port
        if (-not $port) { return $true }  # manager up; cluster port unknown
        $tcp = $null
        try {
            $tcp = New-Object System.Net.Sockets.TcpClient
            $tcp.ConnectAsync($host_, $port).Wait(2000) | Out-Null
            return $tcp.Connected
        } finally {
            if ($tcp) {
                try { $tcp.Dispose() } catch { }
            }
        }
    } catch {
        return $false
    }
}

if (($BuildAndRunGame -or $RunPIE) -and -not $SkipServerCheck) {
    if ($UseSpacetimeDB) {
        # SpacetimeDB path: optionally start simulator; verify SpacetimeDB server (port 3000) is reachable.
        $runDemoSpacetime = Join-Path $RepoRoot "scripts\run_demo_spacetime.ps1"
        if ($StartBackend) {
            if (Test-SpacetimeDBRunning -Port 3000) {
                Write-Host "SpacetimeDB (port 3000) reachable. Starting simulator in background..."
                if (Test-Path $runDemoSpacetime) {
                    $SpacetimeDBBackendProcess = Start-Process powershell -ArgumentList "-NoExit", "-Command", "& '$runDemoSpacetime' -NoPublish -EntityCount 200" -WorkingDirectory $RepoRoot -PassThru
                    Start-Sleep -Seconds 15
                } else {
                    Write-Warning "run_demo_spacetime.ps1 not found; ensure SpacetimeDB + simulator are running manually."
                }
            } else {
                Write-Host "Starting SpacetimeDB demo (publish + simulator)..."
                Write-Host "  Prereq: run 'spacetime start' in another terminal first. Starting simulator script (it will publish then run sim)."
                if (Test-Path $runDemoSpacetime) {
                    $SpacetimeDBBackendProcess = Start-Process powershell -ArgumentList "-NoExit", "-Command", "& '$runDemoSpacetime' -EntityCount 200" -WorkingDirectory $RepoRoot -PassThru
                    Start-Sleep -Seconds 35
                } else {
                    Write-Error "Not found: $runDemoSpacetime. Run 'spacetime start' and run_demo_spacetime.ps1 manually, or add -SkipServerCheck."
                }
            }
        }
        Write-Host "Verifying SpacetimeDB (port 3000)..."
        $retries = 5
        while ($retries -gt 0) {
            if (Test-SpacetimeDBRunning -Port 3000) {
                Write-Host "  SpacetimeDB OK (port 3000 reachable)."
                break
            }
            $retries--
            if ($retries -eq 0) {
                Write-Host "SpacetimeDB not reachable on port 3000. Run 'spacetime start' in another terminal, then run_demo_spacetime.ps1 (or use -StartBackend)."
                exit 1
            }
            Write-Host "  Waiting 3s before retry..."
            Start-Sleep -Seconds 3
        }
        if ($BuildAndRunGame -and ($GameArgs | Where-Object { $_ -like "*UseSpacetimeDBNetworking*" }).Count -eq 0) {
            $GameArgs = $GameArgs + @("-UseSpacetimeDBNetworking")
        }
    } else {
        # Arcane path
        if ($StartBackend) {
            if (Test-ServersRunning -ManagerBaseUrl $ManagerUrl) {
                Write-Host "Manager and cluster already running, skipping start."
            } else {
                Write-Host "Starting manager + cluster in background..."
                $runDemo = Join-Path $RepoRoot "scripts\run_demo.ps1"
                if (-not (Test-Path $runDemo)) { Write-Error "Not found: $runDemo" }
                Start-Process powershell -ArgumentList "-NoExit", "-File", $runDemo -WorkingDirectory $RepoRoot
                Start-Sleep -Seconds 20
            }
        }
        Write-Host "Verifying manager and cluster are running..."
        $retries = 5
        while ($retries -gt 0) {
            if (Test-ServersRunning -ManagerBaseUrl $ManagerUrl) {
                Write-Host "  Manager and cluster OK ($ManagerUrl/join -> cluster reachable)."
                break
            }
            $retries--
            if ($retries -eq 0) {
                Write-Host "Manager or cluster not reachable. Start them with: .\scripts\run_demo.ps1"
                Write-Host "Or run this script with -StartBackend to start them automatically, or -SkipServerCheck to skip."
                exit 1
            }
            Write-Host "  Waiting 3s before retry..."
            Start-Sleep -Seconds 3
        }
    }
}

# ---- 2) Sequence: from -Sequence, or from file (AI can edit sequence.txt to define actions)
if (-not $Sequence -and (Test-Path $SequenceFile)) {
    $lines = Get-Content $SequenceFile | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith("#") }
    $Sequence = ($lines | ForEach-Object { $_.Trim() }) -join ","
    if ($Sequence) { Write-Host "Using sequence from $SequenceFile" }
}
if (-not $Sequence -and $BuildAndRunGame) {
    $Sequence = "w:2,mouse:100:0,capture,w:1,mouse:-50:30,capture"
}
if (-not $Sequence -and $RunPIE) {
    $Sequence = "play,wait:$PIEStartupSeconds,w:2,mouse:100:0,capture,w:1,mouse:-50:30,capture"
}

# ---- 3a) Build + run GAME via UnrealEditor -game (no packaging)
if ($BuildAndRunGame) {
    if (-not (Test-Path $UprojectPath)) {
        Write-Error "Not found: $UprojectPath"
    }
    Write-Host "Step 1: Building Arcane Demo..."
    & (Join-Path $UnrealProjectDir "BuildArcaneDemo.ps1")
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed. Fix errors and run again."
    }
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
        if (Test-Path $exe) {
            $UnrealEditorExe = $exe
            break
        }
    }
    if (-not $UnrealEditorExe) {
        Write-Error "UnrealEditor.exe not found. Set UE_PATH or install UE $ver in a standard location."
    }
    $gameArgsList = @("`"$UprojectPath`"", "-game", "-windowed", "-log") + $GameArgs
    Write-Host "Step 2: Launching game (UnrealEditor -game -windowed -log $($GameArgs -join ' '))..."
    $GameProcess = Start-Process -FilePath $UnrealEditorExe -ArgumentList $gameArgsList -WorkingDirectory $UnrealProjectDir -PassThru
    Write-Host "Waiting $StartupDelaySeconds s for game window to appear..."
    Start-Sleep -Seconds $StartupDelaySeconds
}

# ---- 3b) Build + run EDITOR, then sequence sends Play (PIE) so we capture the same context as in-editor
if ($RunPIE) {
    if (-not (Test-Path $UprojectPath)) {
        Write-Error "Not found: $UprojectPath"
    }
    Write-Host "Step 1: Building Arcane Demo..."
    & (Join-Path $UnrealProjectDir "BuildArcaneDemo.ps1")
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed. Fix errors and run again."
    }
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
        if (Test-Path $exe) {
            $UnrealEditorExe = $exe
            break
        }
    }
    if (-not $UnrealEditorExe) {
        Write-Error "UnrealEditor.exe not found. Set UE_PATH or install UE $ver in a standard location."
    }
    Write-Host "Step 2: Launching Unreal EDITOR (no -game)..."
    $GameProcess = Start-Process -FilePath $UnrealEditorExe -ArgumentList "`"$UprojectPath`"", "-windowed", "-log" -WorkingDirectory $UnrealProjectDir -PassThru
    Write-Host "Waiting $EditorLoadSeconds s for editor to load (sequence will send Alt+P for PIE)..."
    Start-Sleep -Seconds $EditorLoadSeconds
    $WindowTitle = "Unreal Editor"
    $InitialDelaySeconds = 0
}

# ---- 4) Capture (excludes File Explorer; optional initial delay + interaction sequence)
$pyArgs = @(
    "--window-title", $WindowTitle,
    "--wait", $WaitSeconds,
    "--output-dir", $CaptureDir
)
if ($ExePath) { $pyArgs += "--exe", $ExePath }
if ($Launch)  { $pyArgs += "--launch" }
if ($CloseAfter -and -not $BuildAndRunGame -and -not $RunPIE) { $pyArgs += "--close-after" }
if ($Keys)    { $pyArgs += "--keys", $Keys }
if ($InitialDelaySeconds -gt 0) { $pyArgs += "--initial-delay", $InitialDelaySeconds }
if ($Sequence) { $pyArgs += "--sequence", $Sequence }
if ($Beep)             { $pyArgs += "--beep" }
if ($BlockInput)       { $pyArgs += "--block-input" }
if ($MinimizeAfterInput) { $pyArgs += "--minimize-after-input" }

if ($BuildAndRunGame -or $RunPIE) {
    $editorLog = Join-Path $UnrealProjectDir "Saved\Logs\ArcaneDemo.log"
    $pyArgs += "--log", $editorLog
}

Write-Host "Running capture (game window only; File Explorer excluded)..."
& python $PythonScript $pyArgs
if ($LASTEXITCODE -ne 0) {
    if ($GameProcess) { $GameProcess.Kill() }
    Write-Host "Capture failed. Is the game window open and not minimized? Title should contain '$WindowTitle' (and not be File Explorer)."
    exit $LASTEXITCODE
}

# ---- Close game/editor (and SpacetimeDB simulator if we started it) when -CloseAfter
if ($CloseAfter -and $GameProcess -and -not $GameProcess.HasExited) {
    Write-Host "Closing process (game or editor)..."
    $GameProcess.Kill()
}
if ($CloseAfter -and $SpacetimeDBBackendProcess -and -not $SpacetimeDBBackendProcess.HasExited) {
    Write-Host "Closing SpacetimeDB simulator..."
    $SpacetimeDBBackendProcess.Kill()
}

Write-Host ""
Write-Host "Verification output (for AI):"
Write-Host "  Screenshots: $CaptureDir\screenshot.png, $CaptureDir\screenshot_1.png, $CaptureDir\screenshot_2.png (if sequence had captures)"
Write-Host "  Log tail:    $CaptureDir\game_log.txt"
Write-Host "  (Cursor AI can read these files to verify the game state.)"
