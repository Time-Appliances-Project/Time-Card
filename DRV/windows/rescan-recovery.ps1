#Requires -RunAsAdministrator
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$log = Join-Path $PSScriptRoot 'rescan-recovery.log'

Start-Transcript -Path $log -Force | Out-Null
try {
    Write-Host 'Scanning Plug and Play devices for the installed Time Card...'
    & pnputil.exe /scan-devices
    if ($LASTEXITCODE -ne 0) {
        throw "pnputil failed with exit code $LASTEXITCODE"
    }
}
finally {
    Stop-Transcript | Out-Null
}
