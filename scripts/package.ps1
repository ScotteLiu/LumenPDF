<#
.SYNOPSIS
    Builds LumenPDF and produces a portable zip and a Windows installer.

.DESCRIPTION
    Three steps:

      1. Release build.
      2. Stage a self-contained directory: the exe, the Qt runtime as deployed
         by windeployqt, pdfium.dll, and the third-party licence texts. Build
         artefacts (.pdb, .ilk, .lib, CMake scratch) are deliberately excluded --
         staging is what makes the output reproducible and small.
      3. Zip the stage, and compile the Inno Setup script against it.

    The installer step is skipped with a warning if Inno Setup is not present,
    so this still produces something useful on a machine without it.

.PARAMETER SkipBuild
    Package whatever is already built. Useful when iterating on packaging.

.EXAMPLE
    ./scripts/package.ps1
#>

[CmdletBinding()]
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build\windows-release"
$stageDir = Join-Path $repoRoot "build\stage"
$distDir  = Join-Path $repoRoot "build\dist"

# Version comes from CMakeLists so there is exactly one place to bump it.
$cmakeText = Get-Content (Join-Path $repoRoot "CMakeLists.txt") -Raw
if ($cmakeText -match '(?ms)project\(LumenPDF.*?VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    $version = $Matches[1]
} else {
    throw "Could not read the project version out of CMakeLists.txt"
}
Write-Host "Version   : $version"

# -- 1. Build ---------------------------------------------------------------
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1")
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

$exe = Join-Path $buildDir "lumenpdf.exe"
if (-not (Test-Path $exe)) { throw "Not built: $exe" }

# -- 2. Stage ---------------------------------------------------------------
Write-Host ""
Write-Host "--- stage ---" -ForegroundColor Cyan

if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }

# Copy the whole build output, then prune. Copy-then-prune rather than an
# allow-list because windeployqt decides what Qt needs, and second-guessing it
# by hand is how you ship an app that dies on someone else's machine.
Copy-Item $buildDir $stageDir -Recurse -Force

# Build scratch, at any depth. The qml/ tree in the build directory holds both
# deployed Qt modules and CMake's own working files for our modules.
$scratchDirs = @("CMakeFiles", ".qt", ".rcc", "lumenpdf_autogen", "Lumen_autogen",
                 "App_autogen", "Lumenplugin_autogen", "Appplugin_autogen")
foreach ($name in $scratchDirs) {
    Get-ChildItem $stageDir -Directory -Recurse -Filter $name -ErrorAction SilentlyContinue |
        Sort-Object { $_.FullName.Length } -Descending |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
}

$scratchFiles = @("*.pdb", "*.ilk", "*.exp", "*.lib", "*.manifest", "*.obj",
                  "CMakeCache.txt", "cmake_install.cmake", "build.ninja",
                  ".ninja_deps", ".ninja_log", "compile_commands.json",
                  "*.qmltypes.aotstats", "*.aotstats")
foreach ($pattern in $scratchFiles) {
    Get-ChildItem $stageDir -File -Recurse -Filter $pattern -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

# Our own QML modules are STATIC -- compiled into the executable. What sits in
# build/qml/{Lumen,App} is CMake's copy of the sources, and shipping it both
# bloats the package and lets a stale .qml on disk shadow the compiled one.
foreach ($ours in @("Lumen", "App")) {
    $path = Join-Path $stageDir "qml\$ours"
    if (Test-Path $path) { Remove-Item $path -Recurse -Force }
}

# Runtime pieces this build genuinely cannot use:
#
#   opengl32sw.dll  software OpenGL fallback (~20 MB). main.cpp pins the
#                   scene graph to Direct3D 11, so it is never loaded.
#   dxcompiler.dll  and dxil.dll -- the DXIL shader compiler, used only by the
#   dxil.dll        Direct3D 12 backend (~14 MB).
#
# Both are re-checked by the smoke test below rather than taken on trust.
foreach ($unused in @("opengl32sw.dll", "dxcompiler.dll", "dxil.dll")) {
    $path = Join-Path $stageDir $unused
    if (Test-Path $path) { Remove-Item $path -Force }
}

# Qt Quick Controls ships six styles. LumenPDF sets the Basic style explicitly
# and draws every control itself, so the rest are dead weight.
$unusedStyles = @("Material", "Fusion", "Universal", "Imagine",
                  "FluentWinUI3", "Windows", "macOS", "iOS")
foreach ($style in $unusedStyles) {
    Get-ChildItem $stageDir -File -Filter "Qt6QuickControls2$style*.dll" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    $qmlPath = Join-Path $stageDir "qml\QtQuick\Controls\$style"
    if (Test-Path $qmlPath) { Remove-Item $qmlPath -Recurse -Force }
}

if (-not (Test-Path (Join-Path $stageDir "pdfium.dll"))) {
    throw "pdfium.dll is missing from the stage -- was the build configured with PDFium?"
}
if (-not (Test-Path (Join-Path $stageDir "Qt6Core.dll"))) {
    throw "The Qt runtime is missing from the stage -- windeployqt did not run"
}

# Third-party licence texts. Shipping these is an obligation of using Qt under
# LGPLv3 and PDFium under BSD, not a nicety.
$licenseDir = Join-Path $stageDir "LICENSES"
New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null

$ownLicense = Join-Path $repoRoot "LICENSE"
if (Test-Path $ownLicense) {
    Copy-Item $ownLicense (Join-Path $stageDir "LICENSE.txt") -Force
}

$pdfiumLicense = Join-Path $repoRoot "third_party\pdfium\LICENSE"
if (Test-Path $pdfiumLicense) {
    Copy-Item $pdfiumLicense (Join-Path $licenseDir "PDFium-LICENSE.txt") -Force
}

@"
LumenPDF bundles the following third-party components.

Qt $((Get-Item (Join-Path $stageDir 'Qt6Core.dll')).VersionInfo.FileVersion)
    Licensed under the GNU Lesser General Public License version 3.
    Qt is used as dynamically linked shared libraries and is not statically
    linked into LumenPDF. The Qt DLLs shipped alongside the executable may be
    replaced with compatible versions.
    Full text: https://www.gnu.org/licenses/lgpl-3.0.html
    Source:    https://download.qt.io/official_releases/qt/

PDFium
    Licensed under the BSD 3-Clause License. See PDFium-LICENSE.txt.
    Source: https://pdfium.googlesource.com/pdfium/
"@ | Set-Content (Join-Path $licenseDir "THIRD-PARTY-NOTICES.txt") -Encoding utf8

$stageSize = [math]::Round((Get-ChildItem $stageDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1)
$stageCount = (Get-ChildItem $stageDir -Recurse -File | Measure-Object).Count
Write-Host "Staged $stageCount files, $stageSize MB"

# -- 2b. Smoke test the staged build ---------------------------------------
#
# Pruning is the step most likely to produce a package that builds cleanly and
# then fails on a user's machine, so the staged copy has to prove it still
# renders a page before anything is shipped. Nothing about this test touches
# the build tree.
Write-Host ""
Write-Host "--- smoke test ---" -ForegroundColor Cyan

$smokePdf = Get-ChildItem (Join-Path $repoRoot "work") -Filter "*.pdf" -Recurse -File -ErrorAction SilentlyContinue |
    Select-Object -First 1

if (-not $smokePdf) {
    Write-Warning "No PDF under work/ to smoke test with -- skipping. The package is UNVERIFIED."
} else {
    $shot = Join-Path $stageDir "smoke.png"
    $log  = Join-Path $stageDir "smoke.log"

    $env:LUMEN_CAPTURE = $shot
    $env:LUMEN_CAPTURE_DELAY = "4000"
    $env:QT_FORCE_STDERR_LOGGING = "1"
    foreach ($leftover in @("LUMEN_SELECT", "LUMEN_ANNOTATE", "LUMEN_SAVE_AS",
                            "LUMEN_PAGEOP", "LUMEN_SEARCH", "LUMEN_THEME")) {
        Remove-Item "Env:$leftover" -ErrorAction SilentlyContinue
    }

    $proc = Start-Process (Join-Path $stageDir "lumenpdf.exe") `
        -ArgumentList $smokePdf.FullName -Wait -PassThru -RedirectStandardError $log

    $captured = (Test-Path $shot) -and ((Get-Item $shot).Length -gt 10000)
    $errors = if (Test-Path $log) {
        Get-Content $log | Where-Object { $_ -match "error|failed|unavailable|Unable to" -and $_ -notmatch "qt.qpa.fonts" }
    }

    Remove-Item $shot, $log -Force -ErrorAction SilentlyContinue

    if ($proc.ExitCode -ne 0 -or -not $captured) {
        throw "Staged build failed to render (exit $($proc.ExitCode), captured=$captured). Pruning removed something it needs."
    }
    if ($errors) {
        Write-Warning "Staged build logged problems:"
        $errors | ForEach-Object { Write-Warning "  $_" }
    }

    Write-Host "Staged build rendered $($smokePdf.Name) successfully"
}

# -- 3. Code signing --------------------------------------------------------
#
# Optional, and absent by default. Without a signature Windows SmartScreen
# warns on first run, which is the single largest reason people abandon an
# install -- but a code-signing certificate is bought by an identified legal
# entity and cannot be generated here.
#
# Provide one and this signs automatically:
#   LUMEN_SIGN_PFX       path to a .pfx
#   LUMEN_SIGN_PASSWORD  its password
# or, for a certificate already in the Windows store:
#   LUMEN_SIGN_THUMBPRINT
#
# See docs/CODE-SIGNING.md.
function Find-SignTool {
    $candidates = Get-ChildItem `
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" `
        -ErrorAction SilentlyContinue | Sort-Object FullName -Descending
    if ($candidates) { return $candidates[0].FullName }
    $onPath = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    return $null
}

$script:signTool = $null
$script:signArgs = @()

if ($env:LUMEN_SIGN_PFX -or $env:LUMEN_SIGN_THUMBPRINT) {
    $script:signTool = Find-SignTool
    if (-not $script:signTool) {
        throw "Signing was requested but signtool.exe was not found. Install the Windows SDK."
    }

    # RFC 3161 timestamping, so the signature stays valid after the
    # certificate expires. Without it every build stops verifying the day the
    # certificate lapses.
    $script:signArgs = @("sign", "/fd", "SHA256",
                         "/tr", "http://timestamp.digicert.com", "/td", "SHA256")

    if ($env:LUMEN_SIGN_THUMBPRINT) {
        $script:signArgs += @("/sha1", $env:LUMEN_SIGN_THUMBPRINT)
    } else {
        if (-not (Test-Path $env:LUMEN_SIGN_PFX)) {
            throw "LUMEN_SIGN_PFX does not exist: $($env:LUMEN_SIGN_PFX)"
        }
        $script:signArgs += @("/f", $env:LUMEN_SIGN_PFX)
        if ($env:LUMEN_SIGN_PASSWORD) {
            $script:signArgs += @("/p", $env:LUMEN_SIGN_PASSWORD)
        }
    }
}

function Invoke-Sign([string]$Path) {
    if (-not $script:signTool) { return }

    & $script:signTool @script:signArgs $Path | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $Path" }

    # Verified rather than trusted: signtool reports success for a signature
    # that will not actually satisfy Windows.
    & $script:signTool verify /pa /q $Path
    if ($LASTEXITCODE -ne 0) { throw "the signature on $Path does not verify" }

    Write-Host "Signed    : $(Split-Path $Path -Leaf)" -ForegroundColor Green
}

if ($script:signTool) {
    Write-Host ""
    Write-Host "--- signing ---" -ForegroundColor Cyan
    # The executable is signed before it is zipped, so the portable build
    # carries the signature too.
    Invoke-Sign (Join-Path $stageDir "lumenpdf.exe")
} else {
    Write-Host ""
    Write-Warning "Not signed. Windows SmartScreen will warn on first run. See docs/CODE-SIGNING.md."
}

# -- 4. Zip and installer ---------------------------------------------------
#
# Emptied first. A previous version's artefacts left sitting here end up in
# SHA256SUMS.txt and then in the release, which is how a "0.3.0" release comes
# to contain a 0.1.0 installer.
Remove-Item $distDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $distDir | Out-Null

$zip = Join-Path $distDir "LumenPDF-$version-win64-portable.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }

Write-Host ""
Write-Host "--- zip ---" -ForegroundColor Cyan
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Portable  : $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"

# winget installs Inno Setup per-user by default, which is neither of the
# Program Files locations most scripts check.
$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

# Missing Inno Setup skips the installer but must not skip the checksums --
# returning early here once meant the portable zip shipped unverifiable.
if (-not $iscc) {
    Write-Warning "Inno Setup not found -- skipping the installer. Install it with: winget install JRSoftware.InnoSetup"
} else {
    Write-Host ""
    Write-Host "--- installer ---" -ForegroundColor Cyan
    & $iscc `
        "/DStageDir=$stageDir" `
        "/DAppVersion=$version" `
        (Join-Path $repoRoot "packaging\lumenpdf.iss") | Select-Object -Last 6
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

    $setup = Join-Path $distDir "LumenPDF-$version-win64-setup.exe"
    if (Test-Path $setup) {
        Invoke-Sign $setup
        Write-Host ""
        Write-Host "Installer : $setup ($([math]::Round((Get-Item $setup).Length / 1MB, 1)) MB)" -ForegroundColor Green
    }
}

# -- 5. Checksums -----------------------------------------------------------
#
# Published with every release, and not optional: the in-app updater refuses to
# download anything it cannot verify against this file. Format matches
# sha256sum, so `sha256sum -c SHA256SUMS.txt` works.
Write-Host ""
Write-Host "--- checksums ---" -ForegroundColor Cyan

$sumsPath = Join-Path $distDir "SHA256SUMS.txt"
$lines = foreach ($artefact in (Get-ChildItem $distDir -File |
                                Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
                                Sort-Object Name)) {
    $hash = (Get-FileHash $artefact.FullName -Algorithm SHA256).Hash.ToLower()
    Write-Host "  $hash  $($artefact.Name)"
    "$hash  $($artefact.Name)"
}

# ASCII with LF endings, so the file reads identically on every platform.
[System.IO.File]::WriteAllText($sumsPath, ($lines -join "`n") + "`n",
                               (New-Object System.Text.UTF8Encoding $false))
Write-Host "Checksums : $sumsPath" -ForegroundColor Green
