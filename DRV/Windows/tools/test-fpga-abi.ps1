[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content (Join-Path $root 'include\timecard_ioctl.h') -Raw
$internal = Get-Content (Join-Path $root 'timecard.h') -Raw
$driver = Get-Content (Join-Path $root 'driver.c') -Raw
$fpga = Get-Content (Join-Path $root 'fpga.c') -Raw
$nmea = Get-Content (Join-Path $root 'nmea.c') -Raw
$ptp = Get-Content (Join-Path $root 'ptp.c') -Raw
$sma = Get-Content (Join-Path $root 'sma.c') -Raw
$timing = Get-Content (Join-Path $root 'timing.c') -Raw
$ioctl = Get-Content (Join-Path $root 'ioctl.c') -Raw
$tool = Get-Content (Join-Path $root 'tools\timecardctl.c') -Raw
$models = Get-Content (Join-Path $root `
    'TimeCardControlCenter\Models.cs') -Raw
$client = Get-Content (Join-Path $root `
    'TimeCardControlCenter\TimeCardClient.cs') -Raw
$window = Get-Content (Join-Path $root `
    'TimeCardControlCenter\MainWindow.xaml') -Raw
$windowCode = Get-Content (Join-Path $root `
    'TimeCardControlCenter\MainWindow.Fpga.cs') -Raw
$mainWindowCode = Get-Content (Join-Path $root `
    'TimeCardControlCenter\MainWindow.xaml.cs') -Raw
$productCode = Get-Content (Join-Path $root `
    'TimeCardControlCenter\ControlCenterProduct.cs') -Raw
$productWindowCode = Get-Content (Join-Path $root `
    'TimeCardControlCenter\MainWindow.Product.cs') -Raw
$project = Get-Content (Join-Path $root `
    'TimeCardControlCenter\TimeCardControlCenter.csproj') -Raw
$driverReadme = Get-Content (Join-Path $root 'README.md') -Raw
$signalManualPath = Join-Path $root `
    '..\..\SOM\FPGA\Doc\Clk_SignalGenerator_ReferenceManual.pdf'

function Assert-Match {
    param([string]$Text, [string]$Pattern, [string]$Description)
    if ($Text -notmatch $Pattern) {
        throw "FPGA ABI contract missing: $Description"
    }
}

function Assert-NoMatch {
    param([string]$Text, [string]$Pattern, [string]$Description)
    if ($Text -match $Pattern) {
        throw "FPGA ABI safety contract violated: $Description"
    }
}

function Assert-ManagedLayout {
    param(
        [string]$Type,
        [string[]]$ExpectedFields,
        [int]$ExpectedSize
    )

    $pattern = '(?s)internal\s+struct\s+' +
        [regex]::Escape($Type) + '\s*\{(?<Body>.*?)\}'
    $match = [regex]::Match($models, $pattern)
    if (-not $match.Success) {
        throw "FPGA ABI contract missing: managed $Type layout"
    }
    $fields = @([regex]::Matches(
        $match.Groups['Body'].Value,
        'public\s+(?:uint|int)\s+(?<Name>[A-Za-z0-9_]+)\s*;') |
        ForEach-Object { $_.Groups['Name'].Value })
    if (($fields -join ',') -ne ($ExpectedFields -join ',')) {
        throw "FPGA ABI layout mismatch for ${Type}: $($fields -join ', ')"
    }
    if (($fields.Count * 4) -ne $ExpectedSize) {
        throw "FPGA ABI size mismatch for ${Type}: expected $ExpectedSize"
    }
}

Assert-Match $header 'TIMECARD_ABI_VERSION\s+15u' 'ABI 15'

$nativeLayouts = [ordered]@{
    TIMECARD_FPGA_CAPABILITIES = 64
    TIMECARD_FPGA_IMAGE_INFO = 64
    TIMECARD_CLOCK_TELEMETRY = 80
    TIMECARD_PPS_CONTROL = 64
    TIMECARD_TIMECODE_CONTROL = 80
    TIMECARD_TOD_CONTROL = 80
}
foreach ($entry in $nativeLayouts.GetEnumerator()) {
    Assert-Match $fpga (
        'C_ASSERT\(sizeof\(' + [regex]::Escape($entry.Key) +
        '\)\s*==\s*' + $entry.Value + '\)') (
        "$($entry.Key) $($entry.Value)-byte native layout")
}

$ioctlContracts = @(
    @{ Native = 'GET_FPGA_CAPABILITIES'; Managed = 'IoctlFpgaCapabilities'; Number = 39; Access = 'FILE_READ_ACCESS'; ManagedAccess = 'FileReadAccess' },
    @{ Native = 'CLOCK_TELEMETRY_QUERY'; Managed = 'IoctlClockTelemetry'; Number = 40; Access = 'FILE_READ_ACCESS'; ManagedAccess = 'FileReadAccess' },
    @{ Native = 'PPS_QUERY'; Managed = 'IoctlPpsQuery'; Number = 41; Access = 'FILE_READ_ACCESS\s*\|\s*FILE_WRITE_ACCESS'; ManagedAccess = 'FileReadAccess\s*\|\s*FileWriteAccess' },
    @{ Native = 'PPS_SET'; Managed = 'IoctlPpsSet'; Number = 42; Access = 'FILE_READ_ACCESS\s*\|\s*FILE_WRITE_ACCESS'; ManagedAccess = 'FileReadAccess\s*\|\s*FileWriteAccess' },
    @{ Native = 'TIMECODE_QUERY'; Managed = 'IoctlTimecodeQuery'; Number = 43; Access = 'FILE_READ_ACCESS\s*\|\s*FILE_WRITE_ACCESS'; ManagedAccess = 'FileReadAccess\s*\|\s*FileWriteAccess' },
    @{ Native = 'TIMECODE_SET'; Managed = 'IoctlTimecodeSet'; Number = 44; Access = 'FILE_READ_ACCESS\s*\|\s*FILE_WRITE_ACCESS'; ManagedAccess = 'FileReadAccess\s*\|\s*FileWriteAccess' },
    @{ Native = 'TOD_QUERY'; Managed = 'IoctlTodQuery'; Number = 45; Access = 'FILE_READ_ACCESS\s*\|\s*FILE_WRITE_ACCESS'; ManagedAccess = 'FileReadAccess\s*\|\s*FileWriteAccess' },
    @{ Native = 'TOD_SET'; Managed = 'IoctlTodSet'; Number = 46; Access = 'FILE_READ_ACCESS\s*\|\s*FILE_WRITE_ACCESS'; ManagedAccess = 'FileReadAccess\s*\|\s*FileWriteAccess' }
    @{ Native = 'FPGA_IMAGE_QUERY'; Managed = 'IoctlFpgaImageQuery'; Number = 47; Access = 'FILE_READ_ACCESS'; ManagedAccess = 'FileReadAccess' }
)
foreach ($contract in $ioctlContracts) {
    Assert-Match $header (
        '(?s)#define\s+IOCTL_TIMECARD_' + $contract.Native +
        '\s*\\\s*TIMECARD_IOCTL\(' + $contract.Number + '\s*,\s*' +
        $contract.Access + '\s*\)') "native IOCTL $($contract.Number) access"
    Assert-Match $client (
        [regex]::Escape($contract.Managed) + '\s*=\s*ControlCode\(' +
        $contract.Number + '\s*,\s*' + $contract.ManagedAccess + '\s*\)') (
        "managed IOCTL $($contract.Number) access")
    Assert-Match $ioctl (
        'case\s+IOCTL_TIMECARD_' + $contract.Native + '\s*:') (
        "IOCTL $($contract.Number) dispatch")
}

Assert-ManagedLayout 'TimeCardFpgaCapabilitiesRaw' @(
    'Size', 'AbiVersion', 'CoreMask', 'FeatureFlags', 'KnownGaps', 'Layout',
    'BoardProfile', 'Reserved0', 'Reserved1', 'Reserved2', 'Reserved3',
    'Reserved4', 'Reserved5', 'Reserved6', 'Reserved7', 'Reserved8') 64
Assert-ManagedLayout 'TimeCardFpgaImageInfoRaw' @(
    'Size', 'AbiVersion', 'Flags', 'RawVersion', 'ImageTag', 'ImageVersion',
    'Layout', 'BoardProfile', 'RegisterOffset', 'Reserved0', 'Reserved1',
    'Reserved2', 'Reserved3', 'Reserved4', 'Reserved5', 'Reserved6') 64
Assert-ManagedLayout 'TimeCardClockTelemetryRaw' @(
    'Size', 'Flags', 'Version', 'Control', 'Status', 'Select', 'CoreMask',
    'KnownGaps', 'InSyncThreshold', 'ServoOffsetP', 'ServoOffsetI',
    'ServoDriftP', 'ServoDriftI', 'StatusOffsetNanoseconds', 'StatusDriftPpb',
    'StatusOffsetFraction', 'StatusDriftFraction', 'Reserved0', 'Reserved1',
    'Reserved2') 80
Assert-ManagedLayout 'TimeCardPpsControlRaw' @(
    'Size', 'Core', 'Flags', 'Control', 'Status', 'Version', 'Polarity',
    'PulseWidthMilliseconds', 'CableDelayNanoseconds', 'Reserved0',
    'Reserved1', 'Reserved2', 'Reserved3', 'Reserved4', 'Reserved5',
    'Reserved6') 64
Assert-ManagedLayout 'TimeCardTimecodeControlRaw' @(
    'Size', 'Format', 'Role', 'Flags', 'Control', 'Status', 'Version', 'Mode',
    'Code', 'CorrectionSeconds', 'DelayNanoseconds', 'ControlBits',
    'BitPosition', 'AmplitudeModulation', 'ManualYear', 'Reserved0',
    'Reserved1', 'Reserved2', 'Reserved3', 'Reserved4') 80
Assert-ManagedLayout 'TimeCardTodControlRaw' @(
    'Size', 'Flags', 'Control', 'Status', 'Version', 'Protocol', 'Gnss',
    'Baud', 'BaudSelector', 'Polarity', 'CorrectionSeconds',
    'MessageDisableMask', 'UtcStatus', 'TimeToLeapSeconds', 'GnssStatus',
    'Satellites', 'Reserved0', 'Reserved1', 'Reserved2', 'Reserved3') 80

Assert-Match $internal 'TIMECARD_PPS_MASTER_OFFSET_MSI\s+0x01030000u' `
    'MSI PPS master map'
Assert-Match $internal 'TIMECARD_DCF_MASTER_OFFSET_MSIX\s+0x030a0000u' `
    'MSI-X DCF master map'
Assert-Match $internal 'TIMECARD_IMAGE_OFFSET_MSI\s+0x00020000u' `
    'trusted MSI image-version offset'
Assert-Match $internal 'TIMECARD_IMAGE_OFFSET_MSIX\s+0x02020000u' `
    'trusted MSI-X image-version offset'
Assert-Match $driver 'context->PpsMaster\s*=\s*NULL;' `
    'ART-safe PPS pointer reset'
Assert-Match $fpga '!TimeCardStandardFpga\(context\)' `
    'standard-board access guard'
Assert-Match $fpga 'TimeCardCoreMaskForProfile' `
    'trusted static board-profile core map'
Assert-NoMatch $fpga 'TimeCardCoreMaskLocked' `
    'capability discovery must not sweep optional BAR windows'
Assert-Match $fpga 'TimeCardVersionPresent\([^)]*Version' `
    'common version register validation'
Assert-Match $fpga 'context->Layout\s*==\s*TIMECARD_LAYOUT_MSI[\s\S]*TIMECARD_IMAGE_OFFSET_MSI[\s\S]*context->Layout\s*==\s*TIMECARD_LAYOUT_MSIX[\s\S]*TIMECARD_IMAGE_OFFSET_MSIX[\s\S]*else\s*\r?\n\s*return STATUS_NOT_SUPPORTED' `
    'ART-safe image query restricted to the two static layouts'
Assert-Match $fpga 'TimeCardFpgaImageQuery[\s\S]*!TimeCardStandardFpga\(context\)[\s\S]*return STATUS_NOT_SUPPORTED' `
    'image query restricted to trusted standard board profiles'
Assert-Match $fpga 'rawVersion\s*==\s*0u\s*\|\|\s*rawVersion\s*==\s*MAXULONG' `
    'invalid image word rejection'
Assert-Match $fpga '\(decoded\s*&\s*0xffffu\)\s*==\s*0u[\s\S]*decoded\s*>>=\s*16[\s\S]*TIMECARD_FPGA_IMAGE_FLAG_LOADER' `
    'Linux-compatible loader encoding decode'
Assert-Match $fpga 'ImageTag\s*=\s*decoded\s*>>\s*15[\s\S]*ImageVersion\s*=\s*decoded\s*&\s*0x7fffu' `
    'Linux-compatible image tag/version decode'

Assert-Match $fpga 'TimeCardFpgaContractAllows\([^)]*TIMECARD_FPGA_CONTRACT_CLOCK_SERVO_LOG' `
    'synthesis-optional Clock servo/log registers require an exact-image contract'
Assert-Match $fpga 'TimeCardFpgaContractAllows\([^)]*TIMECARD_FPGA_CONTRACT_TOD_TELEMETRY' `
    'synthesis-optional ToD telemetry requires an exact-image contract'
Assert-Match $header 'TIMECARD_FPGA_GAP_SYNTHESIS_FEATURE_REPORTING' `
    'unreported bitstream-synthesis capability gap'
Assert-Match $fpga 'TIMECARD_FPGA_GAP_SYNTHESIS_FEATURE_REPORTING' `
    'driver reports synthesis capability gap'

Assert-Match $fpga 'control\s*&\s*~TIMECARD_CORE_ENABLE' `
    'disable/configure/enable sequencing'
Assert-Match $fpga 'request->PulseWidthMilliseconds\s*==\s*0u\s*\|\|\s*\r?\n\s*request->PulseWidthMilliseconds\s*>\s*999u' `
    'documented PPS width bounds'
Assert-Match $fpga 'WRITE_REGISTER_ULONG\(\(PULONG\)&reg->Status,\s*errorMask\)' `
    'explicit PPS W1C clearing'
Assert-Match $fpga 'STATUS_DEVICE_DATA_ERROR' 'FPGA configuration readback verification'
Assert-Match $nmea 'request->Polarity\s*!=\s*0u\s*\?\s*0u\s*:\s*1u' `
    'logical-to-register NMEA polarity conversion'
Assert-Match $nmea 'STATUS_DEVICE_DATA_ERROR' 'NMEA readback verification'
Assert-Match $header 'TIMECARD_NMEA_FLAG_ADVANCED_VALID' `
    'backward-compatible advanced NMEA request flag'
Assert-Match $header 'TIMECARD_NMEA_FLAG_ERROR\s+\(1u\s*<<\s*3\)' `
    'ToD Master sticky transmitter-error response flag'
Assert-Match $header 'TIMECARD_NMEA_FLAG_CLEAR_ERROR\s+\(1u\s*<<\s*31\)' `
    'explicit ToD Master W1C request flag'
Assert-Match $header 'CorrectionSeconds[\s\S]*LocalOffsetMinutes[\s\S]*Gnss[\s\S]*MessageDisableMask' `
    'fixed-size NMEA reserved-field extension'
Assert-Match $nmea 'TimeCardNmeaSupportedMessageMask[\s\S]*1u, 4u[\s\S]*1u, 6u' `
    'ToD Master RMC and UTC revision gates'
Assert-Match $nmea 'TimeCardNmeaVersionAtLeast\(version, 1u, 1u\)[\s\S]*NmeaOut->Status[\s\S]*control->Flags\s*\|=\s*TIMECARD_NMEA_FLAG_ERROR' `
    'ToD Master 1.1 sticky transmitter-error query'
Assert-Match $nmea 'TIMECARD_NMEA_FLAG_CLEAR_ERROR[\s\S]*TimeCardNmeaVersionAtLeast\(current\.Version, 1u, 1u\)[\s\S]*TIMECARD_NMEA_FLAG_CLEAR_ERROR[\s\S]*WRITE_REGISTER_ULONG\(\(PULONG\)&context->NmeaOut->Status,\s*TIMECARD_NMEA_STATUS_ERROR\)' `
    'explicit revision-gated ToD Master status W1C'
$nmeaStatusWrites = [regex]::Matches($nmea,
    'WRITE_REGISTER_ULONG\(\(PULONG\)&context->NmeaOut->Status').Count
if ($nmeaStatusWrites -ne 1) {
    throw "FPGA ABI safety contract violated: expected one explicit NMEA status write, found $nmeaStatusWrites"
}
Assert-Match $nmea 'Ctrl,\s*control\s*&\s*~TIMECARD_NMEA_ENABLE[\s\S]*Correction[\s\S]*LocalOffset[\s\S]*Ctrl,\s*control' `
    'ToD Master disabled configuration transaction'
Assert-Match $nmea 'TimeCardNmeaUtcQuery[\s\S]*TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ' `
    'optional NMEA UTC reads require an exact-image contract'
Assert-Match $nmea 'TimeCardNmeaUtcSet[\s\S]*TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ[\s\S]*TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_WRITE' `
    'optional NMEA UTC writes require read/write exact-image contracts'
Assert-NoMatch $driver 'TimeCardNmeaInitialize\s*\(' `
    'D0 entry must not probe or initialize the optional ToD Master'
Assert-NoMatch $driver 'TimeCardEvtD0Entry[\s\S]*TimeCardNmea(Query|Set|Initialize)\s*\(' `
    'D0 entry must not perform any ToD Master MMIO transaction'
Assert-NoMatch $internal 'TimeCardNmeaInitialize\s*\(' `
    'automatic ToD Master initializer must not remain callable'
Assert-NoMatch $nmea 'TimeCardNmeaInitialize\s*\(' `
    'automatic ToD Master initializer implementation must stay removed'
Assert-Match $ioctl 'IOCTL_TIMECARD_NMEA_QUERY[\s\S]*TimeCardNmeaQuery' `
    'explicit ToD Master query IOCTL remains available'
Assert-Match $ioctl 'IOCTL_TIMECARD_NMEA_SET[\s\S]*TimeCardNmeaSet' `
    'explicit ToD Master set IOCTL remains available'
Assert-Match $client 'SetNmeaOutputCore[\s\S]*advancedValid\s*\?\s*4u' `
    'managed advanced-valid request contract'
Assert-Match $models 'NmeaOutputState[\s\S]*HasError\s*=\s*\(value\.Flags\s*&\s*8u\)' `
    'managed NMEA sticky-error state'
Assert-Match $client 'SetNmeaOutput[\s\S]*bool clearError[\s\S]*clearError\s*\?\s*0x80000000u' `
    'managed explicit NMEA clear-error overload'
Assert-Match $mainWindowCode 'ApplyNmeaState[\s\S]*state\.HasError[\s\S]*TRANSMITTER ERROR' `
    'Control Center NMEA sticky-error indication'
Assert-Match $tool 'transmitter %s[\s\S]*TIMECARD_NMEA_FLAG_ERROR' `
    'timecardctl NMEA sticky-error status output'
Assert-Match $tool 'TIMECARD_NMEA_FLAG_CLEAR_ERROR' `
    'timecardctl explicit NMEA clear request'
Assert-Match $tool 'nmea-set <on\|off> <baud> \[normal\|inverted\] \[clear\]' `
    'legacy timecardctl NMEA clear syntax'
Assert-Match $tool '<disable-mask> \[clear\]' `
    'advanced timecardctl NMEA clear syntax'
Assert-Match $window 'NmeaCorrectionTextBox[\s\S]*NmeaLocalOffsetTextBox[\s\S]*NmeaGnssCombo[\s\S]*NmeaDisableUtcCheckBox' `
    'advanced NMEA Control Center panel'
Assert-Match $productWindowCode 'HasNmeaAdvanced[\s\S]*NmeaMessageDisableMask' `
    'advanced NMEA profile round-trip'
Assert-Match $timing 'request->RepeatCount' 'signal repeat-count programming'
Assert-Match $timing 'request->CableDelayNanoseconds' `
    'signal cable-delay programming'
Assert-Match $timing 'TIMECARD_SIGNAL_FLAG_TIME_JUMP' `
    'signal time-jump decoding'
Assert-Match $timing 'oldInterruptMask\s*&\s*~1u' `
    'masked signal-generator IRQ'
Assert-Match $timing 'TimeCardReadClockLocked\(context,\s*&now\)[\s\S]*oldInterruptMask\s*=\s*READ_REGISTER_ULONG' `
    'signal PHC calculation precedes runtime-state mutation'
Assert-Match $header 'TIMECARD_SIGNAL_FLAG_ABSOLUTE_START\s+\(1u\s*<<\s*5\)' `
    'request-only absolute-start flag uses the existing free bit'
Assert-Match $header 'TIMECARD_SIGNAL_FLAG_CLEAR_STATUS\s+\(1u\s*<<\s*6\)' `
    'request-only signal sticky-status clear flag'
Assert-Match $timing 'C_ASSERT\(sizeof\(TIMECARD_SIGNAL_CONTROL\)\s*==\s*64\)' `
    'absolute start preserves the 64-byte signal ABI'
Assert-Match $header 'TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH\s+\(1u\s*<<\s*2\)[\s\S]*TIMECARD_SIGNAL_FLAG_INVERTED\s+TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH' `
    'manual-correct active-high flag keeps the legacy source alias'
Assert-Match $timing 'absoluteStart[\s\S]*request->StartSeconds\s*>\s*MAXULONG[\s\S]*request->StartNanoseconds\s*>=\s*TIMECARD_NANOSECONDS_PER_SECOND' `
    'absolute start validates 32-bit seconds and nanoseconds'
Assert-Match $timing 'earliestStart\s*=\s*now\s*\+\s*TIMECARD_SIGNAL_START_GUARD_NS[\s\S]*if\s*\(start\s*<=\s*earliestStart\)' `
    'absolute start is safely ahead of a fresh PHC sample'
Assert-Match $timing 'reg->StartSeconds,\s*startSeconds[\s\S]*reg->StartNanoseconds,\s*startNanoseconds' `
    'exact absolute start registers are programmed'
Assert-Match $timing 'absoluteStart\s*\?[\s\S]*response->StartSeconds\s*!=\s*request->StartSeconds[\s\S]*response->StartNanoseconds\s*!=\s*request->StartNanoseconds' `
    'absolute start receives exact readback verification'
$signalQuery = [regex]::Match($timing,
    '(?s)static\s+NTSTATUS\s+TimeCardSignalQueryLocked.*?\n\}\s*\n\s*NTSTATUS\s+TimeCardSignalQuery')
if (-not $signalQuery.Success) {
    throw 'FPGA ABI contract missing: signal-generator query implementation'
}
Assert-NoMatch $signalQuery.Value 'TIMECARD_SIGNAL_FLAG_ABSOLUTE_START' `
    'query must not echo the request-only absolute-start flag'
Assert-NoMatch $signalQuery.Value 'TIMECARD_SIGNAL_FLAG_CLEAR_STATUS' `
    'query must not echo the request-only status-clear flag'
Assert-Match $timing 'clearOnly[\s\S]*reg->Status,[\s\S]*TIMECARD_SIGNAL_STATUS_ERROR\s*\|[\s\S]*TIMECARD_SIGNAL_STATUS_TIME_JUMP[\s\S]*TimeCardSignalQueryLocked' `
    'signal clear-only request performs explicit W1C without reconfiguration'
Assert-Match $timing 'rollback_runtime:[\s\S]*reg->Polarity,\s*oldPolarity[\s\S]*reg->CableDelay,\s*oldCableDelay[\s\S]*reg->RepeatCount,\s*oldRepeatCount[\s\S]*reg->StartSeconds,\s*oldStartSeconds[\s\S]*reg->PeriodSeconds,\s*oldPeriodSeconds[\s\S]*reg->PulseSeconds,\s*oldPulseSeconds[\s\S]*reg->InterruptMask,[\s\S]*oldInterruptMask[\s\S]*reg->Enable,\s*oldControl' `
    'signal failure restores the complete register transaction'
Assert-Match $timing 'verify:\s*\r?\n\s*status\s*=\s*TimeCardSignalQueryLocked\(context,\s*request->Generator,\s*response\);\s*\r?\n\s*if\s*\(!NT_SUCCESS\(status\)\)\s*\r?\n\s*goto rollback_runtime' `
    'signal readback failure enters runtime-state rollback'
Assert-Match $timing 'STATUS_DEVICE_DATA_ERROR' 'signal readback verification'
Assert-Match $tool 'cmd_signal_set_at[\s\S]*TIMECARD_SIGNAL_FLAG_ABSOLUTE_START[\s\S]*control\.StartSeconds[\s\S]*control\.StartNanoseconds' `
    'timecardctl exact-PHC signal-set-at command'
Assert-Match $tool 'cmd_signal_clear[\s\S]*TIMECARD_SIGNAL_FLAG_CLEAR_STATUS' `
    'timecardctl explicit signal sticky-status clear command'
Assert-Match $tool 'active-high[\s\S]*inverted[\s\S]*active-low[\s\S]*normal' `
    'timecardctl manual polarity names and legacy aliases'
Assert-Match $client 'SetSignalGeneratorAt[\s\S]*startSeconds\s*>\s*uint\.MaxValue[\s\S]*startNanoseconds\s*>=\s*1000000000u[\s\S]*absoluteStart\s*\?\s*0x20u' `
    'managed exact-PHC start overload and bounds'
Assert-Match $client 'ClearSignalGeneratorStatus[\s\S]*Flags\s*=\s*0x40u' `
    'managed explicit signal sticky-status clear operation'
Assert-Match $models 'SignalGeneratorState[\s\S]*IsActiveHigh\s*=\s*\(value\.Flags\s*&\s*4u\)' `
    'managed signal polarity uses active-high semantics'
Assert-Match $window 'Generator1StartModeCombo[\s\S]*Next PHC-aligned[\s\S]*Exact PHC time[\s\S]*Generator4StartTextBox' `
    'compact two-mode scheduling controls for all generators'
Assert-Match $mainWindowCode 'ParsePhcStart[\s\S]*SetSignalGeneratorAt' `
    'Control Center parses and sends exact PHC starts'
Assert-Match $window 'Generator1ClearButton[\s\S]*Generator4ClearButton' `
    'Control Center exposes clear status for every generator'
Assert-Match $mainWindowCode 'ClearSignalGeneratorStatus_Click[\s\S]*client\.ClearSignalGeneratorStatus' `
    'Control Center issues the explicit clear-only request'
Assert-Match $productCode 'SignalGeneratorProfileSetting[\s\S]*ActiveHigh[\s\S]*ShouldSerializeInverted' `
    'new active-high profiles import the legacy polarity element'
$signalProfile = [regex]::Match($productCode,
    '(?s)class\s+SignalGeneratorProfileSetting\s*\{(?<Body>.*?)\n\s*\}')
if (-not $signalProfile.Success) {
    throw 'FPGA ABI contract missing: signal-generator profile model'
}
Assert-NoMatch $signalProfile.Groups['Body'].Value `
    'AbsoluteStart|StartSeconds|StartNanoseconds' `
    'one-shot absolute start must not persist in profiles'
if (-not (Test-Path -LiteralPath $signalManualPath)) {
    throw 'FPGA ABI contract missing: Clk SignalGenerator reference manual'
}
Assert-Match $driverReadme 'section 3\.2\.1\.3[\s\S]*active-high[\s\S]*active-low' `
    'manual-backed polarity contract is documented'

Assert-Match $sma 'TimeCardRefreshRoutedCoresLocked\(\s*\r?\n?\s*PDEVICE_CONTEXT context,\s*\r?\n?\s*ULONG previousDirection,\s*\r?\n?\s*ULONG previousFunction,\s*\r?\n?\s*ULONG currentDirection,\s*\r?\n?\s*ULONG currentFunction' `
    'route refresh receives old and new route state'
Assert-Match $sma '(?s)previousDirection.*currentDirection' `
    'route refresh scopes affected timecode cores'
Assert-NoMatch $driver 'TimeCardRefreshRoutedCoresLocked' `
    'D0 entry must not silently enable routed timing cores'
Assert-Match $tool 'IOCTL_TIMECARD_TIMECODE_QUERY' `
    'timecardctl preserves current timecode fields'
Assert-Match $tool 'control\.ControlBits\s*=\s*current\.ControlBits' `
    'timecardctl preserves IRIG control bits'
Assert-Match $tool 'IOCTL_TIMECARD_FPGA_IMAGE_QUERY' `
    'timecardctl image identity query'
Assert-Match $tool 'loader encoding' `
    'timecardctl image identity decoding output'
Assert-Match $ptp 'Regs->Select,\s*select\s*&\s*0xffu' `
    'PHC adjustment restores the requested clock source'

foreach ($command in 'fpga-status', 'clock-telemetry', 'pps-status',
         'timecode-status', 'tod-status', 'signal-status') {
    Assert-Match $tool ([regex]::Escape($command)) "timecardctl $command"
}
Assert-Match $window 'x:Name="FpgaPage"' 'FPGA Engines workspace'
Assert-Match $window 'x:Name="PpsMasterWidthTextBox"' 'PPS controls'
Assert-Match $window 'x:Name="TodParserProtocolCombo"' 'ToD parser controls'
Assert-Match $window 'TodParserEsipCrwCheckBox' 'ESIP CRW message gate'
Assert-Match $window 'TodParserEsipCryCheckBox' 'ESIP CRY message gate'
Assert-Match $window 'TodParserEsipCrjCheckBox' 'ESIP CRJ message gate'
Assert-Match $window 'TextWrapping="Wrap"' 'narrow-layout text wrapping'
Assert-Match $windowCode 'HasFpgaFeature' 'feature-gated write controls'
Assert-Match $windowCode 'FpgaImageRequiredAbi\s*=\s*13' `
    'Control Center image identity ABI gate'
Assert-Match $windowCode 'GetFpgaImageInfo' `
    'Control Center image identity diagnostics'
Assert-Match $windowCode 'no standard static image register is assumed' `
    'Control Center ART safety explanation'
Assert-Match $productCode 'HasFpgaImageIdentity' `
    'configuration profile image compatibility metadata'
Assert-Match $productWindowCode `
    'image\.BoardProfile\s*!=\s*profile\.FpgaImageBoardProfile[\s\S]*image\.Layout\s*!=\s*profile\.FpgaImageLayout[\s\S]*image\.RawVersion\s*!=\s*profile\.FpgaImageRawVersion[\s\S]*image\.ImageTag\s*!=\s*profile\.FpgaImageTag[\s\S]*image\.ImageVersion\s*!=\s*profile\.FpgaImageVersion[\s\S]*image\.IsLoader\s*!=\s*profile\.FpgaImageLoaderEncoding' `
    'configuration profile full FPGA image compatibility check'
Assert-Match $productWindowCode `
    'Profile FPGA image mismatch:[\s\S]*No configuration was written' `
    'configuration profile image mismatch safety error'
$captureStart = $productWindowCode.IndexOf(
    'private ConfigurationProfile CaptureConfigurationCore',
    [StringComparison]::Ordinal)
$captureEnd = if ($captureStart -ge 0) {
    $productWindowCode.IndexOf('private void ApplyConfigurationCore',
        $captureStart, [StringComparison]::Ordinal)
} else { -1 }
if ($captureStart -lt 0 -or $captureEnd -le $captureStart) {
    throw 'FPGA ABI safety contract violated: profile capture method not found'
}
$captureBody = $productWindowCode.Substring($captureStart,
    $captureEnd - $captureStart)
Assert-NoMatch $captureBody 'GetNmeaOutput\s*\(' `
    'profile capture must not probe the synthesis-optional ToD Master'
Assert-Match $captureBody `
    'Do not query the optional ToD Master[\s\S]*image-specific synthesis manifest' `
    'profile capture documents the trusted-manifest gate'
Assert-Match $productWindowCode `
    'if\s*\(profile\.HasNmea\)[\s\S]*client\.GetNmeaOutput\s*\(' `
    'explicit profile NMEA operation remains available'
Assert-Match $windowCode 'master\s*\?\s*\r?\n\s*FpgaVersionAtLeast\(state\.Version,\s*1u,\s*4u\)\s*:\s*\r?\n\s*FpgaVersionAtLeast\(state\.Version,\s*1u,\s*2u\)' `
    'PPS master/slave revision-aware write gates'
Assert-Match $windowCode 'maximumDelay\s*=\s*FpgaVersionAtLeast\(current\.Version,\s*1u,\s*6u\)\s*\?\s*\r?\n\s*0x3fffffffL\s*:\s*0xffffL' `
    'PPS revision-aware cable-delay range'
Assert-Match $windowCode 'IrigMasterModeCombo,\s*2u,\s*\r?\n\s*irigGSupported' `
    'IRIG master G requires core v1.3'
Assert-Match $windowCode 'IrigSlaveModeCombo\.IsEnabled\s*=\s*modeWritable' `
    'IRIG slave mode is fixed before core v1.3'
Assert-Match $windowCode 'TodParserGnssCombo\.IsEnabled\s*=\s*enabled\s*&&\s*\r?\n\s*FpgaVersionAtLeast\(version,\s*1u,\s*5u\)' `
    'ToD GNSS selector requires core v1.5'
Assert-Match $windowCode 'TodParserInvertCheckBox\.IsEnabled\s*=\s*enabled\s*&&\s*\r?\n\s*FpgaVersionAtLeast\(version,\s*1u,\s*3u\)' `
    'ToD polarity requires core v1.3'
Assert-Match $windowCode 'case\s+1u:\s*return\s+FpgaVersionAtLeast\(version,\s*1u,\s*6u\)' `
    'ToD UBX protocol requires core v1.6'
Assert-Match $windowCode 'case\s+2u:\s*return\s+FpgaVersionAtLeast\(version,\s*1u,\s*9u\)' `
    'ToD TSIP protocol requires core v1.9'
Assert-Match $windowCode 'case\s+3u:\s*return\s+FpgaVersionAtLeast\(version,\s*2u,\s*1u\)' `
    'ToD ESIP protocol requires core v2.1'
Assert-Match $windowCode 'FpgaVersionAtLeast\(version,\s*2u,\s*0u\)\s*\?\s*0x04u\s*:\s*0u' `
    'ToD NMEA UTC message requires core v2.0'
Assert-Match $windowCode 'FpgaVersionAtLeast\(version,\s*1u,\s*7u\)\s*\?\s*0x1fu\s*:\s*0x07u' `
    'ToD UBX GNSS messages require core v1.7'
Assert-Match $windowCode 'supportedMessageMask\s*&\s*0x80u' `
    'ToD apply masks unsupported per-protocol message gates'
Assert-Match $windowCode 'MessageDisableMask\s*&\s*0x20u' 'ESIP CRW mask'
Assert-Match $windowCode 'MessageDisableMask\s*&\s*0x40u' 'ESIP CRY mask'
Assert-Match $windowCode 'MessageDisableMask\s*&\s*0x80u' 'ESIP CRJ mask'
Assert-Match $windowCode 'synthesis' 'synthesis-capability warning'
Assert-Match $windowCode 'not read' 'optional telemetry is not presented as zero'
Assert-Match $windowCode 'RefreshFpgaCoresAsync' 'FPGA refresh implementation'
Assert-Match $windowCode 'SetPpsEngine' 'PPS apply implementation'
Assert-Match $windowCode 'SetTimecodeEngine' 'timecode apply implementation'
Assert-Match $windowCode 'SetTodParser' 'ToD apply implementation'
Assert-Match $project 'Compile Include="MainWindow.Fpga.cs"' `
    'FPGA workspace source in application build'

Write-Host 'FPGA ABI, safety, and Control Center static contracts passed.'
