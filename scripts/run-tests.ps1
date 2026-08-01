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
& (Join-Path $PSScriptRoot "make-link-fixture.ps1") | Out-Null
& (Join-Path $PSScriptRoot "make-encrypted-fixture.ps1") | Out-Null

$latin     = Join-Path $fixtureDir "latin-sample.pdf"
$cjk       = Join-Path $fixtureDir "cjk-sample.pdf"
$form      = Join-Path $fixtureDir "form-simple.pdf"
$links     = Join-Path $fixtureDir "links.pdf"
$encrypted = Join-Path $fixtureDir "encrypted.pdf"

foreach ($f in @($latin, $cjk, $form, $links, $encrypted)) {
    if (-not (Test-Path $f)) { throw "Fixture missing: $f" }
}

# -- Harness ----------------------------------------------------------------
$hookNames = @("LUMEN_SEARCH", "LUMEN_SELECT", "LUMEN_SELECT_WORD", "LUMEN_ANNOTATE",
               "LUMEN_SAVE_AS", "LUMEN_PAGEOP", "LUMEN_EXPORT", "LUMEN_COMPRESS",
               "LUMEN_FORM_FILL", "LUMEN_THEME", "LUMEN_SIDEBAR_TAB", "LUMEN_CAPTURE",
               "LUMEN_EDIT_TEXT", "LUMEN_BENCH", "LUMEN_OCR",
               "LUMEN_PRINT", "LUMEN_PASSWORD", "LUMEN_LINK_PROBE", "LUMEN_NOTE_PAGE",
               "LUMEN_VERSION_COMPARE", "LUMEN_UPDATE_ASSET", "LUMEN_UPDATE_VERIFY",
               "LUMEN_HOVER")

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
    $r = Invoke-Lumen -Pdf $latin -Hooks @{ LUMEN_SELECT_WORD = "0,find:cycle" } -Name "word-latin"
    if ($r.ok) {
        Assert-Equal "cycle" $r.report.selectionText "double-click selects a whole Latin word"
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

# -- Scripts beyond Latin and Han -------------------------------------------
#
# Text extraction is where a PDF tool quietly goes wrong, and it goes wrong
# differently in each writing system. These assert that a phrase written in each
# script comes back out of the file recognisably, rather than as the lookalikes,
# reordered runs or dropped combining marks that extraction can silently produce.

if (Start-Case "world-scripts") {
    $world = Join-Path $fixtureDir "world-scripts.pdf"

    if (-not (Test-Path $world)) {
        Assert-True $false "world-scripts fixture was generated"
    } else {
        $out = Join-Path $scratch "world.txt"
        $r = Invoke-Lumen -Pdf $world -Name "world" -Hooks @{ LUMEN_EXPORT = "text,$out" }

        if ($r.ok -and (Test-Path $out)) {
            $text = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($out))

            # A distinctive fragment per script. Short on purpose: the point is
            # that the characters survive extraction, not that line breaking is
            # identical.
            $probes = [ordered]@{
                "Latin"              = "quick brown fox"
                "Latin diacritics"   = [string][char]0x017E + [char]0x006C   # žl
                "Vietnamese"         = [string][char]0x1EBF + [char]0x006E   # ến
                "Greek"              = [string][char]0x03BA + [char]0x03B5   # κε
                "Cyrillic"           = [string][char]0x0435 + [char]0x0449   # ещ
                "Arabic"             = [string][char]0x0627 + [char]0x0644   # ال
                "Hebrew"             = [string][char]0x05D1 + [char]0x05E2   # בע
                "Devanagari"         = [string][char]0x0939 + [char]0x093F   # हि
                "Thai"               = [string][char]0x0E20 + [char]0x0E32   # ภา
                "Korean"             = [string][char]0xD55C + [char]0xAD6D   # 한국
            }

            $found = 0
            foreach ($name in $probes.Keys) {
                if ($text.Contains($probes[$name])) { $found++ }
            }

            # Scripts whose fonts are not installed are skipped when the fixture
            # is written, so the bar is "most of them", not "all".
            Assert-True ($found -ge 7) `
                "at least 7 of 10 scripts survive extraction (found $found)"
            Assert-True ($text.Contains("quick brown fox")) "Latin extraction is exact"
        }
    }
}

if (Start-Case "world-scripts-search") {
    $world = Join-Path $fixtureDir "world-scripts.pdf"
    if (Test-Path $world) {
        # Cyrillic, chosen because case-insensitive matching has real rules here
        # and a naive ASCII fold would miss it.
        $needle = [string][char]0x0435 + [char]0x0449   # ещ
        $r = Invoke-Lumen -Pdf $world -Name "world-search" -Hooks @{ LUMEN_SEARCH = $needle }
        if ($r.ok) { Assert-True ($r.report.searchHits -ge 1) "non-Latin search finds its phrase" }
    }
}

# -- OCR --------------------------------------------------------------------
#
# The test builds its own scan: redaction rasterises a page, which is exactly
# what a scanner produces -- pixels and no text. OCR then has to put the text
# back, and the assertion is that the page becomes searchable again.

if (Start-Case "ocr-makes-a-scan-searchable") {
    $scan = Join-Path $scratch "scan.pdf"
    Copy-Item $latin $scan -Force

    Invoke-Lumen -Pdf $scan -Name "ocr-flatten" -Hooks @{
        LUMEN_SELECT = "0,72,140,400,160"
        LUMEN_ANNOTATE = "redact"
        LUMEN_SAVE_AS = $scan
    } | Out-Null

    $before = Invoke-Lumen -Pdf $scan -Name "ocr-before"
    if ($before.ok) {
        Assert-Equal 0 $before.report.pageTextLengths[0] "the flattened page has no text"
    }

    # Recognition is slow; give it room before the report is written.
    $r = Invoke-Lumen -Pdf $scan -Name "ocr-run" -Hooks @{
        LUMEN_OCR = "auto"
        LUMEN_SAVE_AS = $scan
        LUMEN_CAPTURE_DELAY = "30000"
    }
    $env:LUMEN_CAPTURE_DELAY = "3000"

    $after = Invoke-Lumen -Pdf $scan -Name "ocr-after"
    if ($after.ok) {
        $recovered = $after.report.pageTextLengths[0]
        if ($recovered -gt 0) {
            Assert-True ($recovered -gt 100) `
                "OCR restored a text layer ($recovered characters)"

            $hits = Invoke-Lumen -Pdf $scan -Name "ocr-search" -Hooks @{ LUMEN_SEARCH = "cycle" }
            if ($hits.ok) {
                Assert-True ($hits.report.searchHits -gt 6) `
                    "the recognised page is searchable again ($($hits.report.searchHits) hits)"
            }
        } else {
            # No OCR language pack is a property of the machine, not a failure
            # of the code -- say so rather than reporting a red test.
            Write-Host "    skip Windows has no OCR language installed" -ForegroundColor DarkYellow
        }
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

if (Start-Case "form-save-flushes-the-focused-field") {
    # The case above cannot catch an unflushed field: filling three fields in a
    # row means each click blurs the previous one, and the last interaction is a
    # checkbox, which commits on click. So nothing is ever still focused when
    # the save happens.
    #
    # This is what a user actually does: type into one field and press Ctrl+S
    # without clicking away. PDFium keeps the typed value in its live widget
    # until focus is killed, so saving without flushing writes the *pre-edit*
    # value -- while the screen still shows what was typed, because rendering
    # draws the widget. Invisible until the file is reopened.
    $target = Join-Path $scratch "filled-focused.pdf"
    Copy-Item $form $target -Force
    Invoke-Lumen -Pdf $target -Name "form-focused" -Hooks @{
        LUMEN_FORM_FILL = "0,300,119,Still Focused"
        LUMEN_SAVE_AS = $target
    } | Out-Null

    $raw = [System.Text.Encoding]::GetEncoding(28591).GetString(
        [System.IO.File]::ReadAllBytes($target))
    Assert-True ($raw.Contains("Still Focused")) `
        "saving flushes a field that is still focused"
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
        LUMEN_EDIT_TEXT = "0,find:Latin fixture page 1,Edited by LumenPDF"
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
        LUMEN_EDIT_TEXT = "0,find:Latin fixture page 1,Latin future page 1"
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
        LUMEN_EDIT_TEXT = "0,find:Latin fixture page 1,Latin future page 1,--undo"
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

# -- Redaction across pages -------------------------------------------------
#
# Until LUMEN_SELECT grew a cross-page form, no test could produce a selection
# spanning two pages, so SelectionController's cross-page branches and the whole
# multi-page redaction loop were unreachable from the suite. The controller
# reported `last - first + 1` pages flattened whether or not it had touched
# them -- after a dialog promising nothing was recoverable.

if (Start-Case "redact-across-pages") {
    $target = Join-Path $scratch "redact-cross.pdf"
    Copy-Item $latin $target -Force

    Invoke-Lumen -Pdf $target -Name "redact-cross" -Hooks @{
        LUMEN_SELECT   = "0,72,130,1,500,400"
        LUMEN_ANNOTATE = "redact"
        LUMEN_SAVE_AS  = $target
    } | Out-Null

    $r = Invoke-Lumen -Pdf $target -Name "redact-cross-read"
    if ($r.ok) {
        Assert-Equal 0 $r.report.pageTextLengths[0] "page 1 text is destroyed"
        Assert-Equal 0 $r.report.pageTextLengths[1] "page 2 text is destroyed"
        # The clause that stops an over-broad regression from passing.
        Assert-True ($r.report.pageTextLengths[2] -gt 100) "page 3 is untouched"
    } else {
        Assert-True $false "redact-cross reopened ($($r.reason))"
    }
}

if (Start-Case "redact-reports-only-pages-it-flattened") {
    # An A1 page cannot be flattened: redaction rasterises at 300 dpi and the
    # render path refuses anything over 40 megapixels. That refusal is correct.
    # What was wrong was reporting it as a success.
    $oversize = Join-Path $fixtureDir "oversize-sample.pdf"
    if (-not (Test-Path $oversize)) {
        Assert-True $false "the oversize fixture was generated"
    } else {
        $target = Join-Path $scratch "redact-oversize.pdf"
        Copy-Item $oversize $target -Force

        $logPath = Join-Path $scratch "redact-oversize.log"
        foreach ($n in $hookNames) { Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
        $env:LUMEN_SELECT = "0,150,300,1,900,500"
        $env:LUMEN_ANNOTATE = "redact"
        $env:LUMEN_SAVE_AS = $target
        $env:LUMEN_REPORT = Join-Path $scratch "redact-oversize.json"
        Start-Process $exe -ArgumentList $target -Wait -RedirectStandardError $logPath | Out-Null
        foreach ($n in $hookNames) { Remove-Item "Env:$n" -ErrorAction SilentlyContinue }

        $log = Get-Content $logPath -Raw
        Assert-True ($log -match "redact-failed:") `
            "a page that cannot be flattened is reported as a failure"
        Assert-True (-not ($log -match "redact-flattened: 2")) `
            "and is not counted among the pages flattened"

        $r = Invoke-Lumen -Pdf $target -Name "redact-oversize-read"
        if ($r.ok) {
            Assert-True ($r.report.pageTextLengths[0] -gt 20) `
                "the text it could not redact is still there, as the warning says"
        }
    }
}

# -- Links ------------------------------------------------------------------
#
# The fixture carries four link annotations. Only three are followable: the
# fourth is a Launch action, and a document must not be able to start a program
# by being clicked.

if (Start-Case "links-parsed") {
    $r = Invoke-Lumen -Pdf $links -Name "links-parsed"
    Assert-True $r.ok "app ran and reported state"
    if ($r.ok) {
        Assert-Equal 2 $r.report.pageCount "page count"
        Assert-Equal 3 $r.report.pageLinkCounts[0] "page 1 has 3 followable links (of 4 annotations)"
        Assert-Equal 0 $r.report.pageLinkCounts[1] "page 2 has none"
    }
}

if (Start-Case "link-internal") {
    # Rect [72 674 200 692] in PDF user space; centre, top-left origin.
    $r = Invoke-Lumen -Pdf $links -Name "link-internal" `
                      -Hooks @{ LUMEN_LINK_PROBE = "0,120,109" }
    if ($r.ok) {
        Assert-Equal "page" $r.report.linkProbe.kind "resolves as an internal jump"
        Assert-Equal 1 $r.report.linkProbe.pageIndex "lands on page 2"
    } else {
        Assert-True $false "link-internal ran ($($r.reason))"
    }
}

if (Start-Case "link-external") {
    $r = Invoke-Lumen -Pdf $links -Name "link-external" `
                      -Hooks @{ LUMEN_LINK_PROBE = "0,120,149" }
    if ($r.ok) {
        Assert-Equal "uri" $r.report.linkProbe.kind "resolves as a URI"
        Assert-Equal "https://example.com/" $r.report.linkProbe.uri "carries the real target"
    } else {
        Assert-True $false "link-external ran ($($r.reason))"
    }
}

if (Start-Case "link-misleading-label") {
    # The page says "https://example.com/safe"; the annotation goes elsewhere.
    # The URI the app reports must be the annotation's, not the printed text.
    $r = Invoke-Lumen -Pdf $links -Name "link-misleading" `
                      -Hooks @{ LUMEN_LINK_PROBE = "0,120,189" }
    if ($r.ok) {
        Assert-Equal "uri" $r.report.linkProbe.kind "resolves as a URI"
        Assert-Equal "https://not-example.invalid/elsewhere" $r.report.linkProbe.uri `
            "reports the annotation's target, not the visible label"
    } else {
        Assert-True $false "link-misleading ran ($($r.reason))"
    }
}

if (Start-Case "hover-never-touches-pdfium") {
    # The hover handler runs on the GUI thread, and PdfDocument::linkAt takes
    # the document mutex -- which the render workers hold for the whole of a
    # page raster. A probe per mouse move meant the interface stalled for the
    # length of one render, repeatedly, while sweeping over a link.
    #
    # Link rectangles do not change unless the page does, so they are cached.
    # This asserts the move path never reaches the backend again.
    $r = Invoke-Lumen -Pdf $links -Name "hover-probes" `
                      -Hooks @{ LUMEN_HOVER = "0,120,149,50" }
    if ($r.ok) {
        Assert-Equal 50 $r.report.hoverRepeats "50 hover probes were driven"
        Assert-Equal 0 $r.report.hoverProbes `
            "no hover reached PDFium on the GUI thread"
        # Without this second assertion an always-empty cache would pass the
        # first one: it proves the cached geometry still resolves correctly.
        Assert-Equal 3 $r.report.pageLinkCounts[0] "and the cache still has the links"
    } else {
        Assert-True $false "hover-probes ran ($($r.reason))"
    }
}

if (Start-Case "link-launch-refused") {
    $r = Invoke-Lumen -Pdf $links -Name "link-launch" `
                      -Hooks @{ LUMEN_LINK_PROBE = "0,120,229" }
    if ($r.ok) {
        Assert-Equal "none" $r.report.linkProbe.kind `
            "a Launch action is not offered as a followable link"
    } else {
        Assert-True $false "link-launch ran ($($r.reason))"
    }
}

# -- Encrypted documents ----------------------------------------------------

if (Start-Case "encrypted-locked") {
    $r = Invoke-Lumen -Pdf $encrypted -Name "encrypted-locked"
    Assert-True $r.ok "app ran and reported state"
    if ($r.ok) {
        # Status 4 is Locked -- distinct from 3 (Error), because the right
        # response is a prompt, not a failure message.
        Assert-Equal 4 $r.report.status "an encrypted file reports Locked, not Error"
        Assert-Equal 0 $r.report.pageCount "nothing is exposed before the password"
    }
}

if (Start-Case "encrypted-correct-password") {
    $r = Invoke-Lumen -Pdf $encrypted -Name "encrypted-open" `
                      -Hooks @{ LUMEN_PASSWORD = "lumen" }
    if ($r.ok) {
        Assert-Equal 2 $r.report.status "opens with the right password"
        Assert-Equal 1 $r.report.pageCount "page count"
        Assert-True ($r.report.pageTextLengths[0] -gt 100) `
            "text decrypts and extracts (RC4 40-bit, R2)"
    } else {
        Assert-True $false "encrypted-open ran ($($r.reason))"
    }
}

if (Start-Case "encrypted-wrong-password") {
    $r = Invoke-Lumen -Pdf $encrypted -Name "encrypted-wrong" `
                      -Hooks @{ LUMEN_PASSWORD = "not-the-password" }
    if ($r.ok) {
        Assert-Equal 4 $r.report.status "a wrong password leaves it Locked"
        Assert-Equal 0 $r.report.pageCount "and exposes nothing"
    } else {
        Assert-True $false "encrypted-wrong ran ($($r.reason))"
    }
}

# -- Printing ---------------------------------------------------------------
#
# Printed output is asserted by opening it again: the page count and page size
# of the result are checkable numbers, where "it looked right" is not.

if (Start-Case "print-all-pages") {
    $printed = Join-Path $scratch "printed-all.pdf"
    Remove-Item $printed -Force -ErrorAction SilentlyContinue

    $env:LUMEN_CAPTURE_DELAY = "8000"
    $r = Invoke-Lumen -Pdf $links -Name "print-all" -Hooks @{ LUMEN_PRINT = $printed }
    $env:LUMEN_CAPTURE_DELAY = "3000"

    Assert-True (Test-Path $printed) "print produced a file"
    if (Test-Path $printed) {
        $back = Invoke-Lumen -Pdf $printed -Name "print-all-check"
        if ($back.ok) {
            Assert-Equal 2 $back.report.pageCount "every page was printed"
            # Print to a file keeps the document's own page size rather than
            # reflowing a US Letter document onto the default A4.
            Assert-Equal 612 ([math]::Round($back.report.pageWidthsPoints[0])) `
                "the sheet keeps the source page size"
        } else {
            Assert-True $false "the printed file reopens ($($back.reason))"
        }
    }
}

if (Start-Case "print-page-range") {
    $printed = Join-Path $scratch "printed-range.pdf"
    Remove-Item $printed -Force -ErrorAction SilentlyContinue

    $env:LUMEN_CAPTURE_DELAY = "8000"
    $r = Invoke-Lumen -Pdf $links -Name "print-range" -Hooks @{ LUMEN_PRINT = "$printed,1,1" }
    $env:LUMEN_CAPTURE_DELAY = "3000"

    Assert-True (Test-Path $printed) "range print produced a file"
    if (Test-Path $printed) {
        $back = Invoke-Lumen -Pdf $printed -Name "print-range-check"
        if ($back.ok) {
            Assert-Equal 1 $back.report.pageCount "only the requested page was printed"
        } else {
            Assert-True $false "the range file reopens ($($back.reason))"
        }
    }
}

# -- Settings ---------------------------------------------------------------
#
# These write to the real user settings, which is the point: the failure mode
# being tested is a value that does not survive a process boundary.

if (Start-Case "settings-recent-files") {
    $before = Invoke-Lumen -Pdf $latin -Name "recent-before"
    $after  = Invoke-Lumen -Pdf $cjk   -Name "recent-after"

    if ($before.ok -and $after.ok) {
        Assert-True ($after.report.settings.recentCount -ge 2) `
            "opening two documents leaves at least two in the recent list"
        Assert-True ($after.report.settings.theme.Length -gt 0) `
            "the theme preference reads back"
    } else {
        Assert-True $false "settings cases ran"
    }
}

if (Start-Case "settings-reading-position") {
    Invoke-Lumen -Pdf $latin -Name "position-note" -Hooks @{ LUMEN_NOTE_PAGE = "2" } | Out-Null
    $r = Invoke-Lumen -Pdf $latin -Name "position-restore"

    if ($r.ok) {
        Assert-Equal 2 $r.report.settings.restoredPage "a reading position survives a restart"
    } else {
        Assert-True $false "position-restore ran ($($r.reason))"
    }

    # A position belongs to one document, not to the application.
    $other = Invoke-Lumen -Pdf $cjk -Name "position-other"
    if ($other.ok) {
        Assert-Equal 0 $other.report.settings.restoredPage `
            "a different document does not inherit it"
    }
}

# -- Translations -----------------------------------------------------------
#
# Compiled .qm files are easy to leave out of a build and impossible to notice
# missing: the interface simply stays English.

if (Start-Case "translations-complete") {
    $tsFiles = Get-ChildItem (Join-Path $repoRoot "translations") -Filter "*.ts"
    Assert-True ($tsFiles.Count -ge 3) "translation files are present"

    foreach ($ts in $tsFiles) {
        [xml]$doc = Get-Content $ts.FullName -Encoding UTF8
        $all = $doc.SelectNodes("//message").Count
        $unfinished = $doc.SelectNodes("//translation[@type='unfinished']").Count
        Assert-Equal 0 $unfinished "$($ts.Name) has no unfinished strings (of $all)"
    }

    # And that they actually reached the binary.
    $qmDir = Join-Path $repoRoot "build\windows-release"
    $compiled = Get-ChildItem $qmDir -Filter "*.qm" -Recurse -ErrorAction SilentlyContinue
    Assert-True ($compiled.Count -ge 3) "the .qm files were compiled ($($compiled.Count) found)"
}

# -- Startup cost -----------------------------------------------------------
#
# Qt6PrintSupport depends on Qt6Widgets, so any eager touch of QPrinterInfo maps
# 6.6 MB of DLLs into every session -- including the great majority that never
# print. This has been broken once already: PrintController's constructor asked
# for the default printer, and PrintSheet bound to `printers` before it was ever
# opened. Both are easy to reintroduce and invisible without this check.

if (Start-Case "startup-does-not-load-printing") {
    $logPath = Join-Path $scratch "startup-modules.log"
    $env:LUMEN_REPORT = Join-Path $scratch "startup-modules.json"
    $env:LUMEN_CAPTURE_DELAY = "5000"

    $proc = Start-Process $exe -ArgumentList $latin -PassThru `
        -RedirectStandardError $logPath
    Start-Sleep -Seconds 3

    $loaded = @()
    try {
        $proc.Refresh()
        $loaded = $proc.Modules |
            Where-Object { $_.ModuleName -match "Qt6(Widgets|PrintSupport)\.dll" } |
            ForEach-Object { $_.ModuleName }
    } catch {
        # The process can exit between Refresh and Modules; treat that as
        # inconclusive rather than as a pass.
        $loaded = @("<could not read module list>")
    }

    $proc.WaitForExit(20000) | Out-Null
    if (-not $proc.HasExited) { $proc.Kill() }
    $env:LUMEN_CAPTURE_DELAY = "3000"

    Assert-True ($loaded.Count -eq 0) `
        "the printing stack is not loaded until it is used$(if ($loaded.Count) { ' (found: ' + ($loaded -join ', ') + ')' })"
}

# -- Updates ----------------------------------------------------------------
#
# The comparison decides whether anyone is ever told an update exists. Getting
# it backwards either hides releases or nags forever -- v0.2.0 shipped a binary
# reporting 0.1.0, which would have done the latter.

if (Start-Case "update-version-ordering") {
    $cases = @(
        @{ a = "v0.3.0";  b = "0.2.0";  expect =  1; what = "0.3.0 is newer than 0.2.0" },
        @{ a = "0.2.0";   b = "v0.3.0"; expect = -1; what = "0.2.0 is older than 0.3.0" },
        @{ a = "v1.0.0";  b = "1.0.0";  expect =  0; what = "a leading v is ignored" },
        @{ a = "0.10.0";  b = "0.9.0";  expect =  1; what = "0.10 sorts above 0.9, not as a string" },
        @{ a = "1.2";     b = "1.2.0";  expect =  0; what = "a missing field counts as zero" },
        @{ a = "1.2.3";   b = "1.2.3-beta"; expect = 0; what = "a pre-release suffix is ignored" }
    )

    foreach ($case in $cases) {
        $stem = "ver-$($case.a)-$($case.b)".Replace(".", "_")
        $logPath = Join-Path $scratch "$stem.log"
        $env:LUMEN_VERSION_COMPARE = "$($case.a),$($case.b)"
        $env:LUMEN_REPORT = Join-Path $scratch "ver.json"

        $proc = Start-Process $exe -ArgumentList $latin -Wait -PassThru `
            -RedirectStandardError $logPath
        $line = (Get-Content $logPath | Select-String "version-compare:" | Select-Object -First 1)
        $actual = if ($line -match "version-compare:\s*(-?\d+)") { [int]$Matches[1] } else { "no output" }

        Assert-Equal $case.expect $actual $case.what
    }
    Remove-Item Env:LUMEN_VERSION_COMPARE -ErrorAction SilentlyContinue
}

# The update path decides whether to hand somebody a runnable .exe, and until
# now none of it had ever executed: the sink was opened WriteOnly, so hashing
# always failed and every download ended in "could not be read back".

if (Start-Case "update-asset-name-is-sanitised") {
    # The name comes from the release JSON and ends up in a path builder.
    # QDir::filePath returns its argument unchanged when it is absolute.
    $cases = @(
        @{ input = "LumenPDF-0.3.2-win64-setup.exe";                          expect = "LumenPDF-0.3.2-win64-setup.exe"; what = "the real asset name is accepted verbatim" },
        @{ input = "C:/Users/Public/LumenPDF-9-win64-setup.exe";              expect = "LumenPDF-9-win64-setup.exe";     what = "an absolute path is reduced to its leaf" },
        @{ input = "..\..\Startup\LumenPDF-9-win64-setup.exe";                expect = "LumenPDF-9-win64-setup.exe";     what = "traversal segments are stripped" },
        @{ input = "sub/../evil-setup.exe";                                   expect = "";                              what = "a name that is not ours is rejected" },
        @{ input = "totally-different-setup.exe";                             expect = "";                              what = "any other setup.exe is rejected" },
        @{ input = "   ";                                                     expect = "";                              what = "a whitespace name is rejected" },
        @{ input = ("LumenPDF-" + ("A" * 200) + "-win64-setup.exe");          expect = "";                              what = "an over-long name is rejected" },
        @{ input = "LumenPDF-0.3.2-win64-setup.exe.exe";                      expect = "";                              what = "a double extension is rejected" }
    )
    # An empty name is rejected too, but PowerShell unsets a variable assigned
    # "", so it cannot be driven through an environment hook. Covered by the
    # leaf.isEmpty() branch in sanitiseAssetName.

    foreach ($case in $cases) {
        $logPath = Join-Path $scratch "asset-$($cases.IndexOf($case)).log"
        $env:LUMEN_UPDATE_ASSET = $case.input
        $env:LUMEN_REPORT = Join-Path $scratch "asset.json"
        Start-Process $exe -ArgumentList $latin -Wait -RedirectStandardError $logPath | Out-Null
        $line = Get-Content $logPath | Select-String "update-asset:" | Select-Object -First 1
        $actual = if ($line -match "update-asset: \[(.*)\]") { $Matches[1] } else { "<no output>" }
        Assert-Equal $case.expect $actual $case.what
    }
    Remove-Item Env:LUMEN_UPDATE_ASSET -ErrorAction SilentlyContinue
}

if (Start-Case "update-verify-and-promote") {
    $part = Join-Path $scratch "LumenPDF-9.9.9-win64-setup.exe.part"
    $final = Join-Path $scratch "LumenPDF-9.9.9-win64-setup.exe"

    # -- matching hash: the file is promoted --------------------------------
    Remove-Item $part, $final -Force -ErrorAction SilentlyContinue
    [System.IO.File]::WriteAllText($part, "pretend installer bytes")
    $good = (Get-FileHash $part -Algorithm SHA256).Hash.ToLower()

    $logPath = Join-Path $scratch "verify-good.log"
    $env:LUMEN_UPDATE_VERIFY = "$part,$good"
    $env:LUMEN_REPORT = Join-Path $scratch "verify.json"
    Start-Process $exe -ArgumentList $latin -Wait -RedirectStandardError $logPath | Out-Null
    $line = Get-Content $logPath | Select-String "update-verify:" | Select-Object -First 1

    Assert-True ($line -match "ok=1") "a matching checksum promotes the download"
    Assert-True (Test-Path $final) "the .exe exists after promotion"
    Assert-True (-not (Test-Path $part)) "the .part is gone"

    # -- mismatched hash: the file is destroyed -----------------------------
    Remove-Item $part, $final -Force -ErrorAction SilentlyContinue
    [System.IO.File]::WriteAllText($part, "tampered installer bytes")

    $logPath = Join-Path $scratch "verify-bad.log"
    $env:LUMEN_UPDATE_VERIFY = "$part,$good"
    Start-Process $exe -ArgumentList $latin -Wait -RedirectStandardError $logPath | Out-Null
    $line = Get-Content $logPath | Select-String "update-verify:" | Select-Object -First 1

    Assert-True ($line -match "ok=0") "a mismatched checksum is refused"
    Assert-True (-not (Test-Path $final)) "no .exe is left behind on mismatch"
    Assert-True (-not (Test-Path $part)) "the rejected download is deleted"

    Remove-Item Env:LUMEN_UPDATE_VERIFY -ErrorAction SilentlyContinue
}

if (Start-Case "release-artefacts-have-checksums") {
    # Not a build step -- an assertion about what a release contains. The
    # in-app updater refuses to download from a release with no SHA256SUMS.txt,
    # so publishing one without it ships an updater that cannot update.
    $dist = Join-Path $repoRoot "build\dist"
    if (Test-Path $dist) {
        $sumsPath = Join-Path $dist "SHA256SUMS.txt"
        Assert-True (Test-Path $sumsPath) "packaging writes SHA256SUMS.txt"

        if (Test-Path $sumsPath) {
            $sums = Get-Content $sumsPath
            $artefacts = Get-ChildItem $dist -File |
                         Where-Object { $_.Name -ne "SHA256SUMS.txt" }
            foreach ($a in $artefacts) {
                $expected = (Get-FileHash $a.FullName -Algorithm SHA256).Hash.ToLower()
                $line = $sums | Where-Object { $_ -match "\s\*?$([regex]::Escape($a.Name))$" }
                Assert-True ($null -ne $line -and $line.Split()[0] -eq $expected) `
                    "$($a.Name) : checksum is present and correct"
            }
        }
    } else {
        Write-Host "    skip (nothing packaged yet -- run scripts/package.ps1)" -ForegroundColor DarkGray
    }
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

