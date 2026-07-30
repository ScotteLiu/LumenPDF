<#
.SYNOPSIS
    Writes deliberately broken and hostile PDFs, to check the app fails safely.

.DESCRIPTION
    Every PDF tested so far has been well-formed, because every one of them was
    produced by a working PDF writer. Real files are not like that: they arrive
    truncated by a failed download, corrupted by a bad transfer, encrypted,
    empty, or crafted to trip a parser.

    None of these should crash the app. Most should be refused with a message
    the user can act on. What matters is that the failure is a refusal, not an
    access violation.

    The files are generated rather than committed so the repository contains no
    opaque binaries, and so what each one breaks is written down in one place.

.EXAMPLE
    ./scripts/make-hostile-fixtures.ps1
#>

[CmdletBinding()]
param(
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot "tests\fixtures\hostile" }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$enc = [System.Text.Encoding]::GetEncoding(28591)
$sourcePdf = Join-Path $repoRoot "tests\fixtures\latin-sample.pdf"
if (-not (Test-Path $sourcePdf)) {
    throw "latin-sample.pdf is missing -- generate the normal fixtures first"
}
$sourceBytes = [System.IO.File]::ReadAllBytes($sourcePdf)

function Write-Fixture([string]$Name, [byte[]]$Bytes, [string]$Breaks) {
    $path = Join-Path $OutputDir $Name
    [System.IO.File]::WriteAllBytes($path, $Bytes)
    Write-Host ("  {0,-26} {1,8} bytes   {2}" -f $Name, $Bytes.Length, $Breaks)
}

Write-Host "Hostile fixtures:" -ForegroundColor Cyan

# Nothing at all. A zero-byte file is what an interrupted download leaves.
Write-Fixture "empty.pdf" @() "zero bytes"

# Right extension, wrong contents entirely.
Write-Fixture "not-a-pdf.pdf" ($enc.GetBytes("This is plain text, not a PDF at all.`n" * 40)) `
    "valid file, no PDF structure"

# Header only: the parser sees a PDF and then runs out of file.
Write-Fixture "header-only.pdf" ($enc.GetBytes("%PDF-1.7`n")) "header with no body"

# Cut in half. The xref table at the end is gone, so the document has to be
# reconstructed by scanning -- or refused.
$half = New-Object byte[] ([int]($sourceBytes.Length / 2))
[Array]::Copy($sourceBytes, $half, $half.Length)
Write-Fixture "truncated.pdf" $half "cut in half, xref missing"

# Only the last 400 bytes: a trailer pointing at objects that are not there.
$tailLength = [Math]::Min(400, $sourceBytes.Length)
$tail = New-Object byte[] $tailLength
[Array]::Copy($sourceBytes, $sourceBytes.Length - $tailLength, $tail, 0, $tailLength)
Write-Fixture "trailer-only.pdf" $tail "trailer pointing at nothing"

# Byte rot in the middle of the body, as a bad transfer produces.
$corrupt = $sourceBytes.Clone()
$rng = [System.Random]::new(20260730)   # fixed seed: the same file every run
for ($i = 0; $i -lt 200; $i++) {
    $at = $rng.Next([int]($corrupt.Length * 0.2), [int]($corrupt.Length * 0.8))
    $corrupt[$at] = [byte]$rng.Next(0, 256)
}
Write-Fixture "corrupted-body.pdf" $corrupt "200 random bytes overwritten"

# The xref offset points past the end of the file.
$badXref = $enc.GetString($sourceBytes)
$badXref = $badXref -replace '(?s)startxref\s*\r?\n\d+', "startxref`n999999999"
Write-Fixture "bad-startxref.pdf" ($enc.GetBytes($badXref)) "startxref past end of file"

# A page tree that points at itself. A naive walker recurses until it dies.
$loop = @"
%PDF-1.7
1 0 obj
<< /Type /Catalog /Pages 2 0 R >>
endobj
2 0 obj
<< /Type /Pages /Kids [2 0 R 3 0 R] /Count 2 >>
endobj
3 0 obj
<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>
endobj
trailer
<< /Size 4 /Root 1 0 R >>
%%EOF
"@
Write-Fixture "recursive-pages.pdf" ($enc.GetBytes($loop)) "page tree contains itself"

# An outline whose entries form a cycle -- the exact shape buildOutline's
# `seen` set exists to survive.
$cyclicOutline = @"
%PDF-1.7
1 0 obj
<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>
endobj
2 0 obj
<< /Type /Pages /Kids [3 0 R] /Count 1 >>
endobj
3 0 obj
<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>
endobj
4 0 obj
<< /Type /Outlines /First 5 0 R /Last 5 0 R /Count 1 >>
endobj
5 0 obj
<< /Title (loop) /Parent 4 0 R /First 5 0 R /Next 5 0 R /Dest [3 0 R /Fit] >>
endobj
trailer
<< /Size 6 /Root 1 0 R >>
%%EOF
"@
Write-Fixture "cyclic-outline.pdf" ($enc.GetBytes($cyclicOutline)) "outline entry is its own child and sibling"

# Claims a page is astronomically large. Rendering it naively asks for a
# bitmap of several terabytes.
$hugePage = @"
%PDF-1.7
1 0 obj
<< /Type /Catalog /Pages 2 0 R >>
endobj
2 0 obj
<< /Type /Pages /Kids [3 0 R] /Count 1 >>
endobj
3 0 obj
<< /Type /Page /Parent 2 0 R /MediaBox [0 0 14400000 14400000] >>
endobj
trailer
<< /Size 4 /Root 1 0 R >>
%%EOF
"@
Write-Fixture "huge-mediabox.pdf" ($enc.GetBytes($hugePage)) "page declared 200000 inches square"

Write-Host ""
Write-Host "Wrote to $OutputDir" -ForegroundColor Green
