<#
.SYNOPSIS
    Rescans the source for translatable strings and updates translations/*.ts.

.DESCRIPTION
    Run this after adding or changing any qsTr() or tr() string. It only touches
    the .ts files; the .qm files are built by CMake.

    Deliberately not part of the build. Regenerating on every configure means
    every unrelated commit carries .ts churn, and a real translation change
    becomes impossible to see in a diff.
#>
[CmdletBinding()]
param(
    [string]$QtDir = "C:\Qt\6.8.3\msvc2022_64"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$lupdate = Join-Path $QtDir "bin\lupdate.exe"

if (-not (Test-Path $lupdate)) {
    throw "lupdate not found at $lupdate -- pass -QtDir"
}

$tsFiles = Get-ChildItem (Join-Path $root "translations") -Filter "*.ts" |
    ForEach-Object { $_.FullName }

if (-not $tsFiles) {
    throw "No .ts files in translations/. Create the empty stubs first."
}

Push-Location $root
try {
    & $lupdate `
        "src" "qml" `
        -no-obsolete `
        -locations none `
        -ts @tsFiles

    if ($LASTEXITCODE -ne 0) { throw "lupdate failed ($LASTEXITCODE)" }
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "Updated:" -ForegroundColor Green
foreach ($ts in $tsFiles) {
    [xml]$doc = Get-Content $ts -Encoding UTF8
    $all = $doc.SelectNodes("//message").Count
    $done = $doc.SelectNodes("//message[not(translation/@type='unfinished')]").Count
    $name = Split-Path $ts -Leaf
    Write-Host ("  {0,-28} {1,4}/{2,-4} translated" -f $name, $done, $all)
}
