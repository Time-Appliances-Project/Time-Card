#requires -RunAsAdministrator
[CmdletBinding()]
param([string]$Tool = (Join-Path $PSScriptRoot 'timecardctl.exe'))
$ErrorActionPreference = 'Stop'
$wiwi = 'PCI\VEN_2163&DEV_1008&SUBSYS_000710EE&REV_02'
$cards = @(Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -match '^PCI\\VEN_(1D9B&DEV_0400|18D4&DEV_1008|1AD7&DEV_A000|2163&DEV_1008)'
})
# The CLI uses the legacy TimeCard0 link. Require an unambiguous lab target.
if ($cards.Count -ne 1 -or $cards[0].HardwareID -notcontains $wiwi) {
    throw 'This smoke test needs one Time Card controller, with the WiWi revision 02 hardware ID.'
}
$card = $cards[0]
$driver = Get-CimInstance Win32_PnPSignedDriver | Where-Object DeviceID -EQ $card.InstanceId
if ($card.Status -ne 'OK' -or $driver.DriverVersion -ne '1.48.0.0') {
    throw "WiWi status $($card.Status), bound driver $($driver.DriverVersion), INF $($driver.InfName)"
}
$label = (Get-PnpDeviceProperty -InstanceId $card.InstanceId -KeyName 'DEVPKEY_Device_DeviceDesc').Data
if ($label -ne 'Time Card Controller (WiWi Revision 02)') { throw "Unexpected controller label: $label" }
$service = Get-CimInstance Win32_SystemDriver -Filter "Name='timecard'"
if ($service.State -ne 'Running' -or $service.DisplayName -ne 'Time Card Driver') {
    throw "Unexpected Time Card service: $($service.DisplayName), $($service.State)"
}
$imagePath = [Environment]::ExpandEnvironmentVariables($service.PathName.Trim('"'))
$imagePath = $imagePath -replace '^\\SystemRoot(?=\\)', $env:SystemRoot -replace '^\\\?\?\\', ''
if ($imagePath -like 'System32\*') { $imagePath = Join-Path $env:SystemRoot $imagePath }
$version = (Get-Item -LiteralPath $imagePath).VersionInfo
if ($version.ProductName -ne 'Time Card' -or $version.FileDescription -ne 'Time Card KMDF Driver' -or
    $version.FileVersion -ne $driver.DriverVersion) { throw "Unexpected file metadata on $imagePath" }
Write-Output "$label | $($service.DisplayName) | $($version.FileDescription) | $($version.FileVersion)"
$os = Get-CimInstance Win32_OperatingSystem
Write-Output "$env:COMPUTERNAME | $($os.Caption) $($os.Version) | $($card.InstanceId) | $($driver.InfName)"
function Read-Card([string]$Command) {
    $output = & $Tool $Command
    if ($LASTEXITCODE -ne 0) { throw "timecardctl $Command failed with $LASTEXITCODE" }
    $output -join [Environment]::NewLine
}
$status = Read-Card status
$cap = Read-Card capabilities
$fpga = Read-Card fpga-status
Write-Output $status $cap $fpga
if ($status -notmatch '(?m)^Driver:\s+1\.48\s*$' -or
    $status -notmatch 'Register layout:\s+MSI-X' -or $status -notmatch 'BAR length:\s+0x04000000' -or
    $status -notmatch 'Clock offset:\s+0x03000000' -or $cap -notmatch 'Board profile:\s+WiWi' -or
    $fpga -notmatch 'FPGA image build:\s+0x[0-9a-f]{8}') { throw 'Unexpected runtime profile or FPGA mapping.' }

$children = @((Get-PnpDeviceProperty -InstanceId $card.InstanceId -KeyName 'DEVPKEY_Device_Children').Data)
foreach ($id in 'PHC','TOD','UART_GNSS','UART_MAC','UART_NMEA','SMA','TIMING_IO','I2C','PTM') {
    $child = @($children | Where-Object { $_ -like "TIMECARD\$id\*" })
    if ($child.Count -ne 1 -or (Get-PnpDevice -PresentOnly -InstanceId $child[0]).Status -ne 'OK') {
        throw "Missing or unhealthy WiWi child $id"
    }
}
if ($children -match '^TIMECARD\\(UART_GNSS2|FLASH)\\') { throw 'Unwired or unsupported child was enumerated.' }
$previous = $null
$watch = [Diagnostics.Stopwatch]::StartNew()
foreach ($i in 1..4) {
    $before = $watch.Elapsed.TotalSeconds
    $reading = Read-Card get
    $after = $watch.Elapsed.TotalSeconds
    if ($reading -notmatch '(?m)^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\.(\d{9}) UTC') {
        throw "Unrecognized PHC reading: $reading"
    }
    $sec = [datetime]::ParseExact($Matches[1],'yyyy-MM-dd HH:mm:ss',[Globalization.CultureInfo]::InvariantCulture)
    $sample = [decimal]$sec.Ticks / 10000000 + [decimal]$Matches[2] / 1000000000
    if ($null -ne $previous) {
        $delta = [double]($sample - $previous)
        $low = $before - $previousAfter
        $high = $after - $previousBefore
        Write-Output ('PHC delta {0:F9} s, host bounds [{1:F6}, {2:F6}] s' -f $delta,$low,$high)
        if ($delta -le 0 -or $delta -lt ($low - 0.005) -or $delta -gt ($high + 0.005)) {
            throw 'PHC progression is outside host sampling bounds (5 ms allowance).'
        }
    }
    $previous = $sample
    $previousBefore = $before
    $previousAfter = $after
    if ($i -lt 4) { Start-Sleep -Seconds 1 }
}
Write-Output (Read-Card i2c-status)
Write-Output (Read-Card sma-status)
Write-Output 'WiWi device binding, child health and PHC progression passed.'
