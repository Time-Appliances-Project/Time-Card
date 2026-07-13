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

    $installedPackages = (& pnputil.exe /enum-drivers 2>&1 | Out-String)
    $unsafeClassGuid = '{49842651-EF23-47D4-BDF6-017A675C87AD}'
    $unsafePackages = @($installedPackages -split
        '(?m)(?=^Published Name:)' | Where-Object {
            $_ -match "(?im)^Class GUID:\s+$([regex]::Escape($unsafeClassGuid))\s*$"
        })
    if ($unsafePackages.Count -ne 0) {
        throw "The legacy unsafe Time Card class $unsafeClassGuid is still staged. Run rollback.ps1 as Administrator before installing another build."
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
