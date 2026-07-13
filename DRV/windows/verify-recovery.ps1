#Requires -RunAsAdministrator
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot 'out\timecardctl.exe'
$log = Join-Path $PSScriptRoot 'verify-recovery.log'

Start-Transcript -Path $log -Force | Out-Null
try {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Control tool not found: $tool"
    }

    & $tool status
    if ($LASTEXITCODE -ne 0) { throw 'timecardctl status failed.' }
    & $tool get
    if ($LASTEXITCODE -ne 0) { throw 'timecardctl get failed.' }
    & $tool uart-config 0 115200
    if ($LASTEXITCODE -ne 0) { throw 'GNSS UART configuration failed.' }
    & $tool uart-read 0 64 1000
    if ($LASTEXITCODE -ne 0) {
        Write-Warning 'GNSS UART returned no data during the one-second read.'
    }
}
finally {
    Stop-Transcript | Out-Null
}
