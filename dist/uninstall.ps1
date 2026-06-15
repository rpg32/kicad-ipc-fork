<#
.SYNOPSIS
    Restore the original KiCad binaries that install.ps1 replaced.

.DESCRIPTION
    Restores every <file>.original backup and removes the patch marker.
    Run from an elevated PowerShell. Close all KiCad windows first.

.PARAMETER KiCadDir
    KiCad bin directory. Defaults to C:\Program Files\KiCad\10.0\bin.
#>
[CmdletBinding()]
param(
    [string]$KiCadDir = "C:\Program Files\KiCad\10.0\bin"
)

$ErrorActionPreference = "Stop"
Write-Host "=== KiCad IPC-fork uninstaller ===" -ForegroundColor Cyan

if (-not (Test-Path $KiCadDir)) { throw "KiCad bin dir not found: $KiCadDir  (pass -KiCadDir)" }

foreach ($proc in @("eeschema", "pcbnew", "kicad")) {
    if (Get-Process -Name $proc -ErrorAction SilentlyContinue) {
        throw "$proc is running. Close all KiCad windows first."
    }
}

$markerPath = Join-Path $KiCadDir ".live-view-patch.json"
$files = $null
if (Test-Path $markerPath) {
    $files = (Get-Content $markerPath -Raw | ConvertFrom-Json).files
}
# Fall back to whatever *.original backups exist if the marker is gone.
if (-not $files) {
    $files = Get-ChildItem $KiCadDir -Filter "*.original" |
        ForEach-Object { $_.Name -replace '\.original$', '' }
}

if (-not $files) {
    Write-Host "Nothing to restore (no marker and no *.original backups found)." -ForegroundColor Yellow
    return
}

$restored = 0
foreach ($n in $files) {
    $dst = Join-Path $KiCadDir $n
    $bak = "$dst.original"
    if (Test-Path $bak) {
        Copy-Item $bak $dst -Force
        Remove-Item $bak -Force
        $restored++
        Write-Host "  restored $n" -ForegroundColor Green
    } else {
        Write-Host "  no backup for $n (left as-is)" -ForegroundColor Yellow
    }
}

if (Test-Path $markerPath) { Remove-Item $markerPath -Force }
Write-Host "`nRestored $restored binaries. Restart KiCad." -ForegroundColor Cyan
