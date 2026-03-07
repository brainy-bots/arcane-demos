# Package Arcane Demo as a standalone Windows (No Editor) build for the verification loop.
# Output: Unreal\ArcaneDemo\Saved\StagedBuilds\WindowsNoEditor\
# Run the .exe from there (or use scripts/run_verification.ps1 which can launch it).
# Prereq: Unreal Engine 5.7 (or set $env:UE_PATH).

$ErrorActionPreference = "Stop"
$ProjectDir = $PSScriptRoot
$UprojectPath = (Resolve-Path (Join-Path $ProjectDir "ArcaneDemo.uproject")).Path

if (-not (Test-Path $UprojectPath)) {
    Write-Error "Not found: $UprojectPath"
}

$json = Get-Content $UprojectPath -Raw | ConvertFrom-Json
$ver = $json.EngineAssociation
if (-not $ver) { $ver = "5.7" }

# Find RunUAT.bat (same Engine install as Build.bat)
$runUatCandidates = @(
    "E:\UE_$ver\Engine\Build\BatchFiles\RunUAT.bat",
    "C:\Program Files\Epic Games\UE_$ver\Engine\Build\BatchFiles\RunUAT.bat",
    "D:\Program Files\Epic Games\UE_$ver\Engine\Build\BatchFiles\RunUAT.bat",
    "E:\Program Files\Epic Games\UE_$ver\Engine\Build\BatchFiles\RunUAT.bat",
    "$env:ProgramFiles\Epic Games\UE_$ver\Engine\Build\BatchFiles\RunUAT.bat",
    "C:\Epic Games\UE_$ver\Engine\Build\BatchFiles\RunUAT.bat",
    "D:\Epic Games\UE_$ver\Engine\Build\BatchFiles\RunUAT.bat",
    "E:\Epic Games\UE_$ver\Engine\Build\BatchFiles\RunUAT.bat"
)
if ($env:UE_PATH) {
    $runUatCandidates = @("$env:UE_PATH\Engine\Build\BatchFiles\RunUAT.bat") + $runUatCandidates
}

$RunUat = $null
foreach ($path in $runUatCandidates) {
    if (Test-Path $path) {
        $RunUat = $path
        break
    }
}

if (-not $RunUat) {
    Write-Host "RunUAT.bat not found. Set UE_PATH or install UE $ver in a standard location."
    exit 1
}

# Development config so logs are written (Shipping often disables logs)
Write-Host "Packaging Arcane Demo (Windows No Editor, Development). This can take 10+ minutes..."
$args = @(
    "BuildCookRun",
    "-project=$($UprojectPath -replace '\\','/')",
    "-platform=Win64",
    "-clientconfig=Development",
    "-noP4",
    "-cook",
    "-build",
    "-stage",
    "-pak",
    "-package"
)

& $RunUat $args
if ($LASTEXITCODE -ne 0) {
    Write-Host "Package failed (exit code $LASTEXITCODE)."
    exit $LASTEXITCODE
}

$stageRoot = Join-Path $ProjectDir "Saved\StagedBuilds\WindowsNoEditor"
$exePath = Join-Path $stageRoot "ArcaneDemo\Binaries\Win64\ArcaneDemo.exe"
if (-not (Test-Path $exePath)) { $exePath = Join-Path $stageRoot "ArcaneDemo.exe" }
if (Test-Path $exePath) {
    Write-Host ""
    Write-Host "Packaged exe: $exePath"
    Write-Host "Run verification: from repo root, .\scripts\run_verification.ps1 -ExePath `"$exePath`""
} else {
    Write-Host "Staged build completed. Look for ArcaneDemo.exe under: $stageRoot"
}
exit 0
