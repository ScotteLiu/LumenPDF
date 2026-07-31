<#
.SYNOPSIS
    Writes a password-protected PDF used to test the unlock path.

.DESCRIPTION
    Implements PDF 1.4's standard security handler (revision 2, 40-bit RC4)
    directly, because there is no library on this machine that produces one and
    a downloaded encrypted PDF would be an opaque binary in the repository.

    Revision 2 is the weakest thing the specification defines, which is exactly
    what is wanted here: the fixture exists to prove that "locked" is detected,
    that a correct password opens it and a wrong one does not. It is not a
    demonstration of how to encrypt anything, and LumenPDF never writes files
    like this -- it only reads them.

    The algorithms are numbered as in the specification:
      Algorithm 2 -- derive the file encryption key
      Algorithm 3 -- the /O entry, from the owner password
      Algorithm 4 -- the /U entry, for revision 2
      Algorithm 1 -- per-object keys for each string and stream

.EXAMPLE
    ./scripts/make-encrypted-fixture.ps1 -UserPassword lumen
#>

[CmdletBinding()]
param(
    [string]$OutputPath = "",
    [string]$UserPassword = "lumen",
    [string]$OwnerPassword = "lumen-owner"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) {
    $OutputPath = Join-Path $repoRoot "tests\fixtures\encrypted.pdf"
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null

$enc = [System.Text.Encoding]::GetEncoding(28591)

# The 32-byte padding string from the specification. Every password is padded
# or truncated to exactly 32 bytes with it before use.
$PAD = [byte[]]@(
    0x28,0xBF,0x4E,0x5E,0x4E,0x75,0x8A,0x41,0x64,0x00,0x4E,0x56,0xFF,0xFA,0x01,0x08,
    0x2E,0x2E,0x00,0xB6,0xD0,0x68,0x3E,0x80,0x2F,0x0C,0xA9,0xFE,0x64,0x53,0x69,0x7A)

function Get-PaddedPassword([string]$password) {
    $bytes = $enc.GetBytes($password)
    $out = [byte[]]::new(32)
    $take = [Math]::Min(32, $bytes.Length)
    if ($take -gt 0) { [Array]::Copy($bytes, 0, $out, 0, $take) }
    if ($take -lt 32) { [Array]::Copy($PAD, 0, $out, $take, 32 - $take) }
    return $out
}

function Get-Md5([byte[]]$data) {
    $md5 = [System.Security.Cryptography.MD5]::Create()
    try { return $md5.ComputeHash($data) } finally { $md5.Dispose() }
}

function Join-Bytes([byte[][]]$parts) {
    $total = 0
    foreach ($p in $parts) { $total += $p.Length }
    $out = [byte[]]::new($total)
    $at = 0
    foreach ($p in $parts) {
        [Array]::Copy($p, 0, $out, $at, $p.Length)
        $at += $p.Length
    }
    return $out
}

# RC4. Twenty lines, and the only reason it is here is that .NET removed it.
function Invoke-Rc4([byte[]]$key, [byte[]]$data) {
    $s = [byte[]]::new(256)
    for ($i = 0; $i -lt 256; $i++) { $s[$i] = [byte]$i }

    $j = 0
    for ($i = 0; $i -lt 256; $i++) {
        $j = ($j + $s[$i] + $key[$i % $key.Length]) -band 0xFF
        $t = $s[$i]; $s[$i] = $s[$j]; $s[$j] = $t
    }

    $out = [byte[]]::new($data.Length)
    $i = 0; $j = 0
    for ($n = 0; $n -lt $data.Length; $n++) {
        $i = ($i + 1) -band 0xFF
        $j = ($j + $s[$i]) -band 0xFF
        $t = $s[$i]; $s[$i] = $s[$j]; $s[$j] = $t
        $out[$n] = $data[$n] -bxor $s[($s[$i] + $s[$j]) -band 0xFF]
    }
    return $out
}

# -- Key material -----------------------------------------------------------

# A fixed ID makes the output byte-for-byte reproducible, which matters more
# for a fixture than unpredictability does.
$fileId = [byte[]]@(0x4C,0x75,0x6D,0x65,0x6E,0x50,0x44,0x46,
                    0x46,0x69,0x78,0x74,0x75,0x72,0x65,0x31)

# /P: permissions. -44 clears "modify" and "annotate" while leaving printing
# and copying on -- so the file is genuinely restricted, not just locked.
$permissions = -44

$keyLength = 5   # 40 bits, which is what revision 2 means

# Algorithm 3: /O
$oKey = (Get-Md5 (Get-PaddedPassword $OwnerPassword))[0..($keyLength - 1)]
$oEntry = Invoke-Rc4 $oKey (Get-PaddedPassword $UserPassword)

# Algorithm 2: the file encryption key
$pBytes = [System.BitConverter]::GetBytes([int]$permissions)   # little-endian
if (-not [System.BitConverter]::IsLittleEndian) { [Array]::Reverse($pBytes) }

$keyInput = Join-Bytes @((Get-PaddedPassword $UserPassword), $oEntry, $pBytes, $fileId)
$fileKey = (Get-Md5 $keyInput)[0..($keyLength - 1)]

# Algorithm 4: /U for revision 2
$uEntry = Invoke-Rc4 $fileKey $PAD

# Algorithm 1: the key for one object
function Get-ObjectKey([int]$objectNumber, [int]$generation) {
    $extra = [byte[]]@(
        ($objectNumber -band 0xFF),
        (($objectNumber -shr 8) -band 0xFF),
        (($objectNumber -shr 16) -band 0xFF),
        ($generation -band 0xFF),
        (($generation -shr 8) -band 0xFF))

    $digest = Get-Md5 (Join-Bytes @([byte[]]$fileKey, $extra))
    $take = [Math]::Min($fileKey.Length + 5, 16)
    return $digest[0..($take - 1)]
}

# PDF hex strings, used for /O and /U so no byte needs escaping.
function ConvertTo-HexString([byte[]]$bytes) {
    $sb = [System.Text.StringBuilder]::new()
    foreach ($b in $bytes) { [void]$sb.Append($b.ToString("X2")) }
    return "<" + $sb.ToString() + ">"
}

# -- Objects ----------------------------------------------------------------
#
# Object numbers are fixed here rather than allocated, because the content
# stream has to be encrypted with a key derived from its own object number.

$contentText = @"
BT /Helv 18 Tf 72 720 Td (LumenPDF encrypted fixture) Tj ET
BT /Helv 12 Tf 72 680 Td (This document was opened with the correct password.) Tj ET
BT /Helv 12 Tf 72 650 Td (Encryption: RC4 40-bit, standard security handler R2.) Tj ET
BT /Helv 9 Tf 72 610 Td (Generated by scripts/make-encrypted-fixture.ps1) Tj ET
"@

$contentBytes = $enc.GetBytes($contentText)
$encryptedContent = Invoke-Rc4 (Get-ObjectKey 6 0) $contentBytes

$objects = @(
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /Helv 4 0 R >> >> /Contents 6 0 R >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
    ("<< /Filter /Standard /V 1 /R 2 /O {0} /U {1} /P {2} >>" -f
        (ConvertTo-HexString $oEntry), (ConvertTo-HexString $uEntry), $permissions),
    $null   # 6: the content stream, written as raw bytes below
)

# -- Assemble ---------------------------------------------------------------
$out = [System.IO.MemoryStream]::new()
function Write-Text([string]$text) {
    $bytes = $enc.GetBytes($text)
    $out.Write($bytes, 0, $bytes.Length)
}
function Write-Raw([byte[]]$bytes) { $out.Write($bytes, 0, $bytes.Length) }

Write-Text "%PDF-1.4`n"
Write-Raw ([byte[]]@(0x25,0xE2,0xE3,0xCF,0xD3,0x0A))

$offsets = @()
for ($i = 0; $i -lt $objects.Count; $i++) {
    $number = $i + 1
    $offsets += [int]$out.Length

    if ($number -eq 6) {
        Write-Text "6 0 obj`n<< /Length $($encryptedContent.Length) >>`nstream`n"
        Write-Raw $encryptedContent
        Write-Text "`nendstream`nendobj`n"
    } else {
        Write-Text "$number 0 obj`n$($objects[$i])`nendobj`n"
    }
}

$xrefOffset = [int]$out.Length
Write-Text "xref`n0 $($objects.Count + 1)`n"
Write-Text "0000000000 65535 f `n"
foreach ($offset in $offsets) {
    Write-Text ("{0:D10} 00000 n `n" -f $offset)
}

# /Encrypt and /ID both live in the trailer, and the ID must match the one the
# key was derived from or nothing decrypts.
$idHex = ConvertTo-HexString $fileId
Write-Text "trailer`n<< /Size $($objects.Count + 1) /Root 1 0 R /Encrypt 5 0 R /ID [$idHex $idHex] >>`n"
Write-Text "startxref`n$xrefOffset`n%%EOF`n"

[System.IO.File]::WriteAllBytes($OutputPath, $out.ToArray())
$out.Dispose()

Write-Host "Wrote $OutputPath ($((Get-Item $OutputPath).Length) bytes)" -ForegroundColor Green
Write-Host "User password:  $UserPassword"
Write-Host "Owner password: $OwnerPassword"
