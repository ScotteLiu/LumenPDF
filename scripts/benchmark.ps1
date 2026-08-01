<#
.SYNOPSIS
    Measures LumenPDF's startup, scroll and memory behaviour.

.DESCRIPTION
    "Fast" is the product's first claim, so it needs numbers rather than an
    impression.

    Three things are measured:

      * Cold start -- wall-clock from process launch to the first rendered page
        being on screen. Measured from outside the process as well as inside it,
        because the inside number cannot include process creation and DLL
        loading, which the user certainly experiences.
      * Scroll frame rate -- frames actually presented while scrolling
        continuously, counted with FrameAnimation. A warm-up period is discarded
        so the number is the steady state, not the cache filling.
      * Memory -- working set after the document is open and scrolled.

    Each run is repeated and the median reported: a single sample on a desktop
    is mostly noise from whatever else the machine is doing.

    If SumatraPDF or Acrobat are installed, their cold start is measured the
    same way for comparison. Missing competitors are reported as missing rather
    than silently skipped -- a benchmark that omits its baseline is marketing.

.EXAMPLE
    ./scripts/benchmark.ps1
#>

[CmdletBinding()]
param(
    [int]$Runs = 5,
    [switch]$SkipBuild,

    # Compare against docs/perf-baseline.json and exit non-zero if anything has
    # regressed past its tolerance. Opt-in, and never run in CI: the baseline is
    # specific to one machine and means nothing on a shared runner.
    [switch]$Gate,

    # Overwrite the baseline with what this run measured. Only after a change
    # that is meant to move it, and say why in the commit message.
    [switch]$Record
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "build\windows-release\lumenpdf.exe"
$fixtureDir = Join-Path $repoRoot "tests\fixtures"
$scratch = Join-Path $repoRoot "work\bench"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -NoDeploy | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
if (-not (Test-Path $exe)) { throw "Not built: $exe" }

New-Item -ItemType Directory -Force -Path $scratch | Out-Null

# -- Fixtures ---------------------------------------------------------------
$large = Join-Path $fixtureDir "large-sample.pdf"
$latin = Join-Path $fixtureDir "latin-sample.pdf"

if (-not (Test-Path $large) -or -not (Test-Path $latin)) {
    Write-Host "Generating fixtures (the 1000-page one takes a moment)..." -ForegroundColor Cyan
    $env:LUMEN_MAKE_FIXTURES = $fixtureDir
    $env:LUMEN_MAKE_LARGE = "1"
    & $exe | Out-Null
    Remove-Item Env:LUMEN_MAKE_FIXTURES, Env:LUMEN_MAKE_LARGE
}

$env:QT_FORCE_STDERR_LOGGING = "1"
foreach ($n in @("LUMEN_SEARCH","LUMEN_SELECT","LUMEN_SELECT_WORD","LUMEN_ANNOTATE",
                 "LUMEN_SAVE_AS","LUMEN_PAGEOP","LUMEN_EXPORT","LUMEN_COMPRESS",
                 "LUMEN_FORM_FILL","LUMEN_CAPTURE","LUMEN_BENCH")) {
    Remove-Item "Env:$n" -ErrorAction SilentlyContinue
}

function Get-Median {
    param([double[]]$Values)
    if ($Values.Count -eq 0) { return 0 }
    $sorted = $Values | Sort-Object
    return [math]::Round($sorted[[int]($sorted.Count / 2)], 1)
}

# One run: launch, let it settle, read back the state report.
function Measure-Run {
    param([string]$Pdf, [string]$Name, [int]$SettleMs, [string]$Bench = "")

    $reportPath = Join-Path $scratch "$Name.json"
    Remove-Item $reportPath -ErrorAction SilentlyContinue

    $env:LUMEN_REPORT = $reportPath
    $env:LUMEN_CAPTURE_DELAY = "$SettleMs"
    if ($Bench) { $env:LUMEN_BENCH = $Bench } else { Remove-Item Env:LUMEN_BENCH -ErrorAction SilentlyContinue }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process $exe -ArgumentList $Pdf -Wait -PassThru `
        -RedirectStandardError (Join-Path $scratch "$Name.log")
    $sw.Stop()

    if (-not (Test-Path $reportPath)) { return $null }

    $json = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($reportPath))
    $report = $json | ConvertFrom-Json

    # Total process wall time minus the artificial settle delay is the honest
    # outside-the-process cost, including process creation and DLL loading.
    $report | Add-Member -NotePropertyName wallMs `
        -NotePropertyValue ($sw.Elapsed.TotalMilliseconds - $SettleMs) -Force
    return $report
}

Write-Host ""
Write-Host "LumenPDF benchmark  ($Runs runs, median reported)" -ForegroundColor White
Write-Host ""

# Collected for -Gate / -Record. Keyed by the short scenario name.
$script:results = @{}

# -- Cold start -------------------------------------------------------------
foreach ($case in @(
    @{ Name = "latin (3 pages)"; Key = "latin"; Pdf = $latin },
    @{ Name = "large (1000 pages)"; Key = "large"; Pdf = $large }
)) {
    if (-not (Test-Path $case.Pdf)) { continue }

    $firstPage = @(); $qmlLoaded = @(); $wall = @(); $memory = @(); $pages = 0

    for ($i = 0; $i -lt $Runs; $i++) {
        $r = Measure-Run -Pdf $case.Pdf -Name "start-$i" -SettleMs 2500
        if (-not $r) { continue }
        $pages = $r.pageCount
        if ($r.timingsMs.'first-page-visible') { $firstPage += $r.timingsMs.'first-page-visible' }
        if ($r.timingsMs.'qml-loaded') { $qmlLoaded += $r.timingsMs.'qml-loaded' }
        $wall += $r.wallMs
        $memory += $r.memoryMb
    }

    Write-Host "  $($case.Name) -- $pages pages" -ForegroundColor Cyan
    Write-Host ("    QML loaded            {0,7} ms" -f (Get-Median $qmlLoaded))
    Write-Host ("    first page on screen  {0,7} ms   (in-process)" -f (Get-Median $firstPage))
    Write-Host ("    total process time    {0,7} ms   (includes process + DLL load)" -f (Get-Median $wall))
    Write-Host ("    memory                {0,7} MB" -f (Get-Median $memory))
    Write-Host ""

    $script:results[$case.Key] = @{
        firstPage = (Get-Median $firstPage)
        memory    = (Get-Median $memory)
    }
}

# -- Scroll -----------------------------------------------------------------
if (Test-Path $large) {
    $fps = @(); $memory = @()
    for ($i = 0; $i -lt $Runs; $i++) {
        # 1.2 s warm-up + 4 s measurement inside the app, plus room to settle.
        $r = Measure-Run -Pdf $large -Name "scroll-$i" -SettleMs 7000 -Bench "scroll"
        if ($r -and $r.benchmark) {
            $fps += $r.benchmark.fps
            $memory += $r.memoryMb
        }
    }

    $refresh = (Get-CimInstance Win32_VideoController |
        Where-Object { $_.CurrentRefreshRate } |
        Select-Object -First 1 -ExpandProperty CurrentRefreshRate)

    Write-Host "  continuous scroll, 1000-page document" -ForegroundColor Cyan
    if ($fps.Count -gt 0) {
        # Rendering is vsync-locked, so the display's refresh rate is the
        # ceiling. Hitting it means no frames were dropped; the honest way to
        # report the number is against that ceiling, not as a raw figure.
        $note = if ($refresh -and (Get-Median $fps) -ge ($refresh - 2)) {
            "  (display refresh is $refresh Hz -- vsync-locked, no dropped frames)"
        } elseif ($refresh) {
            "  (display refresh is $refresh Hz)"
        } else { "" }

        Write-Host ("    frame rate            {0,7} fps{1}" -f (Get-Median $fps), $note)
        Write-Host ("    memory after scroll   {0,7} MB" -f (Get-Median $memory))
        Write-Host ("    samples               {0,7}" -f ($fps -join ", "))
    } else {
        Write-Warning "    scroll benchmark produced no samples"
    }
    Write-Host ""
}

# -- Comparison -------------------------------------------------------------
Write-Host "  cold start of other viewers, same file" -ForegroundColor Cyan

$competitors = @(
    @{ Name = "SumatraPDF"; Paths = @(
        "$env:LOCALAPPDATA\SumatraPDF\SumatraPDF.exe",
        "$env:ProgramFiles\SumatraPDF\SumatraPDF.exe",
        "${env:ProgramFiles(x86)}\SumatraPDF\SumatraPDF.exe") },
    @{ Name = "Adobe Acrobat"; Paths = @(
        "$env:ProgramFiles\Adobe\Acrobat DC\Acrobat\Acrobat.exe",
        "${env:ProgramFiles(x86)}\Adobe\Acrobat Reader DC\Reader\AcroRd32.exe") },
    @{ Name = "Microsoft Edge"; Paths = @(
        "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe") }
)

$anyFound = $false
foreach ($competitor in $competitors) {
    $path = $competitor.Paths | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $path) {
        Write-Host ("    {0,-16} not installed -- no baseline measured" -f $competitor.Name) `
            -ForegroundColor DarkYellow
        continue
    }

    $anyFound = $true
    # Time to a responding main window is the closest comparable signal without
    # instrumenting someone else's binary. It is not the same as "first page
    # visible", and is reported as the weaker measure it is.
    $samples = @()
    for ($i = 0; $i -lt [math]::Min($Runs, 3); $i++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $proc = Start-Process $path -ArgumentList $latin -PassThru
        try {
            $proc.WaitForInputIdle(15000) | Out-Null
            $samples += $sw.Elapsed.TotalMilliseconds
        } catch {
            # WaitForInputIdle is not available for every process type.
        }
        $sw.Stop()
        Start-Sleep -Milliseconds 400
        if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit(3000) | Out-Null }
    }

    if ($samples.Count -gt 0) {
        Write-Host ("    {0,-16} {1,7} ms to first input-idle" -f $competitor.Name, (Get-Median $samples))
    } else {
        Write-Host ("    {0,-16} found, but could not be timed" -f $competitor.Name) -ForegroundColor DarkYellow
    }
}

if (-not $anyFound) {
    Write-Host "    No other PDF viewer is installed, so these numbers stand alone." -ForegroundColor DarkYellow
}

foreach ($n in @("LUMEN_REPORT","LUMEN_BENCH","LUMEN_CAPTURE_DELAY")) {
    Remove-Item "Env:$n" -ErrorAction SilentlyContinue
}
Write-Host ""

# -- Baseline gate ----------------------------------------------------------
#
# A performance number nobody checks drifts. This project already lost 210 ms of
# time-to-first-page to a change whose cost was invisible for two days, and the
# only reason it surfaced was somebody happening to re-run the benchmark.
#
# The baseline is machine-specific, so this is opt-in rather than part of the
# test suite: on a GitHub runner these numbers would be noise.
$baselinePath = Join-Path $repoRoot "docs\perf-baseline.json"

if ($Record) {
    if (-not (Test-Path $baselinePath)) { throw "No baseline file at $baselinePath" }
    $b = Get-Content $baselinePath -Raw | ConvertFrom-Json
    foreach ($k in @("latin","large")) {
        if ($script:results.ContainsKey($k)) {
            $b.$k.firstPageMs = [int]$script:results[$k].firstPage
            $b.$k.memoryMb    = [int]$script:results[$k].memory
        }
    }
    $b.recorded = (Get-Date -Format "yyyy-MM-dd")
    $b.commit = (& git -C $repoRoot rev-parse --short HEAD)
    $b | ConvertTo-Json -Depth 5 | Set-Content $baselinePath -Encoding utf8
    Write-Host "Baseline re-recorded. Say in the commit message why it moved." -ForegroundColor Yellow
}

if ($Gate) {
    if (-not (Test-Path $baselinePath)) { throw "No baseline file at $baselinePath" }
    $b = Get-Content $baselinePath -Raw | ConvertFrom-Json
    $tol = [double]$b.tolerancePercent
    $bad = 0

    Write-Host "--- gate (tolerance ${tol}%) ---" -ForegroundColor Cyan
    foreach ($k in @("latin","large")) {
        if (-not $script:results.ContainsKey($k)) { continue }
        foreach ($m in @(@{n="firstPageMs"; v=$script:results[$k].firstPage},
                         @{n="memoryMb";    v=$script:results[$k].memory})) {
            $want = [double]$b.$k.($m.n)
            $got  = [double]$m.v
            $delta = if ($want -gt 0) { ($got - $want) / $want * 100 } else { 0 }
            $sign = if ($delta -ge 0) { "+" } else { "" }
            $line = "  {0,-6} {1,-12} {2,7:N0}  baseline {3,7:N0}  {4}{5:N1}%" -f `
                    $k, $m.n, $got, $want, $sign, $delta
            if ($delta -gt $tol) { Write-Host "$line  REGRESSED" -ForegroundColor Red; $bad++ }
            elseif ($delta -lt (-$tol)) { Write-Host "$line  improved -- re-record" -ForegroundColor Green }
            else { Write-Host $line -ForegroundColor DarkGray }
        }
    }

    Write-Host ""
    if ($bad -gt 0) {
        Write-Host "$bad measurement(s) regressed past tolerance." -ForegroundColor Red
        exit 1
    }
    Write-Host "Within tolerance of the baseline." -ForegroundColor Green
}
