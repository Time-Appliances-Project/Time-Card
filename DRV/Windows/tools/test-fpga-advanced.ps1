[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$windows = Split-Path -Parent $PSScriptRoot
$header = Get-Content -LiteralPath `
    (Join-Path $windows 'include\timecard_ioctl.h') -Raw
$driverHeader = Get-Content -LiteralPath (Join-Path $windows 'timecard.h') -Raw
$driver = Get-Content -LiteralPath (Join-Path $windows 'driver.c') -Raw
$dispatcher = Get-Content -LiteralPath (Join-Path $windows 'ioctl.c') -Raw
$fpga = Get-Content -LiteralPath (Join-Path $windows 'fpga.c') -Raw
$timestamp = Get-Content -LiteralPath (Join-Path $windows 'timestamp.c') -Raw
$timing = Get-Content -LiteralPath (Join-Path $windows 'timing.c') -Raw
$nmea = Get-Content -LiteralPath (Join-Path $windows 'nmea.c') -Raw
$ptp = Get-Content -LiteralPath (Join-Path $windows 'ptp.c') -Raw
$serial = Get-Content -LiteralPath (Join-Path $windows 'serial.c') -Raw
$tool = Get-Content -LiteralPath (Join-Path $windows 'tools\timecardctl.c') -Raw
$productWindow = Get-Content -LiteralPath `
    (Join-Path $windows 'TimeCardControlCenter\MainWindow.Product.cs') -Raw
$project = Get-Content -LiteralPath (Join-Path $windows 'timecard.vcxproj') -Raw
$inf = Get-Content -LiteralPath (Join-Path $windows 'timecard.inf') -Raw
$package = Get-Content -LiteralPath (Join-Path $windows 'package-release.ps1') -Raw
$controlAssembly = Get-Content -LiteralPath `
    (Join-Path $windows 'TimeCardControlCenter\Properties\AssemblyInfo.cs') -Raw
$serviceAssembly = Get-Content -LiteralPath `
    (Join-Path $windows 'TimeCardOscillatord\Properties\AssemblyInfo.cs') -Raw
$serviceRuntime = Get-Content -LiteralPath `
    (Join-Path $windows 'TimeCardOscillatord\OscillatordRuntime.cs') -Raw
$providerBuild = Get-Content -LiteralPath `
    (Join-Path $windows 'build-time-provider.cmd') -Raw
$providerResource = Get-Content -LiteralPath `
    (Join-Path $windows 'TimeCardTimeProvider\timecard_time_provider.rc') -Raw
$assertions = 0

function Assert-Match {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Description
    )
    if ($Text -notmatch $Pattern) {
        throw "Advanced FPGA test failed: $Description"
    }
    $script:assertions++
}

function Assert-NotMatch {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Description
    )
    if ($Text -match $Pattern) {
        throw "Advanced FPGA test failed: $Description"
    }
    $script:assertions++
}

function Assert-Sequence {
    param(
        [string]$Text,
        [string[]]$Patterns,
        [string]$Description
    )
    $combined = [string]::Join('[\s\S]*?', $Patterns)
    Assert-Match $Text $combined $Description
}

Write-Host '=== ABI 15 and release layout ==='
Assert-Match $header '#define\s+TIMECARD_ABI_VERSION\s+15u' `
    'the public header is not ABI 15'
foreach ($ioctl in ([ordered]@{
        IOCTL_TIMECARD_TIMESTAMP_QUERY = 49
        IOCTL_TIMECARD_TIMESTAMP_SET = 50
        IOCTL_TIMECARD_TIMESTAMP_READ = 51
        IOCTL_TIMECARD_CLOCK_ADJUST_QUERY = 52
        IOCTL_TIMECARD_CLOCK_ADJUST_SET = 53
        IOCTL_TIMECARD_CORE_INVENTORY_QUERY = 54
        IOCTL_TIMECARD_SIGNAL_EVENT_READ = 55
        IOCTL_TIMECARD_FPGA_CONTRACT_QUERY = 56
        IOCTL_TIMECARD_FPGA_CONTRACT_SET = 57
        IOCTL_TIMECARD_NMEA_UTC_QUERY = 58
        IOCTL_TIMECARD_NMEA_UTC_SET = 59
        IOCTL_TIMECARD_CLOCK_ADVANCED_QUERY = 60
        IOCTL_TIMECARD_CLOCK_ADVANCED_SET = 61
    }).GetEnumerator()) {
    Assert-Match $header (
        [regex]::Escape($ioctl.Key) + '\s+\\\s*' +
        'TIMECARD_IOCTL\(' + $ioctl.Value + ',') `
        "$($ioctl.Key) is not assigned ABI function $($ioctl.Value)"
    Assert-Match $dispatcher ('case\s+' + [regex]::Escape($ioctl.Key) + ':') `
        "$($ioctl.Key) is not dispatched"
}
foreach ($layout in @(
        'sizeof\(TIMECARD_CLOCK_ADJUSTMENT\)\s*==\s*80',
        'sizeof\(TIMECARD_CLOCK_ADVANCED_CONTROL\)\s*==\s*128',
        'sizeof\(TIMECARD_FPGA_IMAGE_CONTRACT\)\s*==\s*64',
        'sizeof\(TIMECARD_CORE_DESCRIPTOR\)\s*==\s*32',
        'sizeof\(TIMECARD_CORE_INVENTORY\)\s*==\s*1056')) {
    Assert-Match $fpga $layout "kernel layout assertion is missing: $layout"
}
foreach ($layout in @(
        'sizeof\(TIMECARD_TIMESTAMP_EVENT\)\s*==\s*80',
        'sizeof\(TIMECARD_TIMESTAMP_CONTROL\)\s*==\s*80',
        'sizeof\(TIMECARD_TIMESTAMP_BATCH\)\s*==\s*1312')) {
    Assert-Match $timestamp $layout `
        "timestamper layout assertion is missing: $layout"
}
foreach ($layout in @(
        'sizeof\(TIMECARD_SIGNAL_EVENT\)\s*==\s*32',
        'sizeof\(TIMECARD_SIGNAL_EVENT_BATCH\)\s*==\s*544')) {
    Assert-Match $timing $layout `
        "signal-event layout assertion is missing: $layout"
}
Assert-Match $header `
    'TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER_READY\s+\(1u\s*<<\s*9\)' `
    'advanced-clock holdover readiness is missing'
Assert-Match $header `
    'TIMECARD_CLOCK_ADVANCED_FLAG_AGING_READY\s+\(1u\s*<<\s*10\)' `
    'advanced-clock aging readiness is missing'
Assert-Sequence $header @(
    'typedef\s+struct\s+_TIMECARD_CLOCK_ADVANCED_CONTROL',
    'unsigned\s+__int32\s+Control',
    'unsigned\s+__int32\s+Status',
    'unsigned\s+__int32\s+ApplyFlags',
    'unsigned\s+__int32\s+Reserved\[3\]') `
    'the fixed 128-byte advanced-clock layout changed unexpectedly'

Write-Host '=== Exact-image contract and static inventory ==='
Assert-Match $header `
    'TIMECARD_FPGA_CONTRACT_ACKNOWLEDGEMENT\s+0x54434d46u' `
    'the explicit TCMF acknowledgement is missing'
Assert-Match $header `
    'TIMECARD_FPGA_CONTRACT_ALL_FLAGS\s+0x00000fffu' `
    'the contract mask does not include the audited capabilities'
foreach ($flag in @(
        'CLOCK_SERVO_LOG', 'CLOCK_ADVANCED', 'TOD_TELEMETRY',
        'TOD_MASTER_UTC_READ', 'TOD_MASTER_UTC_WRITE', 'IRIG_MASTER_AM',
        'IRIG_SLAVE_AM', 'IRIG_SLAVE_YEAR', 'NTP_SOURCE',
        'ART_TIMESTAMP_EXTENDED', 'SYNCE_SOURCE', 'DYNAMIC_SOURCE')) {
    Assert-Match $header ('TIMECARD_FPGA_CONTRACT_' + $flag) `
        "the $flag contract flag is missing"
}
Assert-Sequence $fpga @(
    'TimeCardFpgaContractSet',
    'request->Acknowledgement\s*!=',
    'TIMECARD_FPGA_CONTRACT_ACKNOWLEDGEMENT',
    'request->BoardProfile\s*!=\s*context->BoardProfile',
    'request->Layout\s*!=\s*context->Layout',
    'request->Reserved\[index\]\s*!=\s*0u',
    'request->RawImageVersion\s*!=\s*image.RawVersion') `
    'contract activation does not validate identity, layout, and reserved data'
Assert-Sequence $fpga @(
    'TimeCardFpgaContractAllows',
    'TimeCardFpgaImageQuery\(context,\s*&image\)',
    'TIMECARD_FPGA_IMAGE_FLAG_LOADER',
    'TIMECARD_FPGA_IMAGE_FLAG_FPGA_FIRMWARE',
    'context->FpgaContractImageVersion\s*==\s*image.RawVersion') `
    'optional accesses do not revalidate the live FPGA-firmware image'
Assert-Match $driver `
    'FpgaContractImageVersion\s*=\s*0u;[\s\S]*?FpgaContractCapabilities\s*=\s*0u;' `
    'contract state is not cleared with mapped resources'
Assert-Match $fpga `
    'TIMECARD_INVENTORY_FLAG_STATIC_PROFILE\s*\|[\s\S]*?TIMECARD_INVENTORY_FLAG_NO_CONFIG_SLAVE' `
    'inventory does not identify itself as static/no-Configuration-Slave'
Assert-Match $fpga `
    'static\s+const\s+ULONG\s+timestampMsi\[TIMECARD_TIMESTAMP_COUNT\]' `
    'inventory is not built from a fixed MSI timestamp map'
Assert-Match $fpga `
    'static\s+const\s+ULONG\s+timestampMsix\[TIMECARD_TIMESTAMP_COUNT\]' `
    'inventory is not built from a fixed MSI-X timestamp map'
Assert-Match $fpga `
    'static\s+const\s+ULONG\s+timestampArt\[TIMECARD_TIMESTAMP_COUNT\]' `
    'inventory is not built from a fixed ART timestamp map'
Assert-NotMatch $driverHeader `
    'volatile\s+[^;]*CONFIG(?:URATION)?_SLAVE[^;]*\*' `
    'a speculative Configuration Slave MMIO pointer was added'

Write-Host '=== Timestamp and generator interrupts ==='
Assert-Match $header '#define\s+TIMECARD_TIMESTAMP_COUNT\s+6u' `
    'all six published timestamp channels are not exposed'
Assert-Match $timestamp `
    'TimeCardTimestampVersionAtLeast\(version,\s*1u,\s*3u\)' `
    'current timestamper layout is not gated at 1.3'
Assert-Match $timestamp `
    'static\s+const\s+ULONG\s+msiMessages\[TIMECARD_TIMESTAMP_COUNT\][\s\S]*?1u,\s*2u,\s*6u,\s*15u,\s*16u,\s*0u' `
    'MSI timestamper vectors do not match the published map'
Assert-Match $timestamp `
    'static\s+const\s+ULONG\s+msixMessages\[TIMECARD_TIMESTAMP_COUNT\][\s\S]*?33u,\s*34u,\s*38u,\s*47u,\s*48u,\s*32u' `
    'MSI-X timestamper vectors do not match the published map'
Assert-Match $timestamp `
    'static\s+const\s+ULONG\s+artMessages\[TIMECARD_TIMESTAMP_COUNT\][\s\S]*?12u,\s*8u,\s*10u,\s*14u,\s*15u,\s*11u' `
    'ART timestamper vectors do not match the published map'
Assert-Match $timestamp `
    'TimeCardTimestampExtendedSurface[\s\S]*?BoardProfile\s*!=\s*TIMECARD_BOARD_ART' `
    'ART extended timestamp registers are not fail-closed'
Assert-Match $serial `
    'TimeCardHandleTimestampInterrupt\(context,\s*globalMessageId\)' `
    'the combined ISR does not dispatch timestamp events'
Assert-Match $timing `
    'Layout\s*==\s*TIMECARD_LAYOUT_MSIX\s*\?\s*43u\s*:\s*11u' `
    'signal-generator completion vectors do not start at 43/11'
Assert-Match $serial `
    'TimeCardHandleSignalInterrupt\(context,\s*globalMessageId\)' `
    'the combined ISR does not dispatch generator completion events'
Assert-Match $project '<ClCompile\s+Include="timestamp\.c"' `
    'timestamp.c is not part of the driver project'

Write-Host '=== Revision and synthesis gates ==='
foreach ($gate in @(
        '2u,\s*1u[\s\S]*?TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER',
        '2u,\s*2u[\s\S]*?TIMECARD_CLOCK_ADVANCED_FLAG_OUTLIER_FILTERS',
        '2u,\s*3u[\s\S]*?TIMECARD_CLOCK_ADVANCED_FLAG_RATE_LIMITERS',
        '2u,\s*4u[\s\S]*?TIMECARD_CLOCK_ADVANCED_FLAG_DYNAMIC_CONTROL',
        '2u,\s*5u[\s\S]*?TIMECARD_CLOCK_ADVANCED_FLAG_AGING',
        '2u,\s*6u[\s\S]*?TIMECARD_CLOCK_ADVANCED_FLAG_REVERT',
        '1u,\s*6u[\s\S]*?TIMECARD_CLOCK_ADVANCED_FLAG_SERVO_FACTORS')) {
    Assert-Match $fpga $gate "advanced-clock version gate is missing: $gate"
}
Assert-Match $fpga `
    'result->Status\s*=\s*READ_REGISTER_ULONG\([\s\S]*?context->Regs->Status' `
    'advanced clock query does not return the base status word'
Assert-Match $fpga `
    'result->Status\s*&\s*\(1u\s*<<\s*2\)[\s\S]*?TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER_READY' `
    'holdover-ready status is not decoded'
Assert-Match $fpga `
    'result->Status\s*&\s*\(1u\s*<<\s*3\)[\s\S]*?TIMECARD_CLOCK_ADVANCED_FLAG_AGING_READY' `
    'aging-ready status is not decoded'
Assert-Sequence $ptp @(
    'request->Source\s*==\s*TIMECARD_CLOCK_SOURCE_NTP',
    'TIMECARD_FPGA_CONTRACT_NTP_SOURCE') `
    'NTP source is not exact-contract gated'
Assert-Match $ptp `
    'TIMECARD_CLOCK_SOURCE_NTP\)[\s\S]*?version\s*<\s*0x01080000u' `
    'NTP source is not gated at Clock 1.8'
Assert-Sequence $ptp @(
    'request->Source\s*==\s*TIMECARD_CLOCK_SOURCE_SYNCE',
    'TIMECARD_FPGA_CONTRACT_SYNCE_SOURCE') `
    'SyncE source is not exact-contract gated'
Assert-Sequence $ptp @(
    'request->Source\s*==\s*TIMECARD_CLOCK_SOURCE_DYN',
    'TIMECARD_FPGA_CONTRACT_DYNAMIC_SOURCE') `
    'dynamic clock source is not exact-contract gated'
Assert-Match $ptp `
    'TIMECARD_CLOCK_SOURCE_SYNCE[\s\S]*?TIMECARD_CLOCK_SOURCE_DYN[\s\S]*?version\s*<\s*0x02070000u' `
    'SyncE/dynamic sources are not gated at Clock 2.7'
foreach ($gate in @(
        'TIMECARD_TOD_PROTOCOL_UBX:[\s\S]*?1u,\s*6u',
        'TIMECARD_TOD_PROTOCOL_TSIP:[\s\S]*?1u,\s*9u',
        'TIMECARD_TOD_PROTOCOL_ESIP:[\s\S]*?2u,\s*1u',
        'TIMECARD_TOD_PROTOCOL_PFEC:[\s\S]*?2u,\s*3u')) {
    Assert-Match $fpga $gate "ToD protocol version gate is missing: $gate"
}
Assert-Match $header '#define\s+TIMECARD_TOD_PROTOCOL_PFEC\s+4u' `
    'PFEC protocol selector is not 4'
Assert-Match $fpga `
    'case\s+TIMECARD_TOD_PROTOCOL_PFEC:[\s\S]*?2u,\s*3u\)\s*\?\s*0x7fu\s*:\s*0u' `
    'PFEC message-disable mask is not 0x7f'
Assert-Sequence $fpga @(
    'telemetryAllowed\s*=\s*TimeCardFpgaContractAllows',
    'TIMECARD_FPGA_CONTRACT_TOD_TELEMETRY',
    'TimeCardTodQueryLocked\(context,\s*control,\s*telemetryAllowed\)') `
    'ToD optional telemetry is not contract gated'
Assert-Match $fpga `
    'telemetryAllowed\s*&&[\s\S]*?TimeCardTodUtcTelemetrySupported' `
    'ToD UTC telemetry is not revision gated'
Assert-Match $fpga `
    'telemetryAllowed\s*&&[\s\S]*?TimeCardTodGnssTelemetrySupported' `
    'ToD GNSS telemetry is not revision gated'
Assert-Match $fpga `
    'masterAmAllowed[\s\S]*?TimeCardVersionAtLeast\(version,\s*1u,\s*5u\)' `
    'IRIG Master AM is not gated at 1.5'
Assert-Match $fpga `
    'slaveYearAllowed[\s\S]*?TimeCardVersionAtLeast\(version,\s*1u,\s*5u\)' `
    'IRIG Slave code/year is not gated at 1.5'
Assert-Match $fpga `
    'slaveAmAllowed[\s\S]*?TimeCardVersionAtLeast\(version,\s*1u,\s*6u\)' `
    'IRIG Slave AM is not gated at 1.6'
Assert-Match $nmea `
    'TIMECARD_NMEA_UTC_WRITE_REQUEST\s+\(1u\s*<<\s*0\)' `
    'ToD Master UTC write does not use request bit 0'
Assert-NotMatch $nmea `
    'TIMECARD_NMEA_UTC_WRITE_REQUEST\s+\([^\r\n]*<<\s*1\)' `
    'ToD Master UTC write uses reserved bit 1'
Assert-Match $nmea `
    'TIMECARD_NMEA_UTC_READ_REQUEST[\s\S]*?TIMECARD_NMEA_UTC_READ_DONE[\s\S]*?STATUS_IO_TIMEOUT' `
    'ToD Master UTC read handshake is not bounded'
foreach ($command in @(
        'signal-events', 'timestamp-status', 'timestamp-set',
        'timestamp-read', 'clock-adjust-status', 'clock-adjust-set',
        'clock-advanced-status', 'clock-advanced-set', 'fpga-inventory',
        'fpga-contract-status', 'fpga-contract-set', 'nmea-utc-status',
        'nmea-utc-set')) {
    Assert-Match $tool ('strcmp\(argv\[1\],\s*"' +
        [regex]::Escape($command) + '"\)') `
        "timecardctl command is missing: $command"
}
Assert-Match $tool `
    '"pfec"[\s\S]*?TIMECARD_TOD_PROTOCOL_PFEC' `
    'timecardctl cannot select the PFEC parser'
Assert-Match $tool `
    '"NMEA",\s*"UBX",\s*"TSIP",\s*"ESIP",\s*"PFEC"' `
    'timecardctl cannot display PFEC status by name'
Assert-Match $tool `
    'TIMECARD_TOD_FLAG_UTC_TELEMETRY_VALID[\s\S]*?UtcStatus[\s\S]*?TimeToLeapSeconds' `
    'timecardctl does not display contracted UTC telemetry'
Assert-Match $tool `
    'TIMECARD_TOD_FLAG_GNSS_TELEMETRY_VALID[\s\S]*?GnssStatus[\s\S]*?Satellites' `
    'timecardctl does not display contracted GNSS telemetry'
Assert-Match $productWindow `
    'protocol\s*==\s*4\s*\?\s*"PFEC"' `
    'Control Center cannot display PFEC profiles by name'

Write-Host '=== Transactional restoration ==='
Assert-Sequence $fpga @(
    'TimeCardClockAdjustmentSet',
    'oldOffset\s*=', 'oldDrift\s*=', 'oldThreshold\s*=',
    'rollback:', 'OffsetNs,\s*oldOffset', 'DriftNs,\s*oldDrift',
    'InSyncThreshold,\s*oldThreshold') `
    'smooth-clock writes do not restore their full snapshot'
Assert-Sequence $fpga @(
    'TimeCardClockAdvancedSet',
    'oldOffsetLimiter\s*=', 'oldHoldover\s*=', 'oldOffsetOutlier\s*=',
    'oldAging\s*=', 'oldServoOffsetP\s*=', 'restore:',
    'oldOffsetLimiter', 'oldHoldover', 'oldOffsetOutlier', 'oldAging',
    'oldServoOffsetP', 'oldControl') `
    'advanced-clock writes do not restore their full snapshot'
Assert-Sequence $fpga @(
    'TimeCardPpsSet', 'oldControl\s*=', 'oldPolarity\s*=',
    'oldPulseWidth\s*=', 'oldDelay\s*=', 'rollback:',
    'oldPolarity', 'oldPulseWidth', 'oldDelay', 'oldControl') `
    'PPS writes do not restore their full snapshot'
Assert-Sequence $fpga @(
    'TimeCardTimecodeSet', 'oldControlBits\s*=', 'oldYear\s*=',
    'rollback:', 'oldControlBits', 'oldYear', 'oldControl') `
    'IRIG/DCF writes do not restore advanced fields'
Assert-Sequence $fpga @(
    'TimeCardTodSet', 'TimeCardUartSnapshotHardware',
    'rollback:', 'UartBaud,\s*oldBaud', 'UartPolarity,', 'oldPolarity',
    'TimeCardUartRestoreHardware') `
    'ToD parser rollback does not restore MMIO and UART state'
Assert-Sequence $nmea @(
    'TimeCardNmeaSet', 'TimeCardUartSnapshotHardware',
    'restore:', 'UartBaud,', 'oldBaud', 'UartPolarity,', 'oldPolarity',
    'TimeCardUartRestoreHardware') `
    'NMEA rollback does not restore MMIO and UART state'
Assert-Sequence $serial @(
    'TimeCardUartSnapshotHardware', 'LineControl\s*=',
    'InterruptEnable\s*=', 'DivisorLow\s*=', 'DivisorHigh\s*=',
    'TimeCardUartRestoreHardware', 'DivisorLow', 'DivisorHigh',
    'InterruptEnable', 'LineControl') `
    'raw UART rollback does not preserve divisor, LCR, and interrupt state'
Assert-Sequence $timing @(
    'rollback_runtime:', 'oldPolarity', 'oldCableDelay', 'oldRepeatCount',
    'oldStartSeconds', 'oldPeriodSeconds', 'oldPulseSeconds',
    'oldInterruptMask', 'oldControl') `
    'signal-generator rollback does not restore all writable registers'
Assert-Sequence $timestamp @(
    'oldEnable\s*=\s*READ_REGISTER_ULONG',
    'oldMask\s*=\s*READ_REGISTER_ULONG',
    'oldPolarity\s*=\s*READ_REGISTER_ULONG',
    'oldCable\s*=\s*READ_REGISTER_ULONG',
    'WRITE_REGISTER_ULONG\(\(PULONG\)&reg->Polarity,\s*oldPolarity\)',
    'WRITE_REGISTER_ULONG\(\(PULONG\)&reg->CableDelay,\s*oldCable\)',
    'WRITE_REGISTER_ULONG\(\(PULONG\)&reg->Enable,\s*oldEnable\)',
    'WRITE_REGISTER_ULONG\(\(PULONG\)&reg->InterruptMask,\s*oldMask\)',
    'STATUS_DEVICE_DATA_ERROR') `
    'timestamp configuration does not restore its prior state on failure'

Write-Host '=== 1.42 package metadata ==='
Assert-Match $inf 'DriverVer\s*=\s*08/01/2026,1\.42\.0\.0' `
    'INF version is not 1.42.0.0'
Assert-Match $package 'TimeCard-1\.42-x64\.cab' `
    'submission CAB name is not version 1.42'
Assert-Match $providerResource 'FILEVERSION\s+1,42,0,0' `
    'W32Time provider file version is not 1.42.0.0'
Assert-Match $providerBuild `
    'rc\.exe[\s\S]*?timecard_time_provider\.rc[\s\S]*?timecard_time_provider\.res' `
    'the W32Time provider build does not link its version resource'
Assert-Match $controlAssembly `
    'AssemblyFileVersion\("1\.42\.0\.0"\)' `
    'Control Center file version is not 1.42.0.0'
Assert-Match $serviceAssembly `
    'AssemblyFileVersion\("1\.42\.0\.0"\)' `
    'oscillatord service file version is not 1.42.0.0'
Assert-Match $serviceRuntime `
    'ServiceVersion\s*=\s*"1\.42\.0"' `
    'oscillatord monitoring version is not 1.42.0'
Assert-Match $ptp 'DriverVersion\s*=\s*0x0001002au' `
    'runtime driver version is not 1.42'

Write-Host "Advanced FPGA static tests passed ($assertions assertions; no hardware accessed)."
