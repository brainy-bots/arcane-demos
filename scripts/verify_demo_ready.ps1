# One-command demo readiness check: cargo check, then run verification loop (build + launch game + capture).
# Exits 0 if all pass; non-zero if any step fails. Use this to confirm the demo is in a runnable state.
# From repo root: .\scripts\verify_demo_ready.ps1

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

Write-Host "=== 1) Cargo check (workspace) ===" -ForegroundColor Cyan
Push-Location $RepoRoot
cargo check --workspace 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: cargo check" -ForegroundColor Red
    exit 1
}
Write-Host "OK: cargo check" -ForegroundColor Green
Pop-Location

Write-Host ""
Write-Host "=== 2) Verification loop (start backend, build game, launch, capture, close) ===" -ForegroundColor Cyan
& (Join-Path $RepoRoot "scripts\verification-loop\run_verification.ps1") -BuildAndRunGame -StartBackend -CloseAfter
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: verification loop" -ForegroundColor Red
    exit 2
}

Write-Host ""
Write-Host "Demo ready: cargo check passed, game built and ran, capture completed." -ForegroundColor Green
exit 0
