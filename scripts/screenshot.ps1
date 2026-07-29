<#
.SYNOPSIS
    Launches LumenPDF, screenshots the screen, and captures Qt/QML log output.

.DESCRIPTION
    LumenPDF is built as a WIN32 (windowed) executable, so it has no console
    and qDebug/QML warnings would normally go nowhere visible.
    QT_FORCE_STDERR_LOGGING=1 redirects them to stderr, which this script
    captures to a log file alongside the screenshot.

    Deliberately avoids Add-Type with inline C#: compiling a helper assembly
    on every run costs seconds at best and stalls outright on some machines.
    Everything here uses assemblies that are already built.

.EXAMPLE
    ./scripts/screenshot.ps1 -Pdf C:\some\file.pdf
#>

[CmdletBinding()]
param(
    [string]$Pdf = "",
    [ValidateSet("release", "debug")]
    [string]$Config = "release",
    [int]$Wait = 6,
    [switch]$KeepOpen
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "build\windows-$Config\lumenpdf.exe"
if (-not (Test-Path $exe)) { throw "Not built: $exe" }

$outDir = Join-Path $repoRoot "work\ui-check"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$stamp = Get-Date -Format "HHmmss"
$shot = Join-Path $outDir "lumenpdf-$stamp.png"
$log  = Join-Path $outDir "lumenpdf-$stamp.log"

$env:QT_FORCE_STDERR_LOGGING = "1"

$startArgs = @{
    FilePath               = $exe
    RedirectStandardError  = $log
    RedirectStandardOutput = (Join-Path $outDir "lumenpdf-$stamp.out")
    PassThru               = $true
}
if ($Pdf) { $startArgs.ArgumentList = @($Pdf) }

Write-Host "Launching $exe"
$proc = Start-Process @startArgs

Start-Sleep -Seconds $Wait

if ($proc.HasExited) {
    Write-Warning "Process exited early (code $($proc.ExitCode))"
} else {
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms

    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.X, $bounds.Y, 0, 0, $bmp.Size)
    $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Host "Screenshot: $shot"

    if (-not $KeepOpen) {
        $proc.CloseMainWindow() | Out-Null
        Start-Sleep -Milliseconds 900
        if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit(3000) | Out-Null }
    }
}

Write-Host "Log: $log"
if ((Test-Path $log) -and (Get-Item $log).Length -gt 0) {
    Write-Host "--- Qt output ---" -ForegroundColor Yellow
    Get-Content $log
} else {
    Write-Host "(no Qt warnings)" -ForegroundColor Green
}
