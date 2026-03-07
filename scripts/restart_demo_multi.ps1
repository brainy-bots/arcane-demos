# Stop any running Arcane manager and cluster processes, then start the full multi-cluster demo again.
# Use this to restart the entire stack (e.g. after changing demo agent spawn logic).

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot

Write-Host "Stopping existing Arcane processes..."
$killed = 0
foreach ($name in @("arcane-manager", "arcane-cluster-demo")) {
    $procs = Get-Process -Name $name -ErrorAction SilentlyContinue
    if ($procs) {
        $procs | Stop-Process -Force
        $killed += $procs.Count
        Write-Host "  Stopped $($procs.Count) $name process(es)"
    }
}
if ($killed -eq 0) {
    Write-Host "  No Arcane processes were running."
}
Start-Sleep -Seconds 2

Write-Host ""
Write-Host "Starting multi-cluster demo..."
& (Join-Path $ScriptDir "run_demo_multi.ps1")
