# Generate Unreal C++ bindings from the SpacetimeDB demo module.
# Prereqs: SpacetimeDB CLI on PATH (e.g. %LocalAppData%\SpacetimeDB). Run from repo root.
# On Windows install is interactive — use --yes where supported.
#
# Usage: .\scripts\generate_spacetime_bindings.ps1

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$UprojectDir = Join-Path $RepoRoot "Unreal\ArcaneDemo"
$ModulePath = Join-Path $RepoRoot "spacetimedb_demo\spacetimedb"

if (-not (Test-Path $ModulePath)) { Write-Error "Module not found: $ModulePath" }
if (-not (Test-Path (Join-Path $UprojectDir "ArcaneDemo.uproject"))) { Write-Error "Project not found: $UprojectDir" }

$spacetime = Get-Command spacetime -ErrorAction SilentlyContinue
if (-not $spacetime) {
    $spacetimeExe = Join-Path $env:LocalAppData "SpacetimeDB\spacetime.exe"
    if (Test-Path $spacetimeExe) { $env:Path = "$env:LocalAppData\SpacetimeDB;" + $env:Path }
    else { Write-Error "SpacetimeDB CLI not found. Install: irm https://windows.spacetimedb.com -useb | iex" }
}

Write-Host "Generating Unreal bindings (module: $ModulePath, uproject: $UprojectDir)..."
Push-Location $RepoRoot
try {
    & spacetime generate -l unrealcpp --uproject-dir $UprojectDir -p $ModulePath --unreal-module-name ArcaneDemo 2>&1
    if ($LASTEXITCODE -ne 0) { throw "spacetime generate failed" }
    Write-Host "Done. Rebuild the Unreal project. Bindings in project ModuleBindings."
} finally {
    Pop-Location
}
