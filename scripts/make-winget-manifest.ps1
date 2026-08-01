<#
.SYNOPSIS
    Generates the winget manifests for a published release.

.DESCRIPTION
    winget needs three YAML files per version, and one of them carries the
    installer's SHA-256. That hash is read from the SHA256SUMS.txt published
    with the release rather than recomputed locally, so the manifest describes
    the file people will actually download -- not a local build that happens to
    have the same version number.

    The output goes to build/winget/<version>/. To submit, copy that directory
    into a fork of microsoft/winget-pkgs at:

        manifests/s/ScotteLiu/LumenPDF/<version>/

    and open a pull request. Validate first with:

        winget validate --manifest build/winget/<version>
        winget install --manifest build/winget/<version>

.PARAMETER Version
    The released version, e.g. 0.3.1. Defaults to the version in CMakeLists.txt.

.EXAMPLE
    ./scripts/make-winget-manifest.ps1
    ./scripts/make-winget-manifest.ps1 -Version 0.3.1
#>

[CmdletBinding()]
param(
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $cmake = Get-Content (Join-Path $repoRoot "CMakeLists.txt") -Raw
    if ($cmake -notmatch 'VERSION\s+(\d+\.\d+\.\d+)') {
        throw "Could not read VERSION from CMakeLists.txt"
    }
    $Version = $Matches[1]
}

$tag = "v$Version"
$installer = "LumenPDF-$Version-win64-setup.exe"
$base = "https://github.com/ScotteLiu/LumenPDF/releases/download/$tag"

Write-Host "Version   : $Version"
Write-Host "Installer : $installer"

# -- The hash, from what was actually published ------------------------------
Write-Host "Fetching SHA256SUMS.txt from the release..."
try {
    $response = Invoke-WebRequest "$base/SHA256SUMS.txt" -UseBasicParsing
} catch {
    throw "Could not fetch $base/SHA256SUMS.txt -- is $tag released and public?"
}

# Windows PowerShell hands back .Content as a Byte[] for content types it does
# not classify as text, and GitHub serves release assets as octet-stream. Split
# that without decoding and you are splitting an array of numbers.
$sums = if ($response.Content -is [byte[]]) {
    [System.Text.Encoding]::UTF8.GetString($response.Content)
} else {
    $response.Content
}

$line = $sums -split "`n" | Where-Object { $_ -match [regex]::Escape($installer) } | Select-Object -First 1
if (-not $line) { throw "No checksum for $installer in the published SHA256SUMS.txt" }

$sha256 = ($line -split '\s+')[0].ToUpper()
Write-Host "SHA-256   : $sha256"

# Inno Setup registers itself in Add/Remove Programs under "<AppId>_is1".
# winget uses this to detect an existing install and to drive upgrades.
$productCode = "{7A4E1C62-9B3D-4F1A-8C2E-5D9F0A3B6E41}_is1"

$outDir = Join-Path $repoRoot "build\winget\$Version"
Remove-Item $outDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# UTF-8 without a BOM: the winget validation pipeline rejects a BOM.
$utf8 = New-Object System.Text.UTF8Encoding $false
function Write-Manifest([string]$name, [string]$content) {
    [System.IO.File]::WriteAllText((Join-Path $outDir $name), $content, $utf8)
    Write-Host "  $name"
}

Write-Host ""
Write-Host "--- manifests ---" -ForegroundColor Cyan

Write-Manifest "ScotteLiu.LumenPDF.yaml" @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.1.6.0.schema.json
PackageIdentifier: ScotteLiu.LumenPDF
PackageVersion: $Version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.6.0
"@

Write-Manifest "ScotteLiu.LumenPDF.installer.yaml" @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.1.6.0.schema.json
PackageIdentifier: ScotteLiu.LumenPDF
PackageVersion: $Version
InstallerType: inno
Scope: user
InstallModes:
  - interactive
  - silent
  - silentWithProgress
UpgradeBehavior: install
ProductCode: '$productCode'
ReleaseDate: $(Get-Date -Format 'yyyy-MM-dd')
Installers:
  - Architecture: x64
    InstallerUrl: $base/$installer
    InstallerSha256: $sha256
ManifestType: installer
ManifestVersion: 1.6.0
"@

Write-Manifest "ScotteLiu.LumenPDF.locale.en-US.yaml" @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.defaultLocale.1.6.0.schema.json
PackageIdentifier: ScotteLiu.LumenPDF
PackageVersion: $Version
PackageLocale: en-US
Publisher: Scotte Liu
PublisherUrl: https://github.com/ScotteLiu
PublisherSupportUrl: https://github.com/ScotteLiu/LumenPDF/issues
PackageName: LumenPDF
PackageUrl: https://github.com/ScotteLiu/LumenPDF
License: Apache-2.0
LicenseUrl: https://github.com/ScotteLiu/LumenPDF/blob/main/LICENSE
Copyright: Copyright 2026 Scotte Liu
CopyrightUrl: https://github.com/ScotteLiu/LumenPDF/blob/main/NOTICE
ShortDescription: A fast, elegant PDF viewer and editor
Description: |-
  A PDF viewer and editor built for speed, restraint and craft. Opening a
  1000-page document costs the same as opening a 3-page one, because page
  geometry is cached once at load and rendering is fully virtualised on a
  worker pool that the GUI thread never waits on.

  Reading, full-text search, thumbnails and outline. Text selection that
  follows the real reading order, across columns and across pages. Printing
  with page ranges and print-to-PDF. Highlight, underline and strike-through
  written as standards-conformant PDF markup. Page rotate, reorder, delete,
  merge and split, all undoable. AcroForm filling. Export to images or text.

  Redaction destroys the text rather than covering it: the affected page is
  rasterised and its objects replaced, so nothing remains underneath to
  recover. That costs the page its selectable text, and the confirmation says
  so.

  OCR writes an invisible text layer under a scanned page using Windows' own
  recogniser, so the scan looks identical but becomes searchable -- offline,
  with no extra download.

  Text extraction is regression-tested across ten writing systems, because it
  fails differently in each: right-to-left order, stacked tone marks, Indic
  clusters, scripts written without spaces.

  Interface available in English, Traditional Chinese, Simplified Chinese and
  Japanese.
Moniker: lumenpdf
Tags:
  - pdf
  - pdf-editor
  - pdf-viewer
  - ocr
  - redaction
  - annotation
  - qt
ReleaseNotesUrl: https://github.com/ScotteLiu/LumenPDF/releases/tag/$tag
Documentations:
  - DocumentLabel: Engineering log
    DocumentUrl: https://github.com/ScotteLiu/LumenPDF/blob/main/docs/DEVLOG.md
ManifestType: defaultLocale
ManifestVersion: 1.6.0
"@

Write-Host ""
Write-Host "Wrote $outDir" -ForegroundColor Green
Write-Host ""
Write-Host "Next:" -ForegroundColor Cyan
Write-Host "  winget validate --manifest `"$outDir`""
Write-Host "  winget install  --manifest `"$outDir`"   # verify it really installs"
Write-Host ""
Write-Host "Then copy into a fork of microsoft/winget-pkgs at"
Write-Host "  manifests/s/ScotteLiu/LumenPDF/$Version/"
Write-Host "and open a pull request."
