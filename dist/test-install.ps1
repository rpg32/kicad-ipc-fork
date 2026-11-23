<#
.SYNOPSIS
    Make a private, unelevated test copy of KiCad with the fork's built binaries.

.DESCRIPTION
    Installing the fork needs elevation and every KiCad closed, which interrupts
    everyone on the machine. This copies the installed KiCad's bin, etc and lib to
    a user folder, links its share folder, and overlays the binaries of this build
    (the same set manifest.json ships). The installed KiCad is not touched.

    Run the live-view suites against the copy before releasing:

        $env:LIVEVIEW_KICAD_BIN = "<TestDir>\bin"
        node mcp/test-kicad-regression.mjs ; node mcp/bench-e2e.mjs

.PARAMETER TestDir
    Where to put the copy. Default: %LOCALAPPDATA%\live-view\kicad-fork-test

.PARAMETER KiCadDir
    The installed KiCad. Default: C:\Program Files\KiCad\10.0

.PARAMETER BuildDir
    CMake build output dir. Default: ..\build\msvc-win64-release (relative to dist/).

.PARAMETER Remove
    Delete the test copy (its share link first, so the installed share is never touched).
#>
[CmdletBinding()]
param(
    [string]$TestDir = (Join-Path $env:LOCALAPPDATA "live-view\kicad-fork-test"),
    [string]$KiCadDir = "C:\Program Files\KiCad\10.0",
    [string]$BuildDir,
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $BuildDir) { $BuildDir = Join-Path (Split-Path -Parent $scriptDir) "build\msvc-win64-release" }

function Remove-TestCopy {
    if (-not (Test-Path $TestDir)) { return }
    # Links first, as links: a recursive delete must never follow one into the installed KiCad.
    Get-ChildItem $TestDir -Force | Where-Object { $_.LinkType } | ForEach-Object { cmd /c rmdir "$($_.FullName)" }
    if (Get-ChildItem $TestDir -Force -Recurse | Where-Object { $_.LinkType }) {
        throw "Links remain under $TestDir; not deleting it."
    }
    Remove-Item $TestDir -Recurse -Force
}

if ($Remove) {
    Remove-TestCopy
    Write-Host "Removed $TestDir"
    return
}

foreach ($name in @("eeschema", "pcbnew", "kicad", "kicad-cli")) {
    Get-Process -Name $name -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$TestDir*" } | ForEach-Object {
        throw "$($_.ProcessName) (pid $($_.Id)) is running from the test copy. Close it first."
    }
}

if (-not (Test-Path (Join-Path $KiCadDir "bin\_eeschema.dll"))) { throw "$KiCadDir does not look like a KiCad install." }
if (-not (Test-Path $BuildDir)) { throw "Build dir not found: $BuildDir (build the fork first)" }

Remove-TestCopy
New-Item -ItemType Directory -Path $TestDir -Force | Out-Null
foreach ($d in @("bin", "etc", "lib")) {
    $src = Join-Path $KiCadDir $d
    if (Test-Path $src) { Copy-Item $src (Join-Path $TestDir $d) -Recurse -Force }
}
cmd /c mklink /J "$(Join-Path $TestDir 'share')" "$(Join-Path $KiCadDir 'share')" | Out-Null

$manifest = Get-Content (Join-Path $scriptDir "manifest.json") -Raw | ConvertFrom-Json
$bin = Join-Path $TestDir "bin"
$count = 0
foreach ($entry in $manifest.binaries) {
    if ($entry -is [string]) { $rel = $entry; $dest = Split-Path $entry -Leaf }
    else                     { $rel = $entry.src; $dest = $entry.dest }
    $src = Join-Path $BuildDir $rel
    if (-not (Test-Path $src)) { throw "Missing built binary: $src" }
    $out = Join-Path $bin $dest
    New-Item -ItemType Directory -Path (Split-Path $out -Parent) -Force | Out-Null
    Copy-Item $src $out -Force
    $count++
}

Write-Host "Test copy ready: $TestDir ($count fork binaries over a copy of $KiCadDir)"
Write-Host "Use it with:  `$env:LIVEVIEW_KICAD_BIN = `"$bin`""
