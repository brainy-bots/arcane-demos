<#
.SYNOPSIS
    Find Arcane cluster ceiling: run arcane-swarm at increasing player counts (and cluster configs) and record pass/fail.

.DESCRIPTION
    Prerequisites (start these before running):
      - Redis running (default 127.0.0.1:6379).
      - C=1: one arcane-cluster on ws://127.0.0.1:8080.
      - C>1: C arcane-cluster processes on ports 8080, 8082, ... and arcane-manager on http://127.0.0.1:8081 with MANAGER_CLUSTERS set.
    This script only runs the swarm; it does not start cluster/manager processes.
    Parses the "FINAL: players=... total_calls=... total_oks=... total_errs=... lat_avg_ms=..." line to decide pass/fail.

.PARAMETER Clusters
    Number of clusters currently running (1 = use --arcane-ws; 2+ = use --arcane-manager).

.PARAMETER PlayerCounts
    List of player counts to try (e.g. 100,200,300,400). Ignored if -FindCeiling is set.

.PARAMETER FindCeiling
    If set, auto-sweep from -Step up by -Step until failure or -MaxPlayers. Pass = err_rate < MaxErrRate and lat_avg_ms < MaxLatencyMs.

.PARAMETER Step
    When -FindCeiling: try N = Step, 2*Step, 3*Step, ... (default 100).

.PARAMETER MaxPlayers
    When -FindCeiling: stop when N exceeds this (default 2000).

.PARAMETER Duration
    Seconds per run (default 45). Shorter = faster sweep but noisier.

.PARAMETER MaxErrRate
    Pass if total_errs / total_calls < this (default 0.01 = 1%).

.PARAMETER MaxLatencyMs
    Pass if lat_avg_ms < this (default 200).

.PARAMETER OutCsv
    Append results to this CSV (default: arcane_ceiling_sweep.csv in script dir).

.EXAMPLE
    # One cluster running on 8080; try 100,200,...,500
    .\Run-ArcaneCeilingSweep.ps1 -Clusters 1 -PlayerCounts 100,200,300,400,500

.EXAMPLE
    # Find ceiling for 1 cluster (try 100, 200, 300, ... until fail or 2000)
    .\Run-ArcaneCeilingSweep.ps1 -Clusters 1 -FindCeiling -Step 100 -MaxPlayers 2000

.EXAMPLE
    # Two clusters + manager running; find ceiling
    .\Run-ArcaneCeilingSweep.ps1 -Clusters 2 -FindCeiling -Step 100
#>
param(
    [int]   $Clusters = 1,
    [int[]] $PlayerCounts = @(100, 200, 300, 400, 500),
    [switch] $FindCeiling,
    [int]   $Step = 100,
    [int]   $MaxPlayers = 2000,
    [int]   $Duration = 45,
    [double] $MaxErrRate = 0.01,
    [double] $MaxLatencyMs = 200,
    [string] $OutCsv = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Exe = Join-Path $RepoRoot "target\release\arcane-swarm.exe"
if ($OutCsv -eq "") { $OutCsv = Join-Path $PSScriptRoot "arcane_ceiling_sweep.csv" }

if (-not (Test-Path $Exe)) {
    Write-Host "Building arcane-swarm (release)..." -ForegroundColor Yellow
    Push-Location $RepoRoot
    cargo build -p arcane-demo --bin arcane-swarm --features swarm --release 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "Build failed" }
    Pop-Location
}

function Parse-FinalLine {
    param([string] $Stderr)
    if ($Stderr -match "FINAL:\s*players=(\d+)\s+total_calls=(\d+)\s+total_oks=(\d+)\s+total_errs=(\d+)\s+lat_avg_ms=([\d.]+)") {
        return [PSCustomObject]@{
            players = [int]$Matches[1]
            total_calls = [long]$Matches[2]
            total_oks = [long]$Matches[3]
            total_errs = [long]$Matches[4]
            lat_avg_ms = [double]$Matches[5]
        }
    }
    return $null
}

function Test-Pass {
    param($Parsed, [double]$MaxErrRate, [double]$MaxLatencyMs)
    if (-not $Parsed) { return $false }
    $errRate = if ($Parsed.total_calls -gt 0) { $Parsed.total_errs / $Parsed.total_calls } else { 1.0 }
    return ($errRate -lt $MaxErrRate -and $Parsed.lat_avg_ms -lt $MaxLatencyMs)
}

# Build list of (Clusters, Players) to run
$runs = [System.Collections.Generic.List[object]]::new()
if ($FindCeiling) {
    $N = $Step
    while ($N -le $MaxPlayers) {
        $runs.Add([PSCustomObject]@{ Clusters = $Clusters; Players = $N })
        $N += $Step
    }
} else {
    foreach ($P in $PlayerCounts) {
        $runs.Add([PSCustomObject]@{ Clusters = $Clusters; Players = $P })
    }
}

$results = [System.Collections.Generic.List[object]]::new()
$ceiling = $null   # last passing N for this C

Write-Host "`n=== Arcane ceiling sweep ===" -ForegroundColor Cyan
Write-Host "  Clusters: $Clusters | Runs: $($runs.Count) | Duration: ${Duration}s | Pass: err_rate<$MaxErrRate lat_avg_ms<$MaxLatencyMs"
Write-Host "  OutCsv: $OutCsv`n"

foreach ($run in $runs) {
    $C = $run.Clusters
    $N = $run.Players
    $ppc = [math]::Round($N / $C, 0)
    Write-Host "[C=$C N=$N ppc=$ppc] Running..." -NoNewline
    $args = @(
        "--backend", "arcane",
        "--players", $N,
        "--tick-rate", "10",
        "--duration", $Duration,
        "--mode", "spread"
    )
    if ($C -eq 1) {
        $args += @("--arcane-ws", "ws://127.0.0.1:8080")
    } else {
        $args += @("--arcane-manager", "http://127.0.0.1:8081")
    }
    $tmpOut = [System.IO.Path]::GetTempFileName()
    $tmpErr = [System.IO.Path]::GetTempFileName()
    $proc = Start-Process -FilePath $Exe -ArgumentList $args -WorkingDirectory $RepoRoot -RedirectStandardOutput $tmpOut -RedirectStandardError $tmpErr -Wait -NoNewWindow -PassThru
    $out = Get-Content -Path $tmpOut -Raw -ErrorAction SilentlyContinue
    $err = Get-Content -Path $tmpErr -Raw -ErrorAction SilentlyContinue
    $stderr = if ($out) { $out } else { "" }; if ($err) { $stderr += "`n" + $err }
    Remove-Item -Path $tmpOut, $tmpErr -Force -ErrorAction SilentlyContinue
    $parsed = Parse-FinalLine $stderr
    $pass = Test-Pass $parsed $MaxErrRate $MaxLatencyMs
    if ($pass) { $ceiling = $N }
    $total_calls = if ($parsed) { $parsed.total_calls } else { 0 }
    $total_oks   = if ($parsed) { $parsed.total_oks } else { 0 }
    $total_errs  = if ($parsed) { $parsed.total_errs } else { 0 }
    $lat_avg_ms  = if ($parsed) { $parsed.lat_avg_ms } else { 0.0 }
    $err_rate    = if ($parsed -and $parsed.total_calls -gt 0) { $parsed.total_errs / $parsed.total_calls } else { 1.0 }
    $status = if ($pass) { "PASS" } else { "FAIL" }
    Write-Host " $status total_calls=$total_calls errs=$total_errs err_rate=$([math]::Round($err_rate*100,2))% lat_avg_ms=$([math]::Round($lat_avg_ms,1))"
    $results.Add([PSCustomObject]@{
        clusters = $C
        players = $N
        players_per_cluster = $ppc
        total_calls = $total_calls
        total_oks = $total_oks
        total_errs = $total_errs
        err_rate_pct = [math]::Round($err_rate * 100, 2)
        lat_avg_ms = [math]::Round($lat_avg_ms, 2)
        pass = $pass
    })
    if ($FindCeiling -and -not $pass) {
        Write-Host "  Ceiling reached at N=$N (last pass N=$ceiling). Stopping sweep."
        break
    }
}

# Write CSV (create with header, or append rows for additional cluster runs)
$csvExists = Test-Path $OutCsv
$results | Export-Csv -Path $OutCsv -NoTypeInformation -Append:$csvExists
$writeAction = if ($csvExists) { "Appended" } else { "Wrote" }; Write-Host "`n$writeAction : $OutCsv" -ForegroundColor Green

# Summary
Write-Host "`n--- Summary ---" -ForegroundColor Cyan
$passCount = ($results | Where-Object { $_.pass }).Count
$lastPass = $results | Where-Object { $_.pass } | Select-Object -Last 1
if ($lastPass) {
    Write-Host "  Ceiling for C=$Clusters : N=$($lastPass.players) (players_per_cluster=$($lastPass.players_per_cluster))"
} else {
    Write-Host "  No passing run for C=$Clusters."
}
Write-Host "  Passed: $passCount / $($results.Count) runs"
Write-Host "Done."
