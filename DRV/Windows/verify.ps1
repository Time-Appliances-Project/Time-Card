#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [switch]$SetFromSystem,
    [switch]$TestGnssUart,
    [switch]$ExpectHierarchy
)

$ErrorActionPreference = 'Stop'
$instance = 'PCI\VEN_1D9B&DEV_0400*'
$tool = Join-Path $PSScriptRoot 'out\timecardctl.exe'
$log = Join-Path $PSScriptRoot 'verify.log'

Start-Transcript -Path $log -Force | Out-Null
try {
    pnputil.exe /scan-devices | Out-Host
    $device = Get-PnpDevice -PresentOnly | Where-Object InstanceId -Like $instance
    if (-not $device) {
        throw 'OCP TimeCard PCI device is not present.'
    }
    $device | Format-List Status, Class, FriendlyName, InstanceId | Out-Host
    if ($device.Status -ne 'OK') {
        $problem = Get-PnpDeviceProperty -InstanceId $device.InstanceId `
            -KeyName DEVPKEY_Device_ProblemCode
        throw "TimeCard did not start. Device Manager problem code: $($problem.Data)"
    }

    $timeCardDevices = Get-PnpDevice -PresentOnly -Class TimeCard |
        Sort-Object FriendlyName
    $timeCardDevices |
        Format-Table -AutoSize Status, FriendlyName, InstanceId | Out-Host
    $expectedChildren = @(
        'TIMECARD\PHC\*',
        'TIMECARD\TOD\*',
        'TIMECARD\UART_GNSS\*',
        'TIMECARD\UART_GNSS2\*',
        'TIMECARD\UART_MAC\*',
        'TIMECARD\UART_NMEA\*',
        'TIMECARD\SMA\*',
        'TIMECARD\TIMING_IO\*',
        'TIMECARD\I2C\*',
        'TIMECARD\FLASH\*',
        'TIMECARD\PTM\*'
    )
    if ($ExpectHierarchy) {
        foreach ($childPattern in $expectedChildren) {
            $child = $timeCardDevices |
                Where-Object InstanceId -Like $childPattern
            if (-not $child) {
                throw "Time Card child device is missing: $childPattern"
            }
            if ($child.Status -ne 'OK') {
                throw "Time Card child is not healthy: $($child.InstanceId)"
            }
        }
    }
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Control tool not found: $tool"
    }

    & $tool status
    if ($LASTEXITCODE -ne 0) { throw 'timecardctl status failed.' }
    & $tool get
    if ($LASTEXITCODE -ne 0) { throw 'timecardctl get failed.' }
    if ($SetFromSystem) {
        & $tool set-system
        if ($LASTEXITCODE -ne 0) { throw 'timecardctl set-system failed.' }
        & $tool get
        if ($LASTEXITCODE -ne 0) { throw 'post-set timecardctl get failed.' }
    }
    if ($TestGnssUart) {
        & $tool uart-config 0 115200
        if ($LASTEXITCODE -ne 0) { throw 'GNSS UART configuration failed.' }
        & $tool uart-read 0 64 1000
        if ($LASTEXITCODE -ne 0) {
            Write-Warning 'GNSS UART produced no data during the one-second smoke test.'
        }
    }
}
finally {
    Stop-Transcript | Out-Null
}
