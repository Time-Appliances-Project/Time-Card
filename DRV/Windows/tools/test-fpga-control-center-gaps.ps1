[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content (Join-Path $root 'include\timecard_ioctl.h') -Raw
$models = Get-Content (Join-Path $root `
    'TimeCardControlCenter\Models.cs') -Raw
$client = Get-Content (Join-Path $root `
    'TimeCardControlCenter\TimeCardClient.cs') -Raw
$window = Get-Content (Join-Path $root `
    'TimeCardControlCenter\MainWindow.xaml') -Raw
$fpga = Get-Content (Join-Path $root `
    'TimeCardControlCenter\MainWindow.Fpga.cs') -Raw
$gaps = Get-Content (Join-Path $root `
    'TimeCardControlCenter\MainWindow.FpgaGaps.cs') -Raw
$project = Get-Content (Join-Path $root `
    'TimeCardControlCenter\TimeCardControlCenter.csproj') -Raw

function Assert-Match {
    param([string]$Text, [string]$Pattern, [string]$Description)
    if ($Text -notmatch $Pattern) {
        throw "FPGA Control Center gap missing: $Description"
    }
}

$ioctls = @(
    @{ Native='TIMESTAMP_QUERY'; Managed='IoctlTimestampQuery'; Number=49 },
    @{ Native='TIMESTAMP_SET'; Managed='IoctlTimestampSet'; Number=50 },
    @{ Native='TIMESTAMP_READ'; Managed='IoctlTimestampRead'; Number=51 },
    @{ Native='CLOCK_ADJUST_QUERY'; Managed='IoctlClockAdjustQuery'; Number=52 },
    @{ Native='CLOCK_ADJUST_SET'; Managed='IoctlClockAdjustSet'; Number=53 },
    @{ Native='CORE_INVENTORY_QUERY'; Managed='IoctlCoreInventoryQuery'; Number=54 },
    @{ Native='SIGNAL_EVENT_READ'; Managed='IoctlSignalEventRead'; Number=55 },
    @{ Native='FPGA_CONTRACT_QUERY'; Managed='IoctlFpgaContractQuery'; Number=56 },
    @{ Native='FPGA_CONTRACT_SET'; Managed='IoctlFpgaContractSet'; Number=57 },
    @{ Native='NMEA_UTC_QUERY'; Managed='IoctlNmeaUtcQuery'; Number=58 },
    @{ Native='NMEA_UTC_SET'; Managed='IoctlNmeaUtcSet'; Number=59 },
    @{ Native='CLOCK_ADVANCED_QUERY'; Managed='IoctlClockAdvancedQuery'; Number=60 },
    @{ Native='CLOCK_ADVANCED_SET'; Managed='IoctlClockAdvancedSet'; Number=61 }
)
foreach ($ioctl in $ioctls) {
    Assert-Match $header (
        'IOCTL_TIMECARD_' + $ioctl.Native + '\s*\\\s*' +
        'TIMECARD_IOCTL\(' + $ioctl.Number + ',') (
        "native IOCTL $($ioctl.Number)")
    Assert-Match $client (
        [regex]::Escape($ioctl.Managed) + '\s*=\s*ControlCode\(' +
        $ioctl.Number + ',') "managed IOCTL $($ioctl.Number)"
}

$rawTypes = @(
    'TimeCardTimestampControlRaw', 'TimeCardTimestampEventRaw',
    'TimeCardTimestampBatchRaw', 'TimeCardClockAdjustmentRaw',
    'TimeCardClockAdvancedControlRaw', 'TimeCardCoreDescriptorRaw',
    'TimeCardCoreInventoryRaw', 'TimeCardSignalEventRaw',
    'TimeCardSignalEventBatchRaw', 'TimeCardFpgaImageContractRaw',
    'TimeCardNmeaUtcControlRaw'
)
foreach ($type in $rawTypes) {
    Assert-Match $models ('internal\s+struct\s+' + $type + '\b') (
        "managed $type")
}

Assert-Match $models `
    'TimeCardClockAdvancedControlRaw[\s\S]*?public\s+uint\s+Control;\s*public\s+uint\s+Status;[\s\S]*?SizeConst\s*=\s*3' `
    '128-byte advanced-clock status layout'
Assert-Match $models `
    'TimeCardTimecodeControlRaw[\s\S]*?AmplitudeModulation;\s*public\s+uint\s+ManualYear;' `
    'IRIG AM/manual-year fields'
Assert-Match $client 'SetFpgaImageContract[\s\S]*0x54434d46u' `
    'explicit exact-image acknowledgement'
Assert-Match $client 'capabilityFlags\s*&\s*~0xfffu' `
    'all current contract flags accepted'
Assert-Match $gaps 'lastFpgaContract\.Allows\(ContractNtpSource\)' `
    'NTP source is contract gated'
Assert-Match $gaps 'lastFpgaContract\.Allows\(ContractSynceSource\)' `
    'SyncE source is contract gated'
Assert-Match $gaps 'lastFpgaContract\.Allows\(ContractDynamicSource\)' `
    'dynamic source is contract gated'
Assert-Match $gaps 'Optional UTC registers were not accessed' `
    'fail-closed UTC messaging'
Assert-Match $gaps 'No optional clock register was accessed' `
    'fail-closed advanced-clock messaging'
Assert-Match $gaps 'No fallback address scan will be attempted' `
    'static inventory never implies BAR probing'
Assert-Match $fpga 'case\s+4u:\s+return\s+FpgaVersionAtLeast\(version,\s*2u,\s*3u\)' `
    'PFEC version gate'

$xamlNames = @(
    'FpgaContractStatusText', 'CoreInventoryText', 'NmeaUtcApplyButton',
    'TimestampChannelCombo', 'TimestampEventsTextBox',
    'SignalEventReadButton', 'ClockAdjustmentApplyButton',
    'ClockAdvancedApplyButton', 'IrigMasterAmplitudeCheckBox',
    'IrigSlaveAmplitudeCheckBox', 'IrigSlaveYearTextBox'
)
foreach ($name in $xamlNames) {
    Assert-Match $window ('x:Name="' + $name + '"') "XAML $name"
}
Assert-Match $window 'ComboBoxItem\s+Tag="4">PFEC<' 'PFEC picker'
Assert-Match $window 'ComboBoxItem\s+Tag="7"\s+IsEnabled="False">NTP' `
    'NTP initially locked'
Assert-Match $window 'ComboBoxItem\s+Tag="8"\s+IsEnabled="False">SyncE' `
    'SyncE initially locked'
Assert-Match $window 'ComboBoxItem\s+Tag="253"\s+IsEnabled="False">Dynamic' `
    'dynamic source initially locked'
Assert-Match $project 'Compile\s+Include="MainWindow\.FpgaGaps\.cs"' `
    'gap integration partial included in project'

$assemblyPath = Join-Path $root `
    'TimeCardControlCenter\bin\Release\TimeCardControlCenter.exe'
if (Test-Path -LiteralPath $assemblyPath) {
    $assembly = [Reflection.Assembly]::LoadFrom($assemblyPath)
    $expectedSizes = [ordered]@{
        TimeCardClockAdjustmentRaw = 80
        TimeCardClockAdvancedControlRaw = 128
        TimeCardNmeaUtcControlRaw = 64
        TimeCardSignalEventRaw = 32
        TimeCardSignalEventBatchRaw = 544
        TimeCardTimestampEventRaw = 80
        TimeCardTimestampControlRaw = 80
        TimeCardTimestampBatchRaw = 1312
        TimeCardCoreDescriptorRaw = 32
        TimeCardCoreInventoryRaw = 1056
        TimeCardFpgaImageContractRaw = 64
        TimeCardTimecodeControlRaw = 80
    }
    foreach ($entry in $expectedSizes.GetEnumerator()) {
        $type = $assembly.GetType(
            'TimeCardControlCenter.' + $entry.Key, $true)
        $instance = [Activator]::CreateInstance($type)
        $actual = [Runtime.InteropServices.Marshal]::SizeOf($instance)
        if ($actual -ne $entry.Value) {
            throw "Managed $($entry.Key) is $actual bytes; expected $($entry.Value)."
        }
    }
}

Write-Host 'FPGA Control Center gap contract: PASS'
