<#
.SYNOPSIS
    Collect the built fork binaries + install scripts into a release zip.

.DESCRIPTION
    Reads manifest.json, copies each built binary from the build tree into a
    staging bin/ folder (flattened to basenames), adds install/uninstall
    scripts, the manifest, and README, then zips it.

.PARAMETER BuildDir
    CMake build output dir. Default: ..\build\msvc-win64-release (relative to dist/).

.PARAMETER OutDir
    Where to write the zip. Default: dist/release.

.EXAMPLE
    ./package.ps1
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutDir
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot "build\msvc-win64-release" }
if (-not $OutDir)   { $OutDir   = Join-Path $scriptDir "release" }

$manifest = Get-Content (Join-Path $scriptDir "manifest.json") -Raw | ConvertFrom-Json
$tag = "kicad-ipc-fork-$($manifest.forkCommit.Substring(0,8))"
$stage = Join-Path $OutDir $tag
$stageBin = Join-Path $stage "bin"

Write-Host "=== Packaging $tag ===" -ForegroundColor Cyan
Write-Host "  build:   $BuildDir"
Write-Host "  staging: $stage`n"

if (-not (Test-Path $BuildDir)) { throw "Build dir not found: $BuildDir (build the fork first)" }
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stageBin -Force | Out-Null

foreach ($rel in $manifest.binaries) {
    $src = Join-Path $BuildDir $rel
    if (-not (Test-Path $src)) { throw "Missing built binary: $src" }
    Copy-Item $src (Join-Path $stageBin (Split-Path $rel -Leaf)) -Force
    Write-Host "  staged $(Split-Path $rel -Leaf)" -ForegroundColor Green
}

foreach ($f in @("install.ps1", "uninstall.ps1", "manifest.json", "README.md")) {
    $src = Join-Path $scriptDir $f
    if (Test-Path $src) { Copy-Item $src (Join-Path $stage $f) -Force }
}

$zip = Join-Path $OutDir "$tag.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip -Force

$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "`nWrote $zip ($sizeMb MB)" -ForegroundColor Cyan
Write-Host "Publish with:  gh release create $tag `"$zip`" --repo <owner>/kicad-ipc-fork --title $tag --notes-file dist/RELEASE_NOTES.md" -ForegroundColor Cyan
