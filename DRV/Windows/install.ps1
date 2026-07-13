#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [switch]$StageOnly
)

$ErrorActionPreference = 'Stop'
$package = Join-Path $PSScriptRoot 'x64\Release\timecard\timecard.inf'
$log = Join-Path $PSScriptRoot 'install.log'

Start-Transcript -Path $log -Force | Out-Null
try {
    if (-not (Test-Path -LiteralPath $package)) {
        throw "Driver package not found: $package. Run build.cmd first."
    }

    $secureBoot = Get-ItemPropertyValue `
        'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' `
        -Name UEFISecureBootEnabled -ErrorAction SilentlyContinue
    if ($secureBoot -eq 1) {
        throw 'Secure Boot is enabled. A local test-signed driver cannot load.'
    }

    Write-Host 'Enabling Windows test-signing for the next boot...'
    & bcdedit.exe /set testsigning on
    if ($LASTEXITCODE -ne 0) {
        throw "bcdedit failed with exit code $LASTEXITCODE"
    }

    Write-Host 'Staging the TimeCard driver package...'
    if ($StageOnly) {
        & pnputil.exe /add-driver $package
    } else {
        & pnputil.exe /add-driver $package /install
    }
    if ($LASTEXITCODE -ne 0) {
        throw "pnputil failed with exit code $LASTEXITCODE"
    }

    Write-Host ''
    Write-Host 'Driver package staged. Reboot is required before the test-signed'
    Write-Host 'kernel driver can load. After reboot run verify.ps1 as Administrator.'
}
finally {
    Stop-Transcript | Out-Null
}
