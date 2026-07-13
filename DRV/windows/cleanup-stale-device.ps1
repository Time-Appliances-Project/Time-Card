#Requires -RunAsAdministrator
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$instanceId = 'PCI\VEN_1D9B&DEV_0400&SUBSYS_000710EE&REV_02\4&14EAF662&0&00E0'
$log = Join-Path $PSScriptRoot 'cleanup-stale-device.log'

Start-Transcript -Path $log -Force | Out-Null
try {
    Write-Host 'Removing the disconnected Time Card device record...'
    & pnputil.exe /remove-device $instanceId
    if ($LASTEXITCODE -ne 0) {
        throw "pnputil failed with exit code $LASTEXITCODE"
    }
    Write-Host 'Disconnected Time Card device record removed.'
}
finally {
    Stop-Transcript | Out-Null
}
