# Open Arcane Demo in Unreal Editor when .uproject is not linked to any app.
# Run this script (e.g. right-click → Run with PowerShell) or from a terminal.

$ErrorActionPreference = "Stop"
$ProjectDir = $PSScriptRoot
$UprojectPath = Join-Path $ProjectDir "ArcaneDemo.uproject"

if (-not (Test-Path $UprojectPath)) {
    Write-Error "Not found: $UprojectPath"
}

# Engine version from .uproject (e.g. 5.7)
$json = Get-Content $UprojectPath -Raw | ConvertFrom-Json
$ver = $json.EngineAssociation
if (-not $ver) { $ver = "5.7" }

# Common install locations (Epic Games Launcher default is under Program Files or the drive where launcher is)
$candidates = @(
    "C:\Program Files\Epic Games\UE_$ver\Engine\Binaries\Win64\UnrealEditor.exe",
    "D:\Program Files\Epic Games\UE_$ver\Engine\Binaries\Win64\UnrealEditor.exe",
    "E:\Program Files\Epic Games\UE_$ver\Engine\Binaries\Win64\UnrealEditor.exe",
    "$env:ProgramFiles\Epic Games\UE_$ver\Engine\Binaries\Win64\UnrealEditor.exe",
    "C:\Epic Games\UE_$ver\Engine\Binaries\Win64\UnrealEditor.exe",
    "D:\Epic Games\UE_$ver\Engine\Binaries\Win64\UnrealEditor.exe",
    "E:\Epic Games\UE_$ver\Engine\Binaries\Win64\UnrealEditor.exe"
)

$EditorExe = $null
foreach ($path in $candidates) {
    if (Test-Path $path) {
        $EditorExe = $path
        break
    }
}

if (-not $EditorExe) {
    Write-Host "Unreal Editor (UE $ver) not found in standard locations."
    Write-Host ""
    Write-Host "Do one of the following:"
    Write-Host "  1. Open Epic Games Launcher → Library → Add (browse to this folder) → add ArcaneDemo → Launch from there."
    Write-Host "  2. Or set the path below and run this script again."
    Write-Host ""
    Write-Host "If your engine is elsewhere, run in PowerShell:"
    Write-Host '  & "C:\Path\To\UE_' + $ver + '\Engine\Binaries\Win64\UnrealEditor.exe" "' + $UprojectPath + '"'
    exit 1
}

Write-Host "Starting Unreal Editor ($ver) with project: $UprojectPath"
& $EditorExe $UprojectPath
