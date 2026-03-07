# One-time: copy Characters/Mannequins into Arcane Demo so the playable character has a skeletal mesh.
# From Unreal/ArcaneDemo: .\Scripts\Copy-CharacterFromTemplate.ps1 -SourceProject "C:\Path\To\ThirdPersonProject"

param(
    [Parameter(Mandatory = $false)]
    [string] $SourceProject = "",
    [Parameter(Mandatory = $false)]
    [string] $ProjectDir = ""
)

$ErrorActionPreference = "Stop"

if ($ProjectDir) {
    $ArcaneContent = Join-Path $ProjectDir "Content"
} else {
    $ScriptDir = Split-Path -Parent $PSScriptRoot
    $ArcaneContent = Join-Path $ScriptDir "Content"
}

$TargetCharacters = Join-Path $ArcaneContent "Characters"
$RequiredSubpath = "Characters\Mannequins"

function Test-CharactersContent {
    param([string]$Root)
    $chars = Join-Path $Root "Content"
    $chars = Join-Path $chars "Characters"
    if (-not (Test-Path $chars)) { return $false }
    $mannequins = Join-Path $chars "Mannequins"
    return (Test-Path $mannequins)
}

function Copy-Characters {
    param([string]$SourceProjectRoot)
    $SourceContent = Join-Path $SourceProjectRoot "Content"
    $SourceChars = Join-Path $SourceContent "Characters"
    if (-not (Test-Path $SourceChars)) {
        Write-Host "Source has no Content\Characters at: $SourceChars"
        return $false
    }
    if (Test-Path $TargetCharacters) {
        Write-Host "Removing existing Content\Characters: $TargetCharacters"
        Remove-Item -Recurse -Force $TargetCharacters
    }
    New-Item -ItemType Directory -Path (Split-Path $TargetCharacters) -Force | Out-Null
    Copy-Item -Path $SourceChars -Destination $TargetCharacters -Recurse -Force
    Write-Host "Copied Characters to: $TargetCharacters"
    $mannequins = Join-Path $TargetCharacters "Mannequins"
    if (Test-Path $mannequins) {
        Write-Host "Mannequins folder present. You can open Arcane Demo and use the character mesh."
        return $true
    }
    Write-Host "Warning: Mannequins subfolder not found. Ensure the source project has Content\Characters\Mannequins."
    return $true
}

# 1) Explicit source project
if ($SourceProject -ne "") {
    $SourceProject = $SourceProject.Trim().TrimEnd('\')
    if (-not (Test-Path $SourceProject)) {
        Write-Host "Source project path not found: $SourceProject"
        exit 1
    }
    $sourceContent = Join-Path $SourceProject "Content"
    if (-not (Test-Path $sourceContent)) {
        Write-Host "Source project has no Content folder: $sourceContent"
        exit 1
    }
    if (Copy-Characters -SourceProjectRoot $SourceProject) { exit 0 }
    exit 1
}

# 2) Try engine Templates (version from ArcaneDemo.uproject)
$ArcaneDir = Split-Path -Parent $ArcaneContent
$UprojectPath = Join-Path $ArcaneDir "ArcaneDemo.uproject"
if (-not (Test-Path $UprojectPath)) {
    Write-Host "ArcaneDemo.uproject not found; run from Unreal/ArcaneDemo or set -ProjectDir."
    exit 1
}
$json = Get-Content $UprojectPath -Raw | ConvertFrom-Json
$ver = $json.EngineAssociation
if (-not $ver) { $ver = "5.7" }

$engineCandidates = @(
    "E:\UE_$ver",
    "C:\Program Files\Epic Games\UE_$ver",
    "D:\Program Files\Epic Games\UE_$ver",
    "E:\Program Files\Epic Games\UE_$ver",
    "$env:ProgramFiles\Epic Games\UE_$ver"
)

foreach ($engineRoot in $engineCandidates) {
    if (-not (Test-Path $engineRoot)) { continue }
    $templatesDir = Join-Path $engineRoot "Templates"
    if (-not (Test-Path $templatesDir)) { continue }
    $templateDirs = @("TP_ThirdPerson", "TP_ThirdPersonBP", "ThirdPerson", "TPS")
    foreach ($name in $templateDirs) {
        $templatePath = Join-Path $templatesDir $name
        if (Test-Path $templatePath) {
            $templateContent = Join-Path $templatePath "Content"
            if (Test-Path $templateContent) {
                if (Test-CharactersContent -Root $templatePath) {
                    Write-Host "Using engine template: $templatePath"
                    if (Copy-Characters -SourceProjectRoot $templatePath) { exit 0 }
                }
            }
        }
    }
}

# 3) Sibling folder named ThirdPerson or similar
$parentDir = Split-Path -Parent $ArcaneDir
$siblingNames = @("ThirdPerson", "ThirdPersonBP", "TP_ThirdPerson", "TPS")
foreach ($name in $siblingNames) {
    $sibling = Join-Path $parentDir $name
    $uproject = Get-ChildItem -Path $sibling -Filter "*.uproject" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($uproject -and (Test-CharactersContent -Root $sibling)) {
        Write-Host "Using sibling project: $sibling"
        if (Copy-Characters -SourceProjectRoot $sibling) { exit 0 }
    }
}

Write-Host ""
Write-Host "No character content source found. Do one of the following:"
Write-Host ""
Write-Host "1) Create a Third Person project (Epic Launcher -> New Project -> Games -> Third Person),"
Write-Host "   then run this script with that project path:"
Write-Host "   .\Scripts\Copy-CharacterFromTemplate.ps1 -SourceProject ""C:\Path\To\YourThirdPersonProject"""
Write-Host ""
Write-Host "2) Or migrate in the editor: open the Third Person project, right-click Content/Characters"
Write-Host "   -> Asset Actions -> Migrate -> select: $ArcaneContent"
Write-Host ""
exit 1
