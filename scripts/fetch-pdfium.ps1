<#
.SYNOPSIS
    Downloads prebuilt PDFium binaries into third_party/pdfium.

.DESCRIPTION
    PDFium is BSD-licensed and safe to ship in a commercial product, but
    building it from source needs depot_tools and roughly an hour. The
    bblanchon/pdfium-binaries project publishes official-toolchain builds of
    every release, which is what this script fetches.

    Nothing here is committed to git -- third_party/pdfium is ignored.

.PARAMETER Version
    Release tag to fetch, e.g. "chromium/7442". Defaults to the latest release.

.EXAMPLE
    ./scripts/fetch-pdfium.ps1
#>

[CmdletBinding()]
param(
    [string]$Version = "latest",
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$targetDir = Join-Path $repoRoot "third_party\pdfium"
$asset = "pdfium-win-$Arch.tgz"

if ($Version -eq "latest") {
    $releaseUrl = "https://api.github.com/repos/bblanchon/pdfium-binaries/releases/latest"
} else {
    $releaseUrl = "https://api.github.com/repos/bblanchon/pdfium-binaries/releases/tags/$Version"
}

Write-Host "Querying $releaseUrl"
$release = Invoke-RestMethod -Uri $releaseUrl -Headers @{ "User-Agent" = "LumenPDF" }

$downloadUrl = ($release.assets | Where-Object { $_.name -eq $asset }).browser_download_url
if (-not $downloadUrl) {
    throw "Release $($release.tag_name) has no asset named $asset"
}

Write-Host "PDFium release: $($release.tag_name)"
Write-Host "Downloading   : $downloadUrl"

$tempArchive = Join-Path $env:TEMP $asset
Invoke-WebRequest -Uri $downloadUrl -OutFile $tempArchive

if (Test-Path $targetDir) {
    Write-Host "Removing previous $targetDir"
    Remove-Item -Recurse -Force $targetDir
}
New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

# tar ships with Windows 10 1803 and later.
Write-Host "Extracting to $targetDir"
tar -xzf $tempArchive -C $targetDir
Remove-Item $tempArchive -Force

$header = Join-Path $targetDir "include\fpdfview.h"
if (-not (Test-Path $header)) {
    throw "Extraction looks wrong -- $header is missing"
}

Write-Host ""
Write-Host "PDFium $($release.tag_name) ready at $targetDir" -ForegroundColor Green
Write-Host "Re-run CMake configure to pick it up."
