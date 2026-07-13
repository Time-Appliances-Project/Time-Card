#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [string]$PublishedName = 'oem226.inf',
    [string]$ExpectedClassGuid = '{49842651-EF23-47D4-BDF6-017A675C87AD}',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$log = Join-Path $PSScriptRoot 'rollback.log'

Start-Transcript -Path $log -Force | Out-Null
try {
    $installedPackages = (& pnputil.exe /enum-drivers 2>&1 | Out-String)
    $package = @($installedPackages -split
        '(?m)(?=^Published Name:)' | Where-Object {
            $_ -match "(?im)^Published Name:\s+$([regex]::Escape($PublishedName))\s*$"
        })
    if ($package.Count -ne 1) {
        throw "Expected one staged package named $PublishedName, found $($package.Count)."
    }
    if (-not $Force -and $package[0] -notmatch
        "(?im)^Class GUID:\s+$([regex]::Escape($ExpectedClassGuid))\s*$") {
        throw "$PublishedName does not use the legacy unsafe class $ExpectedClassGuid. Refusing to remove it without -Force."
    }

    Write-Host "Removing unsafe Time Card package $PublishedName..."
    & pnputil.exe /delete-driver $PublishedName /uninstall /force
    if ($LASTEXITCODE -ne 0) {
        throw "pnputil failed with exit code $LASTEXITCODE"
    }
    Write-Host 'Unsafe package removed. Keep the card disconnected until the'
    Write-Host 'known-working package is confirmed as the remaining candidate.'
}
finally {
    Stop-Transcript | Out-Null
}
