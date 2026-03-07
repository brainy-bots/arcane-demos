# Remove build artifacts so the next open/build does a full rebuild.
# Run from this folder (Unreal/ArcaneDemo) or from repo root.
# Then open ArcaneDemo.uproject and choose "Yes" to rebuild when prompted.

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
if (-not (Test-Path "$Root\ArcaneDemo.uproject")) {
    $Root = ".\Unreal\ArcaneDemo"
    if (-not (Test-Path "$Root\ArcaneDemo.uproject")) {
        Write-Error "Run from Unreal/ArcaneDemo or repo root."
    }
}

$removed = @()
foreach ($dir in @("Binaries", "Intermediate", "Plugins\ArcaneClient\Binaries", "Plugins\ArcaneClient\Intermediate")) {
    $path = Join-Path $Root $dir
    if (Test-Path $path) {
        Remove-Item -Recurse -Force $path
        $removed += $dir
    }
}
foreach ($f in @("*.sln", "*.vcxproj", "*.vcxproj.filters")) {
    Get-ChildItem -Path $Root -Filter $f -File -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path $Root -Filter $f -File -ErrorAction SilentlyContinue | ForEach-Object { $removed += $_.Name }
}

if ($removed.Count -gt 0) {
    Write-Host "Removed: $($removed -join ', ')"
} else {
    Write-Host "Nothing to remove (already clean)."
}
Write-Host "Open ArcaneDemo.uproject and rebuild when prompted."
