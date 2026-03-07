# Build Arcane Demo modules from the command line (no IDE, no editor running).
# Run this when you see "Engine modules are out of date, and cannot be compiled while the engine is running."
# After it succeeds, open the project from the Launcher or OpenArcaneDemo.ps1 — the editor will load the built modules.

$ErrorActionPreference = "Stop"
$ProjectDir = $PSScriptRoot
$UprojectPath = (Resolve-Path (Join-Path $ProjectDir "ArcaneDemo.uproject")).Path

if (-not (Test-Path $UprojectPath)) {
    Write-Error "Not found: $UprojectPath"
}

# Engine version from .uproject
$json = Get-Content $UprojectPath -Raw | ConvertFrom-Json
$ver = $json.EngineAssociation
if (-not $ver) { $ver = "5.7" }

# Build.bat lives in Engine\Build\BatchFiles (sibling of Binaries)
$buildBatCandidates = @(
    "E:\UE_$ver\Engine\Build\BatchFiles\Build.bat",
    "C:\Program Files\Epic Games\UE_$ver\Engine\Build\BatchFiles\Build.bat",
    "D:\Program Files\Epic Games\UE_$ver\Engine\Build\BatchFiles\Build.bat",
    "E:\Program Files\Epic Games\UE_$ver\Engine\Build\BatchFiles\Build.bat",
    "$env:ProgramFiles\Epic Games\UE_$ver\Engine\Build\BatchFiles\Build.bat",
    "C:\Epic Games\UE_$ver\Engine\Build\BatchFiles\Build.bat",
    "D:\Epic Games\UE_$ver\Engine\Build\BatchFiles\Build.bat",
    "E:\Epic Games\UE_$ver\Engine\Build\BatchFiles\Build.bat"
)

$BuildBat = $null
foreach ($path in $buildBatCandidates) {
    if (Test-Path $path) {
        $BuildBat = $path
        break
    }
}

if (-not $BuildBat) {
    Write-Host "Unreal Engine $ver Build.bat not found in standard locations."
    Write-Host ""
    Write-Host "Set your engine root and run the build manually. Example (PowerShell):"
    Write-Host '  $UE = "C:\Program Files\Epic Games\UE_5.7"'
    Write-Host '  & "$UE\Engine\Build\BatchFiles\Build.bat" ArcaneDemoEditor Win64 Development "' + $UprojectPath + '"'
    Write-Host ""
    Write-Host "Or create a shortcut that runs the above (replace the path with your engine install)."
    exit 1
}

Write-Host "Building Arcane Demo (UE $ver). This may take a few minutes..."
Write-Host ""

& $BuildBat ArcaneDemoEditor Win64 Development $UprojectPath -waitmutex

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Build failed (exit code $LASTEXITCODE). Fix any errors above, then run this script again."
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Build succeeded. You can now open the project from the Epic Games Launcher or run OpenArcaneDemo.ps1."
exit 0
