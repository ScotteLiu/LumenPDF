<#
.SYNOPSIS
    Records the product-page demo by driving the real application.

.DESCRIPTION
    The app grabs its own window frame by frame while a scripted timeline
    (qml/App/DemoRunner.qml) drives it through scrolling, zooming, the outline,
    search, selection and highlighting. Nothing is staged: if a feature breaks,
    the demo breaks with it.

    Frames are grabbed synchronously after each animation step rather than on a
    wall clock, so a busy machine produces a slower recording rather than a torn
    one.

    Output is MP4 (H.264) plus a WebM and a poster image. MP4 rather than GIF:
    a GIF of this length would be roughly ten times the size at worse quality,
    and every browser that matters plays inline muted video.

.EXAMPLE
    ./scripts/record-demo.ps1
#>

[CmdletBinding()]
param(
    [string]$Pdf = "",
    [switch]$SkipBuild,
    [int]$Fps = 25
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "build\windows-release\lumenpdf.exe"
$frames = Join-Path $repoRoot "work\demo\frames"
$outDir = Join-Path $repoRoot "docs\video"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -NoDeploy | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
if (-not (Test-Path $exe)) { throw "Not built: $exe" }

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) {
    $ffmpeg = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Filter ffmpeg.exe `
        -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $ffmpeg) { throw "ffmpeg not found. winget install Gyan.FFmpeg" }

# The demo reads better on a document with a real outline and dense pages.
if (-not $Pdf) {
    # The designed demo document, not a test fixture: fixtures are deliberately
    # sparse and look empty on camera.
    $Pdf = Join-Path $repoRoot "tests\fixtures\demo-document.pdf"
    if (-not (Test-Path $Pdf)) {
        $env:LUMEN_MAKE_FIXTURES = Join-Path $repoRoot "tests\fixtures"
        & $exe | Out-Null
        Remove-Item Env:LUMEN_MAKE_FIXTURES
    }
}
if (-not (Test-Path $Pdf)) { throw "No document to record: $Pdf" }

Remove-Item $frames -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $frames | Out-Null
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

foreach ($n in @("LUMEN_SEARCH","LUMEN_SELECT","LUMEN_SELECT_WORD","LUMEN_ANNOTATE",
                 "LUMEN_SAVE_AS","LUMEN_PAGEOP","LUMEN_EXPORT","LUMEN_COMPRESS",
                 "LUMEN_FORM_FILL","LUMEN_CAPTURE","LUMEN_BENCH","LUMEN_REPORT",
                 "LUMEN_EDIT_TEXT")) {
    Remove-Item "Env:$n" -ErrorAction SilentlyContinue
}

# A working copy: the demo highlights text, and the fixture should stay pristine.
$working = Join-Path $repoRoot "work\demo\demo.pdf"
New-Item -ItemType Directory -Force -Path (Split-Path $working) | Out-Null
Copy-Item $Pdf $working -Force

Write-Host "Recording..." -ForegroundColor Cyan
$env:LUMEN_RECORD = $frames
$env:QT_FORCE_STDERR_LOGGING = "1"

$proc = Start-Process $exe -ArgumentList $working -Wait -PassThru `
    -RedirectStandardError (Join-Path $repoRoot "work\demo\record.log")
Remove-Item Env:LUMEN_RECORD

$count = (Get-ChildItem $frames -Filter "frame-*.png" -ErrorAction SilentlyContinue |
          Measure-Object).Count
if ($count -lt 30) {
    throw "Only $count frames were recorded -- the demo did not run (exit $($proc.ExitCode))"
}
Write-Host "Captured $count frames ($([math]::Round($count / $Fps, 1)) s)"

$mp4 = Join-Path $outDir "demo.mp4"
$webm = Join-Path $outDir "demo.webm"
$poster = Join-Path $outDir "poster.jpg"

Write-Host ""
Write-Host "Encoding..." -ForegroundColor Cyan

# Scaled to 1280 wide: the page never shows it larger, and it halves the size.
# yuv420p and faststart are what make it play everywhere and start immediately.
& $ffmpeg -y -loglevel error -framerate $Fps -i (Join-Path $frames "frame-%05d.png") `
    -vf "scale=1280:-2:flags=lanczos" `
    -c:v libx264 -preset slow -crf 24 -pix_fmt yuv420p -movflags +faststart `
    -an $mp4
if ($LASTEXITCODE -ne 0) { throw "MP4 encode failed" }

# VP9 needs the pixel format stated explicitly; without it libvpx refuses the
# RGBA frames PNG decodes to and writes nothing at all.
& $ffmpeg -y -loglevel error -framerate $Fps -i (Join-Path $frames "frame-%05d.png") `
    -vf "scale=1280:-2:flags=lanczos" -pix_fmt yuv420p `
    -c:v libvpx-vp9 -crf 36 -b:v 0 -row-mt 1 -deadline good -an $webm
if ($LASTEXITCODE -ne 0) { Write-Warning "WebM encode failed -- MP4 alone will do" }

# The poster is what people see before pressing play, and on any browser that
# refuses to autoplay. Taken a little way in, where the UI is populated.
$posterFrame = Join-Path $frames ("frame-{0:d5}.png" -f ([int]($count * 0.18)))
& $ffmpeg -y -loglevel error -i $posterFrame -vf "scale=1280:-2:flags=lanczos" -q:v 4 $poster | Out-Null

Write-Host ""
foreach ($f in @($mp4, $webm, $poster)) {
    if (Test-Path $f) {
        Write-Host ("  {0,-12} {1,7} KB" -f (Split-Path $f -Leaf),
                    [math]::Round((Get-Item $f).Length / 1KB))
    }
}
Write-Host ""
Write-Host "Wrote to $outDir" -ForegroundColor Green
