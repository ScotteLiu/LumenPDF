<#
.SYNOPSIS
    Runs LumenPDF's functional test suite.

.DESCRIPTION
    Every check asserts a number or a string, never a screenshot. That matters
    most for redaction, where a black box is not evidence: the assertion is that
    the page's extracted text length went to zero.

    Each case launches the app with LUMEN_* hooks, has it write a JSON state
    report, and compares that report against expectations. The app is exercised
    through the same controllers the UI uses, so a passing test means the UI
    would have seen the same thing.

    Fixtures are generated, not committed -- see scripts/make-form-fixture.ps1
    and src/app/TestFixtures.cpp.

.PARAMETER Filter
    Only run cases whose name contains this string.

.EXAMPLE
    ./scripts/run-tests.ps1
    ./scripts/run-tests.ps1 -Filter cjk
#>

[CmdletBinding()]
param(
    [string]$Filter = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "build\windows-release\lumenpdf.exe"
$fixtureDir = Join-Path $repoRoot "tests\fixtures"
$scratch = Join-Path $repoRoot "work\tests"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -NoDeploy | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
if (-not (Test-Path $exe)) { throw "Not built: $exe" }

# -- Fixtures ---------------------------------------------------------------
Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

$env:LUMEN_MAKE_FIXTURES = $fixtureDir
& $exe | Out-Null
Remove-Item Env:LUMEN_MAKE_FIXTURES

& (Join-Path $PSScriptRoot "make-form-fixture.ps1") | Out-Null

$latin = Join-Path $fixtureDir "latin-sample.pdf"
$cjk   = Join-Path $fixtureDir "cjk-sample.pdf"
$form  = Join-Path $fixtureDir "form-simple.pdf"

foreach ($f in @($latin, $cjk, $form)) {
    if (-not (Test-Path $f)) { throw "Fixture missing: $f" }
}

# -- Harness ----------------------------------------------------------------
$hookNames = @("LUMEN_SEARCH", "LUMEN_SELECT", "LUMEN_SELECT_WORD", "LUMEN_ANNOTATE",
               "LUMEN_SAVE_AS", "LUMEN_PAGEOP", "LUMEN_EXPORT", "LUMEN_COMPRESS",
               "LUMEN_FORM_FILL", "LUMEN_THEME", "LUMEN_SIDEBAR_TAB", "LUMEN_CAPTURE",
               "LUMEN_EDIT_TEXT", "LUMEN_BENCH")

$env:QT_FORCE_STDERR_LOGGING = "1"
$env:LUMEN_CAPTURE_DELAY = "3000"

$script:passed = 0
$script:failed = 0
$script:failures = @()

# Runs the app against `pdf` with the given hooks and returns the state report.
function Invoke-Lumen {
    param([string]$Pdf, [hashtable]$Hooks = @{}, [string]$Name)

    foreach ($name in $hookNames) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue }
    foreach ($entry in $Hooks.GetEnumerator()) { Set-Item "Env:$($entry.Key)" $entry.Value }

    $reportPath = Join-Path $scratch "$Name.json"
    $env:LUMEN_REPORT = $reportPath

    $proc = Start-Process $exe -ArgumentList $Pdf -Wait -PassThru `
        -RedirectStandardError (Join-Path $scratch "$Name.log")

    if ($proc.ExitCode -ne 0) {
        return @{ ok = $false; reason = "exit code $($proc.ExitCode)" }
    }
    if (-not (Test-Path $reportPath)) {
        return @{ ok = $false; reason = "no state report written" }
    }

    # Read as UTF-8 explicitly: Get-Content would mangle CJK on this console.
    $json = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($reportPath))
    return @{ ok = $true; report = ($json | ConvertFrom-Json) }
}

function Assert-Equal {
    param($Expected, $Actual, [string]$What)

    if ($Expected -eq $Actual) {
        Write-Host "    ok   $What = $Actual" -ForegroundColor DarkGray
        $script:passed++
    } else {
        Write-Host "    FAIL $What : expected $Expected, got $Actual" -ForegroundColor Red
        $script:failed++
        $script:failures += "$What : expected $Expected, got $Actual"
    }
}

function Assert-True {
    param([bool]$Condition, [string]$What)

    if ($Condition) {
        Write-Host "    ok   $What" -ForegroundColor DarkGray
        $script:passed++
    } else {
        Write-Host "    FAIL $What" -ForegroundColor Red
        $script:failed++
        $script:failures += $What
    }
}

function Start-Case {
    param([string]$Name)
    if ($Filter -and $Name -notlike "*$Filter*") { return $false }
    Write-Host "  $Name" -ForegroundColor Cyan
    return $true
}

Write-Host ""
Write-Host "LumenPDF test suite" -ForegroundColor White
Write-Host ""

# -- Cases ------------------------------------------------------------------

if (Start-Case "open-latin") {
    $r = Invoke-Lumen -Pdf $latin -Name "open-latin"
    Assert-True $r.ok "app ran and reported state"
    if ($r.ok) {
        Assert-Equal 3 $r.report.pageCount "page count"
        Assert-True ($r.report.pageTextLengths[0] -gt 100) "page 1 has extractable text"
        Assert-Equal $false $r.report.modified "a freshly opened document is unmodified"
    }
}

if (Start-Case "search-latin") {
    # "cycle" appears 3 times per page across 3 pages.
    $r = Invoke-Lumen -Pdf $latin -Hooks @{ LUMEN_SEARCH = "cycle" } -Name "search-latin"
    if ($r.ok) { Assert-Equal 9 $r.report.searchHits "'cycle' hit count" }
}

if (Start-Case "search-latin-none") {
    $r = Invoke-Lumen -Pdf $latin -Hooks @{ LUMEN_SEARCH = "zzzznotpresent" } -Name "search-none"
    if ($r.ok) { Assert-Equal 0 $r.report.searchHits "absent term finds nothing" }
}

if (Start-Case "select-word-latin") {
    $r = Invoke-Lumen -Pdf $latin -Hooks @{ LUMEN_SELECT_WORD = "0,90,150" } -Name "word-latin"
    if ($r.ok) {
        Assert-Equal "The" $r.report.selectionText "double-click selects a whole Latin word"
    }
}

# -- CJK. Every one of these was a real bug before the fixes it guards. ------

if (Start-Case "cjk-open") {
    $r = Invoke-Lumen -Pdf $cjk -Name "cjk-open"
    if ($r.ok) {
        Assert-Equal 2 $r.report.pageCount "CJK page count"
        Assert-True ($r.report.pageTextLengths[0] -gt 100) "CJK text is extractable"
    }
}

if (Start-Case "cjk-search-single-char") {
    # Regression: kMinQueryLength rejected one-character queries, so searching a
    # single Chinese character -- entirely normal -- silently found nothing.
    $yi = [string][char]0x4E00      # U+4E00, the ideograph for "one"
    $r = Invoke-Lumen -Pdf $cjk -Hooks @{ LUMEN_SEARCH = $yi } -Name "cjk-single"
    if ($r.ok) { Assert-Equal 2 $r.report.searchHits "single ideograph is searchable" }
}

if (Start-Case "cjk-search-phrase") {
    $wenjian = [string][char]0x6587 + [char]0x4EF6
    $r = Invoke-Lumen -Pdf $cjk -Hooks @{ LUMEN_SEARCH = $wenjian } -Name "cjk-phrase"
    if ($r.ok) { Assert-Equal 8 $r.report.searchHits "CJK phrase hit count" }
}

if (Start-Case "cjk-no-radical-codepoints") {
    # Regression: PDFium extracted Kangxi-radical duplicates (U+2F00 for U+4E00),
    # so copied text looked right and was wrong everywhere it was pasted.
    $r = Invoke-Lumen -Pdf $cjk -Hooks @{ LUMEN_SELECT = "0,72,135,300,150" } -Name "cjk-radicals"
    if ($r.ok) {
        $text = $r.report.selectionText
        $radicals = @($text.ToCharArray() | Where-Object {
            [int]$_ -ge 0x2E80 -and [int]$_ -le 0x2FD5
        })
        Assert-True ($text.Length -gt 10) "CJK drag selection returns text"
        Assert-Equal 0 $radicals.Count "no radical-block codepoints in extracted text"
        Assert-True ($text.Contains([string][char]0x4E00)) "contains the real ideograph U+4E00"
    }
}

if (Start-Case "cjk-select-word") {
    # Regression on two counts: a one-character selection was reported as empty,
    # and Latin word rules would have swallowed the whole Han run.
    $r = Invoke-Lumen -Pdf $cjk -Hooks @{ LUMEN_SELECT_WORD = "0,80,143" } -Name "cjk-word"
    if ($r.ok) {
        Assert-Equal 1 $r.report.selectionLength "double-click selects exactly one ideograph"
    }
}

# -- Editing ----------------------------------------------------------------

if (Start-Case "annotate-persists") {
    $target = Join-Path $scratch "annotated.pdf"
    Copy-Item $latin $target -Force
    Invoke-Lumen -Pdf $target -Name "annotate" -Hooks @{
        LUMEN_SELECT = "0,72,140,400,160"
        LUMEN_ANNOTATE = "highlight"
        LUMEN_SAVE_AS = $target
    } | Out-Null

    $bytes = [System.IO.File]::ReadAllBytes($target)
    $raw = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    Assert-True ($raw.Contains("/Highlight")) "highlight annotation is written to the file"
}

if (Start-Case "redact-destroys-text") {
    # The whole point: a black box proves nothing, a zero-length page does.
    $target = Join-Path $scratch "redacted.pdf"
    Copy-Item $latin $target -Force

    $before = Invoke-Lumen -Pdf $target -Name "redact-before"
    $beforeLength = if ($before.ok) { $before.report.pageTextLengths[0] } else { -1 }

    Invoke-Lumen -Pdf $target -Name "redact" -Hooks @{
        LUMEN_SELECT = "0,72,140,400,160"
        LUMEN_ANNOTATE = "redact"
        LUMEN_SAVE_AS = $target
    } | Out-Null

    $after = Invoke-Lumen -Pdf $target -Name "redact-after"
    if ($after.ok) {
        Assert-True ($beforeLength -gt 100) "page 1 had text before redaction"
        Assert-Equal 0 $after.report.pageTextLengths[0] "redacted page has no extractable text"
        Assert-True ($after.report.pageTextLengths[1] -gt 100) "other pages are untouched"
    }
}

if (Start-Case "pageops-undo") {
    foreach ($op in @("rotate,1,1", "delete,1", "move,0,2")) {
        $target = Join-Path $scratch "pageop.pdf"
        Copy-Item $latin $target -Force
        $r = Invoke-Lumen -Pdf $target -Name "pageop" -Hooks @{ LUMEN_PAGEOP = "$op,undo" }
        if ($r.ok) { Assert-Equal 3 $r.report.pageCount "undo of '$op' restores the page count" }
    }
}

if (Start-Case "merge") {
    $target = Join-Path $scratch "merged.pdf"
    Copy-Item $latin $target -Force
    $r = Invoke-Lumen -Pdf $target -Name "merge" -Hooks @{ LUMEN_PAGEOP = "merge,$cjk" }
    if ($r.ok) { Assert-Equal 5 $r.report.pageCount "3-page + 2-page merge gives 5 pages" }
}

if (Start-Case "extract") {
    $out = Join-Path $scratch "extracted.pdf"
    Invoke-Lumen -Pdf $latin -Name "extract" -Hooks @{ LUMEN_PAGEOP = "extract,$out,0,1" } | Out-Null
    Assert-True (Test-Path $out) "extract wrote a file"
    if (Test-Path $out) {
        $r = Invoke-Lumen -Pdf $out -Name "extract-check"
        if ($r.ok) { Assert-Equal 2 $r.report.pageCount "extracted range has 2 pages" }
    }
}

if (Start-Case "export-text") {
    $out = Join-Path $scratch "exported.txt"
    Invoke-Lumen -Pdf $cjk -Name "export" -Hooks @{ LUMEN_EXPORT = "text,$out" } | Out-Null
    Assert-True (Test-Path $out) "text export wrote a file"
    if (Test-Path $out) {
        $text = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($out))
        Assert-True ($text.Length -gt 200) "exported text is non-trivial"
        Assert-True ($text.Contains([string][char]0x4E00)) "exported CJK is correctly encoded"
    }
}

if (Start-Case "form-fill") {
    $target = Join-Path $scratch "filled.pdf"
    Copy-Item $form $target -Force
    Invoke-Lumen -Pdf $target -Name "form" -Hooks @{
        LUMEN_FORM_FILL = "0,300,119,Test Name;0,300,169,test@example.com;0,169,223,"
        LUMEN_SAVE_AS = $target
    } | Out-Null

    $raw = [System.Text.Encoding]::GetEncoding(28591).GetString(
        [System.IO.File]::ReadAllBytes($target))
    Assert-True ($raw.Contains("Test Name")) "text field value is saved"
    Assert-True ($raw.Contains("test@example.com")) "second field gets its own value"
    Assert-True ($raw -match '/AS\s*/Yes') "checkbox state is saved"
}

if (Start-Case "form-detection") {
    $r = Invoke-Lumen -Pdf $form -Name "form-detect"
    if ($r.ok) { Assert-Equal $true $r.report.hasForms "form document is detected as fillable" }
    $r = Invoke-Lumen -Pdf $latin -Name "form-detect-neg"
    if ($r.ok) { Assert-Equal $false $r.report.hasForms "plain document is not" }
}

if (Start-Case "text-edit-refuses-to-corrupt") {
    # The guarantee being tested is not "editing works" -- it is that an edit
    # which cannot be encoded in the run's font is refused and rolled back,
    # rather than silently writing garbage into the document. Real PDFs embed
    # font subsets, so this is the common case, not the edge case.
    $target = Join-Path $scratch "textedit-refuse.pdf"
    Copy-Item $latin $target -Force

    # The heading is "Latin fixture page 1", so its subset has no 'b', 'd', 'y'
    # or 'm'. Asking for them must fail.
    Invoke-Lumen -Pdf $target -Name "textedit-refuse" -Hooks @{
        LUMEN_EDIT_TEXT = "0,150,90,Edited by LumenPDF"
        LUMEN_SAVE_AS = $target
    } | Out-Null

    $out = Join-Path $scratch "textedit-refuse.txt"
    Invoke-Lumen -Pdf $target -Name "textedit-refuse-read" -Hooks @{
        LUMEN_EXPORT = "text,$out"
    } | Out-Null

    if (Test-Path $out) {
        $text = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($out))
        Assert-True ($text.Contains("Latin fixture page 1")) `
            "a text edit the font cannot encode leaves the original intact"
        Assert-True (-not $text.Contains("Latin fixite")) `
            "no garbled text is written when an edit is refused"
    } else {
        Assert-True $false "text export after refused edit"
    }
}

if (Start-Case "text-edit-applies-and-undoes") {
    # Only glyphs already present in the run, so this one must succeed.
    $target = Join-Path $scratch "textedit-apply.pdf"
    Copy-Item $latin $target -Force

    Invoke-Lumen -Pdf $target -Name "textedit-apply" -Hooks @{
        LUMEN_EDIT_TEXT = "0,150,90,Latin future page 1"
        LUMEN_SAVE_AS = $target
    } | Out-Null

    $out = Join-Path $scratch "textedit-apply.txt"
    Invoke-Lumen -Pdf $target -Name "textedit-apply-read" -Hooks @{
        LUMEN_EXPORT = "text,$out"
    } | Out-Null

    if (Test-Path $out) {
        $text = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($out))
        Assert-True ($text.Contains("Latin future page 1")) "an encodable text edit is applied"
    }

    # Undo must put the original string back.
    $undoTarget = Join-Path $scratch "textedit-undo.pdf"
    Copy-Item $latin $undoTarget -Force
    Invoke-Lumen -Pdf $undoTarget -Name "textedit-undo" -Hooks @{
        LUMEN_EDIT_TEXT = "0,150,90,Latin future page 1,--undo"
        LUMEN_SAVE_AS = $undoTarget
    } | Out-Null

    $undoOut = Join-Path $scratch "textedit-undo.txt"
    Invoke-Lumen -Pdf $undoTarget -Name "textedit-undo-read" -Hooks @{
        LUMEN_EXPORT = "text,$undoOut"
    } | Out-Null

    if (Test-Path $undoOut) {
        $text = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($undoOut))
        Assert-True ($text.Contains("Latin fixture page 1")) "undo restores the original text"
        Assert-True (-not $text.Contains("Latin future page 1")) "undo removes the edit"
    }
}

# -- Hostile input ----------------------------------------------------------
#
# The bar here is not "opens correctly" -- several of these cannot be opened at
# all. It is that the app refuses them without crashing or hanging, and does not
# let one crafted file dictate how much memory the process uses.

if (Start-Case "hostile-input") {
    & (Join-Path $PSScriptRoot "make-hostile-fixtures.ps1") | Out-Null
    $hostileDir = Join-Path $fixtureDir "hostile"

    # Files that cannot be a document, and must be reported as an error.
    $mustRefuse = @("empty", "header-only", "not-a-pdf", "trailer-only", "truncated")

    # Files that are damaged or malicious but that PDFium can still make sense
    # of. They must open without hanging; how many pages they yield is PDFium's
    # business, not something to pin down.
    $mustSurvive = @("bad-startxref", "corrupted-body", "cyclic-outline",
                     "recursive-pages", "huge-mediabox")

    $peakMemory = 0

    foreach ($stem in ($mustRefuse + $mustSurvive)) {
        $path = Join-Path $hostileDir "$stem.pdf"
        if (-not (Test-Path $path)) {
            Assert-True $false "$stem.pdf was generated"
            continue
        }

        $r = Invoke-Lumen -Pdf $path -Name "hostile-$stem"

        if (-not $r.ok) {
            Assert-True $false "$stem : survived without crashing ($($r.reason))"
            continue
        }

        Assert-True $true "$stem : no crash, no hang"
        $peakMemory = [math]::Max($peakMemory, $r.report.memoryMb)

        # DocumentController::Status -- 2 is Ready, 3 is Error.
        if ($mustRefuse -contains $stem) {
            Assert-Equal 3 $r.report.status "$stem : refused with an error status"
        } else {
            Assert-Equal 2 $r.report.status "$stem : opened despite the damage"
        }
    }

    # A crafted page declaring 200000 inches square once forced a single 268 MB
    # raster. The render path now has a pixel budget; this is what holds it.
    Assert-True ($peakMemory -lt 250) `
        "no hostile file pushes memory past 250 MB (peak ${peakMemory} MB)"
}

# -- Summary ----------------------------------------------------------------
foreach ($name in $hookNames) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue }
Remove-Item Env:LUMEN_REPORT -ErrorAction SilentlyContinue

Write-Host ""
if ($script:failed -eq 0) {
    Write-Host "$($script:passed) assertions passed" -ForegroundColor Green
    exit 0
}

Write-Host "$($script:passed) passed, $($script:failed) FAILED" -ForegroundColor Red
foreach ($failure in $script:failures) { Write-Host "  - $failure" -ForegroundColor Red }
exit 1

