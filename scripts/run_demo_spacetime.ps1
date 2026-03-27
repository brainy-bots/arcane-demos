# Run SpacetimeDB demo: publish module, run external simulator.
# Prereqs: SpacetimeDB CLI installed (irm https://windows.spacetimedb.com -useb | iex; confirm when prompted).
#   On Windows the install is interactive. Run "spacetime start" (or "spacetime dev --yes --server-only") in another terminal first; we use --yes on publish for non-interactive runs.
#
# Usage (from repo root):
#   .\scripts\run_demo_spacetime.ps1
#   .\scripts\run_demo_spacetime.ps1 -EntityCount 500
#   .\scripts\run_demo_spacetime.ps1 -EntityCount 200 -StressRadius 500

param(
    [int] $EntityCount = 200,
    [double] $StressRadius = 0,
    [string] $SpacetimeUri = "http://localhost:3000",
    # Must match the local database name we published with `spacetime publish arcane --yes`
    [string] $DatabaseName = "arcane",
    [switch] $NoPublish   # Only run simulator (skip build + publish). For benchmark: publish once, then run with -NoPublish per N.
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$ModulePath = Join-Path $RepoRoot "spacetimedb_demo\spacetimedb"

if (-not (Test-Path $ModulePath)) {
    Write-Error "SpacetimeDB module not found at $ModulePath. Run from arcane-demos repo root."
}

$spacetime = Get-Command spacetime -ErrorAction SilentlyContinue
if (-not $spacetime) {
    Write-Error "SpacetimeDB CLI not found. Install: irm https://windows.spacetimedb.com -useb | iex (interactive on Windows)."
}

if (-not $NoPublish) {
    Write-Host "Building SpacetimeDB module..."
    Push-Location $ModulePath
    try {
        & spacetime build 2>&1
        if ($LASTEXITCODE -ne 0) { throw "spacetime build failed." }
    } finally {
        Pop-Location
    }

    Write-Host "Building arcane-spacetime-sim..."
    Push-Location $RepoRoot
    cargo build -p arcane-demo --bin arcane-spacetime-sim --features spacetime-sim 2>&1
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "arcane-spacetime-sim build failed." }
    Pop-Location

    Write-Host "Publishing module to $SpacetimeUri (database: $DatabaseName)..."
    Push-Location $ModulePath
    try {
        & spacetime publish $DatabaseName --yes 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "spacetime publish failed. Is SpacetimeDB running? Run 'spacetime start' in another terminal."
        }
    } finally {
        Pop-Location
    }
} else {
    Push-Location $RepoRoot
    cargo build -p arcane-demo --bin arcane-spacetime-sim --features spacetime-sim 2>&1 | Out-Null
    Pop-Location
}

$SimExe = Join-Path $RepoRoot "target\debug\arcane-spacetime-sim.exe"
if (-not (Test-Path $SimExe)) { Write-Error "Not found: $SimExe (build without -NoPublish first)." }

$env:SPACETIMEDB_URI = $SpacetimeUri
$env:DATABASE_NAME = $DatabaseName
$env:DEMO_ENTITIES = "$EntityCount"
if ($StressRadius -gt 0) {
    $env:STRESS_RADIUS = "$StressRadius"
    Write-Host "Stress mode: STRESS_RADIUS=$StressRadius (same-place)"
} else {
    Remove-Item Env:\STRESS_RADIUS -ErrorAction SilentlyContinue
}

Write-Host "Starting simulator ($EntityCount entities). Ctrl+C to stop."
& $SimExe
