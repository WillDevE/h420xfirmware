# Downloads the CI-built firmware images from the latest GitHub release and
# verifies each one against SHA256SUMS.txt. Nothing is flashed and no tablet
# access is performed.
#
# Usage: get-firmware.bat                              (downloads both images)
#        get-firmware.bat v1.0.0                        (a specific release tag)
#        get-firmware.bat -Name rhythm1015-wip.bin      (only the WIP image)

param(
    [string]$Tag = 'latest',
    [string]$Name = ''
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Fail([string]$Message) {
    Write-Host ''
    Write-Host "STOPPED: $Message" -ForegroundColor Red
    exit 1
}

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$explicitName = [bool]$Name
$names = if ($Name) { @($Name) } else { @('rhythm1000.bin', 'rhythm1015-wip.bin') }

$sumsPath = Join-Path $Here 'SHA256SUMS.txt'
if (-not (Test-Path -LiteralPath $sumsPath)) {
    Fail 'SHA256SUMS.txt was not found next to this script.'
}
$listed = @{}
foreach ($line in Get-Content -LiteralPath $sumsPath) {
    if ($line -match '^([0-9a-fA-F]{64})\s+\*?(\S+)$') {
        $listed[$Matches[2]] = $Matches[1].ToUpperInvariant()
    }
}

Write-Host ''
Write-Host 'H420X firmware download' -ForegroundColor Cyan
Write-Host ''

foreach ($image in $names) {
    if (-not $listed.ContainsKey($image)) {
        Fail "SHA256SUMS.txt has no $image entry."
    }
    $expected = $listed[$image]
    $outFile = Join-Path $Here "firmware\$image"

    if ($Tag -eq 'latest') {
        $url = "https://github.com/WillDevE/h420xfirmware/releases/latest/download/$image"
    } else {
        $url = "https://github.com/WillDevE/h420xfirmware/releases/download/$Tag/$image"
    }

    Write-Host "  $url"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outFile) | Out-Null
    $tmp = "$outFile.download"

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
        if (-not $explicitName -and $image -ne 'rhythm1000.bin') {
            # Older releases (v1.0.1 and earlier) do not have the WIP asset.
            Write-Host "  $image is not present in this release; skipping." -ForegroundColor Yellow
            Write-Host ''
            continue
        }
        Fail ("Download failed. Download $image manually from " +
              "https://github.com/WillDevE/h420xfirmware/releases and put it in firmware\.")
    }

    $got = (Get-FileHash -Algorithm SHA256 -Path $tmp).Hash.ToUpperInvariant()
    if ($got -ne $expected) {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        Fail "Downloaded $image SHA-256 $got does not match SHA256SUMS.txt ($expected). Nothing was kept."
    }

    Move-Item -Force -LiteralPath $tmp -Destination $outFile
    Write-Host "Downloaded and verified: $outFile" -ForegroundColor Green
    Write-Host "SHA-256: $got"
    Write-Host ''
}

Write-Host 'Next: run flash.bat and choose 1 (stable) or 2 (WIP).' -ForegroundColor Cyan
