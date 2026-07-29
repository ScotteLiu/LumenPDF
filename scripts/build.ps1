<#
.SYNOPSIS
    Configures and builds LumenPDF.

.DESCRIPTION
    The Ninja generator needs the MSVC environment (cl.exe, the Windows SDK
    headers, the linker) already on PATH -- unlike the Visual Studio generator,
    it will not find them on its own. This script locates vcvars64.bat via
    vswhere, imports the environment it sets, points CMake at Qt, and builds.

.PARAMETER Config
    "release" (default) or "debug" -- matches the CMake presets.

.PARAMETER Fresh
    Delete the build directory before configuring.

.EXAMPLE
    ./scripts/build.ps1
    ./scripts/build.ps1 -Config debug -Fresh
#>

[CmdletBinding()]
param(
    [ValidateSet("release", "debug")]
    [string]$Config = "release",

    [switch]$Fresh
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$preset = "windows-$Config"

# -- Qt ---------------------------------------------------------------------
$qtRoot = $env:LUMEN_QT_DIR
if (-not $qtRoot) {
    $candidates = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^6\.\d+\.\d+$' } |
        Sort-Object Name -Descending
    foreach ($c in $candidates) {
        $probe = Join-Path $c.FullName "msvc2022_64"
        if (Test-Path (Join-Path $probe "lib\cmake\Qt6")) { $qtRoot = $probe; break }
    }
}
if (-not $qtRoot) {
    throw "Could not find a Qt 6 MSVC install. Set LUMEN_QT_DIR to e.g. C:\Qt\6.8.3\msvc2022_64"
}
Write-Host "Qt        : $qtRoot"

# -- MSVC environment -------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found -- is Visual Studio (or Build Tools) 2022 installed?"
}

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath) {
    throw "No Visual Studio installation with the C++ toolset was found."
}

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found at $vcvars"
}
Write-Host "Toolchain : $vsPath"

# Run vcvars in cmd, then import every variable it set back into this session.
$envDump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path "Env:\$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
    }
}

# -- Configure and build ----------------------------------------------------
Set-Location $repoRoot

$buildDir = Join-Path $repoRoot "build\$preset"
if ($Fresh -and (Test-Path $buildDir)) {
    Write-Host "Removing $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

Write-Host ""
Write-Host "--- configure ($preset) ---" -ForegroundColor Cyan
cmake --preset $preset "-DCMAKE_PREFIX_PATH=$qtRoot"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host ""
Write-Host "--- build ($preset) ---" -ForegroundColor Cyan
cmake --build --preset $preset
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host ""
Write-Host "Built: $buildDir\lumenpdf.exe" -ForegroundColor Green
