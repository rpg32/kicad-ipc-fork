<#
.SYNOPSIS
    Install the experimental KiCad IPC-fork binaries over a stock KiCad 10 install.

.DESCRIPTION
    Backs up the original binaries (once) and copies the patched matched set in.
    Run from an elevated PowerShell (the target lives under Program Files).

    EXPERIMENTAL / AGENT-GENERATED: these binaries are an unvetted fork of KiCad.
    See README.md. Use a non-critical machine or VM if that matters to you.

.PARAMETER KiCadDir
    KiCad bin directory. Defaults to C:\Program Files\KiCad\10.0\bin.

.PARAMETER Force
    Proceed even if the installed KiCad version string does not match the
    fork's base version (ABI compatibility is then your risk).

.EXAMPLE
    ./install.ps1
.EXAMPLE
    ./install.ps1 -KiCadDir "D:\KiCad\10.0\bin" -Force
#>
[CmdletBinding()]
param(
    [string]$KiCadDir = "C:\Program Files\KiCad\10.0\bin",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$binDir = Join-Path $scriptDir "bin"
$manifestPath = Join-Path $scriptDir "manifest.json"

Write-Host "=== KiCad IPC-fork installer ===" -ForegroundColor Cyan
Write-Host "  EXPERIMENTAL agent-generated build — treat as unvetted.`n" -ForegroundColor Yellow

if (-not (Test-Path $manifestPath)) { throw "manifest.json not found next to this script." }
if (-not (Test-Path $binDir)) { throw "bin/ folder not found next to this script. Did you extract the full release zip?" }
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json

# Resolve and validate the target install.
if (-not (Test-Path $KiCadDir)) { throw "KiCad bin dir not found: $KiCadDir  (pass -KiCadDir)" }
$eeschema = Join-Path $KiCadDir "_eeschema.dll"
if (-not (Test-Path $eeschema)) { throw "$KiCadDir does not look like a KiCad bin dir (no _eeschema.dll)." }

# Version check (soft — KiCad reports the same string across ABI-incompatible builds).
$cli = Join-Path $KiCadDir "kicad-cli.exe"
if (Test-Path $cli) {
    $ver = (& $cli version 2>$null | Select-Object -First 1)
    if ($ver -and $ver.Trim() -ne $manifest.baseVersionString) {
        Write-Host "WARNING: installed KiCad version '$ver' != fork base '$($manifest.baseVersionString)'." -ForegroundColor Yellow
        Write-Host "         These binaries are only ABI-safe against the matching build." -ForegroundColor Yellow
        if (-not $Force) { throw "Version mismatch. Re-run with -Force to override at your own risk." }
        Write-Host "         -Force given; proceeding anyway.`n" -ForegroundColor Yellow
    }
}

# Refuse to clobber running editors.
foreach ($proc in @("eeschema", "pcbnew", "kicad")) {
    if (Get-Process -Name $proc -ErrorAction SilentlyContinue) {
        throw "$proc is running. Close all KiCad windows first."
    }
}

# Files to install = basenames of the manifest entries, sourced from bin/.
$names = $manifest.binaries | ForEach-Object { Split-Path $_ -Leaf }
foreach ($n in $names) {
    if (-not (Test-Path (Join-Path $binDir $n))) { throw "Release is missing binary: bin/$n" }
}

$installed = @()
foreach ($n in $names) {
    $src = Join-Path $binDir $n
    $dst = Join-Path $KiCadDir $n
    $bak = "$dst.original"
    # Back up the genuine original exactly once; never overwrite an existing backup
    # (re-installing must not turn a patched DLL into the "original").
    if ((Test-Path $dst) -and -not (Test-Path $bak)) {
        Copy-Item $dst $bak -Force
    }
    Copy-Item $src $dst -Force
    $installed += $n
    Write-Host "  installed $n" -ForegroundColor Green
}

# Drop a marker so uninstall and future installs know what's here.
$marker = @{
    forkCommit  = $manifest.forkCommit
    installedAt = (Get-Date).ToString("o")
    files       = $installed
} | ConvertTo-Json
Set-Content -Path (Join-Path $KiCadDir ".live-view-patch.json") -Value $marker -Encoding UTF8

Write-Host "`nDone. $($installed.Count) binaries installed; originals saved as *.original." -ForegroundColor Cyan
Write-Host "Run uninstall.ps1 to restore. Restart KiCad before using the IPC tools." -ForegroundColor Cyan
