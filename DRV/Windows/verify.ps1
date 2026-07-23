#Requires -RunAsAdministrator
[CmdletBinding()]
param(
    [switch]$SetFromSystem,
    [switch]$TestGnssUart,
    [switch]$TestSensors,
    [switch]$TestLeds,
    [switch]$ExpectHierarchy
)

$ErrorActionPreference = 'Stop'
$instances = @(
    'PCI\VEN_1D9B&DEV_0400*',
    'PCI\VEN_18D4&DEV_1008*',
    'PCI\VEN_1AD7&DEV_A000*'
)
$tool = Join-Path $PSScriptRoot 'out\timecardctl.exe'
$log = Join-Path $PSScriptRoot 'verify.log'

Start-Transcript -Path $log -Force | Out-Null
try {
    pnputil.exe /scan-devices | Out-Host
    $devices = Get-PnpDevice -PresentOnly | Where-Object {
        $id = $_.InstanceId
        $instances | Where-Object { $id -like $_ }
    }
    if (-not $devices) {
        throw 'A supported OCP Time Card PCI device is not present.'
    }
    $devices | Format-List Status, Class, FriendlyName, InstanceId | Out-Host
    foreach ($device in $devices) {
        if ($device.Status -ne 'OK') {
            $problem = Get-PnpDeviceProperty -InstanceId $device.InstanceId `
                -KeyName DEVPKEY_Device_ProblemCode
            throw "Time Card did not start. Device Manager problem code: $($problem.Data)"
        }
    }

    $timeCardDevices = Get-PnpDevice -PresentOnly -Class TimeCard |
        Sort-Object FriendlyName
    $timeCardDevices |
        Format-Table -AutoSize Status, FriendlyName, InstanceId | Out-Host
    $expectedChildren = @(
        'TIMECARD\PHC\*',
        'TIMECARD\UART_GNSS\*',
        'TIMECARD\UART_MAC\*',
        'TIMECARD\SMA\*',
        'TIMECARD\I2C\*',
        'TIMECARD\FLASH\*'
    )
    $hasFbProfile = $devices | Where-Object {
        $_.InstanceId -like 'PCI\VEN_1D9B&DEV_0400*' -or
        $_.InstanceId -like 'PCI\VEN_18D4&DEV_1008*'
    }
    if ($hasFbProfile) {
        $expectedChildren += @(
            'TIMECARD\TOD\*',
            'TIMECARD\UART_GNSS2\*',
            'TIMECARD\UART_NMEA\*',
            'TIMECARD\TIMING_IO\*',
            'TIMECARD\PTM\*'
        )
    }
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
    & $tool serial
    if ($LASTEXITCODE -ne 0) { throw 'timecardctl serial failed.' }
    & $tool sma-status
    if ($LASTEXITCODE -ne 0) { throw 'timecardctl sma-status failed.' }
    & $tool i2c-status
    if ($LASTEXITCODE -ne 0) { throw 'timecardctl i2c-status failed.' }
    if ($hasFbProfile) {
        & $tool nmea-status
        if ($LASTEXITCODE -ne 0) { throw 'timecardctl nmea-status failed.' }
    }
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
    if ($TestSensors) {
        & $tool sensors
        if ($LASTEXITCODE -ne 0) {
            throw 'No valid sensor telemetry was returned.'
        }
    }
    if ($TestLeds) {
        & $tool led-test
        if ($LASTEXITCODE -ne 0) {
            throw 'IS32FL3207 electrical test failed.'
        }
    }
}
finally {
    Stop-Transcript | Out-Null
}
