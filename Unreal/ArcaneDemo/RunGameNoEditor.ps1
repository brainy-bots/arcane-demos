# Build and run the Arcane Demo as a game window only (no Unreal Editor UI).
# Use this to iterate faster: change code, run this script, test in the game.
# The editor must be closed when you run this (build will fail if the editor is open).
#
# Prereqs:
#   - Manager + cluster for Arcane mode: from repo root run .\scripts\run_demo.ps1 (or use -StartBackend).
#   - For Unreal networking mode: no backend needed; game uses replicated bots.
#
# Usage (from this folder, or from repo root with path):
#   .\Unreal\ArcaneDemo\RunGameNoEditor.ps1
#   .\Unreal\ArcaneDemo\RunGameNoEditor.ps1 -StartBackend   # start manager+cluster then run game
#   .\Unreal\ArcaneDemo\RunGameNoEditor.ps1 -BuildOnly     # only build, do not launch
#   .\Unreal\ArcaneDemo\RunGameNoEditor.ps1 -LaunchOnly    # only launch (skip build; use after a previous build)

param(
    [switch] $StartBackend,
    [switch] $BuildOnly,
    [switch] $LaunchOnly
)

$ErrorActionPreference = "Stop"
$ProjectDir = $PSScriptRoot
$UprojectPath = (Resolve-Path (Join-Path $ProjectDir "ArcaneDemo.uproject")).Path
$RepoRoot = (Resolve-Path (Join-Path $ProjectDir "..\..")).Path

if (-not (Test-Path $UprojectPath)) {
    Write-Error "Not found: $UprojectPath"
}

# Optional: start manager + cluster in background
if ($StartBackend) {
    $runDemo = Join-Path $RepoRoot "scripts\run_demo.ps1"
    if (-not (Test-Path $runDemo)) { Write-Error "Not found: $runDemo" }
    Write-Host "Starting manager + cluster in background..."
    Start-Process powershell -ArgumentList "-NoExit", "-File", $runDemo -WorkingDirectory $RepoRoot
    Write-Host "Waiting 15s for backend to be ready..."
    Start-Sleep -Seconds 15
}

# Build (unless LaunchOnly)
if (-not $LaunchOnly) {
    Write-Host "Building Arcane Demo (editor must be closed)..."
    & (Join-Path $ProjectDir "BuildArcaneDemo.ps1")
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed. Close the Unreal Editor and run again."
    }
}

if ($BuildOnly) {
    Write-Host "Build complete. Run without -BuildOnly to launch the game."
    exit 0
}

# Find UnrealEditor.exe
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

Write-Host "Launching game (UnrealEditor -game)..."
Start-Process -FilePath $UnrealEditorExe -ArgumentList "`"$UprojectPath`"", "-game", "-windowed", "-log" -WorkingDirectory $ProjectDir
Write-Host "Game window should open. Close it when done (no editor to close)."
