#Requires -RunAsAdministrator
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot 'out\timecardctl.exe'
$log = Join-Path $PSScriptRoot 'set-clock.log'

Start-Transcript -Path $log -Force | Out-Null
try {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Control tool not found: $tool"
    }

    Write-Host 'Setting the Time Card PHC from Windows UTC...'
    & $tool set-system
    if ($LASTEXITCODE -ne 0) { throw 'timecardctl set-system failed.' }

    Write-Host 'Reading the Time Card PHC back...'
    & $tool get
    if ($LASTEXITCODE -ne 0) { throw 'timecardctl get failed.' }
}
finally {
    Stop-Transcript | Out-Null
}
