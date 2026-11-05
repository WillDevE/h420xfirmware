# Downloads the CI-built firmware image from the latest GitHub release and
# verifies it against SHA256SUMS.txt. Nothing is flashed and no tablet access
# is performed.
#
# Usage: get-firmware.bat            (latest release)
#        get-firmware.bat v1.0.0     (a specific release tag)

param(
    [string]$Tag = 'latest',
    [string]$OutFile = ''
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Fail([string]$Message) {
    Write-Host ''
    Write-Host "STOPPED: $Message" -ForegroundColor Red
    exit 1
}

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutFile) { $OutFile = Join-Path $Here 'firmware\rhythm1000.bin' }

$sumsPath = Join-Path $Here 'SHA256SUMS.txt'
if (-not (Test-Path -LiteralPath $sumsPath)) {
    Fail 'SHA256SUMS.txt was not found next to this script.'
}
$expected = $null
foreach ($line in Get-Content -LiteralPath $sumsPath) {
    if ($line -match '^([0-9a-fA-F]{64})\s+\*?(\S+)$' -and $Matches[2] -eq 'rhythm1000.bin') {
        $expected = $Matches[1].ToUpperInvariant()
    }
}
if (-not $expected) {
    Fail 'SHA256SUMS.txt has no rhythm1000.bin entry.'
}

if ($Tag -eq 'latest') {
    $url = 'https://github.com/WillDevE/h420xfirmware/releases/latest/download/rhythm1000.bin'
} else {
    $url = "https://github.com/WillDevE/h420xfirmware/releases/download/$Tag/rhythm1000.bin"
}

Write-Host ''
Write-Host 'H420X firmware download' -ForegroundColor Cyan
Write-Host "  $url"
Write-Host ''

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutFile) | Out-Null
$tmp = "$OutFile.download"

$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
if ($curl) {
    & $curl.Source -L --fail --silent --show-error -o $tmp $url
    $downloadOk = ($LASTEXITCODE -eq 0)
} else {
    try {
        Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing -TimeoutSec 120
        $downloadOk = $true
    } catch {
        $downloadOk = $false
    }
}
if (-not $downloadOk) {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    Fail ("Download failed. Download rhythm1000.bin manually from " +
          "https://github.com/WillDevE/h420xfirmware/releases and put it in firmware\.")
}

$got = (Get-FileHash -Algorithm SHA256 -Path $tmp).Hash.ToUpperInvariant()
if ($got -ne $expected) {
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    Fail "Downloaded image SHA-256 $got does not match SHA256SUMS.txt ($expected). Nothing was kept."
}

Move-Item -Force -LiteralPath $tmp -Destination $OutFile
Write-Host "Downloaded and verified: $OutFile" -ForegroundColor Green
Write-Host "SHA-256: $got"
Write-Host ''
Write-Host 'Next: run flash.bat and choose 1.' -ForegroundColor Cyan
