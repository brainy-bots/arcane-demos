# Run benchmark: compare Arcane vs default Unreal networking at different entity counts.
# Collects FPS and entity count from game log, writes CSV, and generates a chart.
#
# Prereqs: Unreal project builds; for Arcane mode, ports 8080/8081 free.
#   For SpacetimeDB mode: run "spacetime start" in another terminal first; Unreal must have SpacetimeDB plugin + bindings + SpacetimeDBEntityDisplay.
# Usage (from repo root):
#   .\scripts\benchmark\run_benchmark.ps1                    # Both modes, default counts
#   .\scripts\benchmark\run_benchmark.ps1 -Mode Unreal       # Unreal only (no backend)
#   .\scripts\benchmark\run_benchmark.ps1 -Mode SpacetimeDB   # SpacetimeDB only (run "spacetime start" first)
#   .\scripts\benchmark\run_benchmark.ps1 -Mode SpacetimeDB -SpacetimeDBLimitFinding   # High-end: 100..5000 entities to find limit
#   .\scripts\benchmark\run_benchmark.ps1 -Mode Arcane -EntityCounts 50,100,200
#   .\scripts\benchmark\run_benchmark.ps1 -EntityCounts 200,500,1000 -StressRadius 500   # Push limit, same-place
#   .\scripts\benchmark\run_benchmark.ps1 -SkipBuild         # Skip Unreal build

param(
    [ValidateSet("Unreal", "Arcane", "SpacetimeDB", "Both", "All")]
    [string] $Mode = "Both",
    [int[]]  $EntityCounts = @(50, 100, 200, 500, 1000),
    [double] $StressRadius = 0,
    [switch] $SkipBuild,
    [int]    $WaitSeconds = 25,
    [switch] $NoChart,
    [switch] $SpacetimeDBLimitFinding   # When -Mode SpacetimeDB: use 100,200,500,1000,2000,5000 to find limit on high-end hardware
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$CaptureDir = Join-Path $RepoRoot "scripts\verification-loop\capture"
$LogPath = Join-Path $CaptureDir "game_log.txt"
$RunVerification = Join-Path $RepoRoot "scripts\verification-loop\run_verification.ps1"
$RunDemo = Join-Path $RepoRoot "scripts\run_demo.ps1"
$RunDemoSpacetime = Join-Path $RepoRoot "scripts\run_demo_spacetime.ps1"
$ResultsCsv = Join-Path $RepoRoot "scripts\benchmark\benchmark_results.csv"
$ChartScript = Join-Path $RepoRoot "scripts\benchmark\plot_benchmark.py"
$ClusterLogPath = Join-Path $RepoRoot "scripts\benchmark\cluster_stderr.txt"

if (-not (Test-Path $RunVerification)) { Write-Error "Not found: $RunVerification" }

function Parse-ServerStatsLog {
    param([string] $Content)
    $entities = $null; $clusters = $null; $tickMsList = [System.Collections.Generic.List[double]]::new()
    foreach ($line in ($Content -split "`n")) {
        if ($line -match "ArcaneServerStats:\s*entities=(\d+)\s+clusters=(\d+)\s+tick_ms=([\d.]+)") {
            $entities = [int]$Matches[1]; $clusters = [int]$Matches[2]; $tickMsList.Add([double]$Matches[3])
        }
    }
    $tickMs = if ($tickMsList.Count -gt 0) { ($tickMsList | Measure-Object -Average).Average } else { [double]::NaN }
    return [PSCustomObject]@{ ServerEntities = $entities; ServerClusters = $clusters; ServerTickMs = $tickMs; ServerSamples = $tickMsList.Count }
}

function Get-Median {
    param([double[]] $Values)
    if ($Values.Count -eq 0) { return [double]::NaN }
    $sorted = $Values | Sort-Object
    $mid = [math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 1) { return $sorted[$mid] }
    return ($sorted[$mid - 1] + $sorted[$mid]) / 2
}

function Parse-UnrealLog {
    param([string] $Content)
    $entities = 0
    $fpsList = [System.Collections.Generic.List[double]]::new()
    foreach ($line in ($Content -split "`n")) {
        if ($line -match "UnrealBenchmark:\s*entities=(\d+)\s+FPS=([\d.]+)") {
            $entities = [int]$Matches[1]
            $fpsList.Add([double]$Matches[2])
        }
    }
    $median = Get-Median $fpsList
    $min = if ($fpsList.Count -gt 0) { ($fpsList | Measure-Object -Minimum).Minimum } else { [double]::NaN }
    $max = if ($fpsList.Count -gt 0) { ($fpsList | Measure-Object -Maximum).Maximum } else { [double]::NaN }
    return [PSCustomObject]@{ Entities = $entities; FPS_median = $median; FPS_min = $min; FPS_max = $max; Samples = $fpsList.Count }
}

function Parse-ArcaneLog {
    param([string] $Content)
    $entities = 0
    $fpsList = [System.Collections.Generic.List[double]]::new()
    foreach ($line in ($Content -split "`n")) {
        if ($line -match "FPS=([\d.]+)") { $fpsList.Add([double]$Matches[1]) }
        if ($line -match "Arcane Demo ready: (\d+) entities") { $entities = [int]$Matches[1] }
        if ($line -match "snapshot=(\d+).*FPS=" -and $entities -eq 0) { $entities = [int]$Matches[1] }
    }
    $median = Get-Median $fpsList
    $min = if ($fpsList.Count -gt 0) { ($fpsList | Measure-Object -Minimum).Minimum } else { [double]::NaN }
    $max = if ($fpsList.Count -gt 0) { ($fpsList | Measure-Object -Maximum).Maximum } else { [double]::NaN }
    return [PSCustomObject]@{ Entities = $entities; FPS_median = $median; FPS_min = $min; FPS_max = $max; Samples = $fpsList.Count }
}

# Ensure capture dir exists
if (-not (Test-Path $CaptureDir)) { New-Item -ItemType Directory -Path $CaptureDir -Force | Out-Null }
$results = [System.Collections.Generic.List[object]]::new()

# Build once
if (-not $SkipBuild) {
    Write-Host "Building Arcane Demo..."
    & (Join-Path $RepoRoot "Unreal\ArcaneDemo\BuildArcaneDemo.ps1")
    if ($LASTEXITCODE -ne 0) { Write-Error "Build failed." }
}

# ---- Unreal mode (no backend)
if ($Mode -eq "Unreal" -or $Mode -eq "Both" -or $Mode -eq "All") {
    Write-Host "`n--- Unreal mode (default replication) ---"
    foreach ($N in $EntityCounts) {
        Write-Host "  Running with $N bots..."
        & $RunVerification -BuildAndRunGame -CloseAfter -SkipServerCheck `
            -GameArgs "-UseUnrealNetworking", "-ArcaneBenchmarkBots=$N" `
            -WaitSeconds $WaitSeconds
        if (-not (Test-Path $LogPath)) { Write-Warning "  No log at $LogPath"; continue }
        $content = Get-Content $LogPath -Raw
        $parsed = Parse-UnrealLog $content
        $parsed.Entities = $N
        $results.Add([PSCustomObject]@{ Mode = "Unreal"; Entities = $N; FPS_median = $(if ([double]::IsNaN($parsed.FPS_median)) { "" } else { $parsed.FPS_median }); FPS_min = $(if ([double]::IsNaN($parsed.FPS_min)) { "" } else { $parsed.FPS_min }); FPS_max = $(if ([double]::IsNaN($parsed.FPS_max)) { "" } else { $parsed.FPS_max }); Samples = $parsed.Samples; ServerEntities = ""; ServerClusters = ""; ServerTickMs = "" })
        Write-Host "    Entities=$N FPS_median=$([math]::Round($parsed.FPS_median,1)) (samples=$($parsed.Samples))"
    }
}

# ---- Arcane mode (backend per entity count; capture server stats from cluster log)
if ($Mode -eq "Arcane" -or $Mode -eq "Both" -or $Mode -eq "All") {
    Write-Host "`n--- Arcane mode (library) ---"
    $benchmarkDir = Join-Path $RepoRoot "scripts\benchmark"
    if (-not (Test-Path $benchmarkDir)) { New-Item -ItemType Directory -Path $benchmarkDir -Force | Out-Null }
    foreach ($N in $EntityCounts) {
        if (Test-Path $ClusterLogPath) { Remove-Item $ClusterLogPath -Force }
        $stressArg = if ($StressRadius -gt 0) { " -StressRadius $StressRadius" } else { "" }
        # If a manager/cluster is already running on 8081, don't start another backend; avoid port conflicts.
        $backendProc = $null
        $managerOk = $false
        try {
            $joinResp = Invoke-WebRequest -Uri "http://127.0.0.1:8081/join" -UseBasicParsing -TimeoutSec 2 -ErrorAction Stop
            if ($joinResp.StatusCode -eq 200) { $managerOk = $true }
        } catch {
            $managerOk = $false
        }
        if ($managerOk) {
            Write-Host "  Manager/cluster already running on 8081, skipping backend start (EntityCount=$N)..."
        } else {
            Write-Host "  Starting backend with $N entities (cluster log: $ClusterLogPath)$stressArg..."
            $backendProc = Start-Process powershell -ArgumentList "-NoExit", "-Command", "& '$RunDemo' -EntityCount $N -ClusterLogPath '$ClusterLogPath'$stressArg" -WorkingDirectory $RepoRoot -PassThru
            Start-Sleep -Seconds 15
        }
        Write-Host "  Running game..."
        & $RunVerification -BuildAndRunGame -CloseAfter -WaitSeconds $WaitSeconds
        if ($backendProc -and -not $backendProc.HasExited) {
            $backendProc.Kill()
            Start-Sleep -Seconds 2
        }
        $serverStats = $null
        if (Test-Path $ClusterLogPath) {
            $serverContent = Get-Content $ClusterLogPath -Raw -ErrorAction SilentlyContinue
            if ($serverContent) { $serverStats = Parse-ServerStatsLog $serverContent }
        }
        if (-not (Test-Path $LogPath)) { Write-Warning "  No log at $LogPath"; continue }
        $content = Get-Content $LogPath -Raw
        $parsed = Parse-ArcaneLog $content
        if ($parsed.Entities -eq 0) { $parsed.Entities = $N }
        $sEnt = if ($serverStats -and $null -ne $serverStats.ServerEntities) { $serverStats.ServerEntities } else { "" }
        $sClu = if ($serverStats -and $null -ne $serverStats.ServerClusters) { $serverStats.ServerClusters } else { "" }
        $sMs = if ($serverStats -and -not [double]::IsNaN($serverStats.ServerTickMs)) { [math]::Round($serverStats.ServerTickMs, 2) } else { "" }
        $results.Add([PSCustomObject]@{ Mode = "Arcane"; Entities = $parsed.Entities; FPS_median = $(if ([double]::IsNaN($parsed.FPS_median)) { "" } else { $parsed.FPS_median }); FPS_min = $(if ([double]::IsNaN($parsed.FPS_min)) { "" } else { $parsed.FPS_min }); FPS_max = $(if ([double]::IsNaN($parsed.FPS_max)) { "" } else { $parsed.FPS_max }); Samples = $parsed.Samples; ServerEntities = $sEnt; ServerClusters = $sClu; ServerTickMs = $sMs })
        Write-Host "    Entities=$($parsed.Entities) FPS_median=$([math]::Round($parsed.FPS_median,1)) (samples=$($parsed.Samples)) | server: entities=$sEnt clusters=$sClu tick_ms_avg=$sMs"
    }
}

# ---- SpacetimeDB mode (run "spacetime start" in another terminal first; publish once then simulator per N)
if ($Mode -eq "SpacetimeDB" -or $Mode -eq "All") {
    Write-Host "`n--- SpacetimeDB mode ---"
    if (-not (Test-Path $RunDemoSpacetime)) { Write-Error "Not found: $RunDemoSpacetime" }
    $benchmarkDir = Join-Path $RepoRoot "scripts\benchmark"
    if (-not (Test-Path $benchmarkDir)) { New-Item -ItemType Directory -Path $benchmarkDir -Force | Out-Null }
    $spacetimeCounts = $EntityCounts
    if ($SpacetimeDBLimitFinding) {
        $spacetimeCounts = @(100, 200, 500, 1000, 2000, 5000)
        Write-Host "  SpacetimeDB limit-finding: entity counts $($spacetimeCounts -join ',')"
    }
    # Publish once: run full script with N=1 then kill after build+publish
    Write-Host "  Publishing SpacetimeDB module (one-time)..."
    $publishProc = Start-Process powershell -ArgumentList "-NoExit", "-Command", "& '$RunDemoSpacetime' -EntityCount 1" -WorkingDirectory $RepoRoot -PassThru
    Start-Sleep -Seconds 35
    if ($publishProc -and -not $publishProc.HasExited) { $publishProc.Kill(); Start-Sleep -Seconds 2 }
    foreach ($N in $spacetimeCounts) {
        Write-Host "  Starting SpacetimeDB simulator with $N entities..."
        $simProc = Start-Process powershell -ArgumentList "-NoExit", "-Command", "& '$RunDemoSpacetime' -NoPublish -EntityCount $N" -WorkingDirectory $RepoRoot -PassThru
        Start-Sleep -Seconds 15
        Write-Host "  Running game (SpacetimeDB mode)..."
        & $RunVerification -BuildAndRunGame -CloseAfter -SkipServerCheck `
            -GameArgs "-UseSpacetimeDBNetworking" `
            -WaitSeconds $WaitSeconds
        if ($simProc -and -not $simProc.HasExited) {
            $simProc.Kill()
            Start-Sleep -Seconds 2
        }
        if (-not (Test-Path $LogPath)) { Write-Warning "  No log at $LogPath"; continue }
        $content = Get-Content $LogPath -Raw
        $parsed = Parse-ArcaneLog $content
        if ($parsed.Entities -eq 0) { $parsed.Entities = $N }
        $results.Add([PSCustomObject]@{ Mode = "SpacetimeDB"; Entities = $parsed.Entities; FPS_median = $(if ([double]::IsNaN($parsed.FPS_median)) { "" } else { $parsed.FPS_median }); FPS_min = $(if ([double]::IsNaN($parsed.FPS_min)) { "" } else { $parsed.FPS_min }); FPS_max = $(if ([double]::IsNaN($parsed.FPS_max)) { "" } else { $parsed.FPS_max }); Samples = $parsed.Samples; ServerEntities = ""; ServerClusters = ""; ServerTickMs = "" })
        Write-Host "    Entities=$($parsed.Entities) FPS_median=$([math]::Round($parsed.FPS_median,1)) (samples=$($parsed.Samples))"
    }
}

# Write CSV
$results | Export-Csv -Path $ResultsCsv -NoTypeInformation
Write-Host "`nResults: $ResultsCsv"

# Chart
if (-not $NoChart -and $results.Count -gt 0 -and (Test-Path $ChartScript)) {
    Write-Host "Generating chart..."
    & python $ChartScript $ResultsCsv
}

Write-Host "Done."
