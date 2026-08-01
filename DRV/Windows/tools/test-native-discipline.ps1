[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root `
    'TimeCardControlCenter\bin\Release\TimeCardDiscipline.dll'
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Native discipline DLL not found: $dll"
}
$escapedDll = ([IO.Path]::GetFullPath($dll)).Replace('\', '\\')
$source = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

[StructLayout(LayoutKind.Sequential)]
internal struct SmokeInput
{
    internal double Temperature;
    internal long Phase;
    internal uint Fine;
    internal int Coarse;
    internal int Qerr;
    internal uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
internal struct SmokeOutput
{
    internal uint Action;
    internal uint Setpoint;
    internal int PhaseJump;
    internal uint State;
    internal uint ClockClass;
    internal int Count;
    internal int Threshold;
    internal float Progress;
    internal uint HoldoverReady;
}

public static class NativeDisciplineSmoke
{
    [DllImport("$escapedDll", CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "tcod_create", CharSet = CharSet.Ansi)]
    private static extern IntPtr Create(uint coarse, byte[] saved,
        uint savedLength, StringBuilder error, uint errorLength);
    [DllImport("$escapedDll", CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "tcod_process")]
    private static extern int Process(IntPtr context, ref SmokeInput input,
        out SmokeOutput output);
    [DllImport("$escapedDll", CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "tcod_parameters_size")]
    private static extern uint ParametersSize();
    [DllImport("$escapedDll", CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "tcod_get_parameters")]
    private static extern int GetParameters(IntPtr context, byte[] buffer,
        uint bufferLength);
    [DllImport("$escapedDll", CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "tcod_destroy")]
    private static extern void Destroy(IntPtr context);

    public static string Run()
    {
        if (Marshal.SizeOf(typeof(SmokeInput)) != 32)
            throw new InvalidOperationException("Input ABI size mismatch.");
        if (Marshal.SizeOf(typeof(SmokeOutput)) != 36)
            throw new InvalidOperationException("Output ABI size mismatch.");
        StringBuilder error = new StringBuilder(1024);
        IntPtr context = Create(32768, null, 0, error,
            (uint)error.Capacity);
        if (context == IntPtr.Zero)
            throw new InvalidOperationException("Create failed: " + error);
        try
        {
            uint size = ParametersSize();
            if (size != 512)
                throw new InvalidOperationException(
                    "EEPROM ABI size is " + size + ", expected 512.");
            byte[] parameters = new byte[size];
            if (GetParameters(context, parameters, size) != 0)
                throw new InvalidOperationException("Parameter export failed.");
            if (parameters[0] != (byte)'O' || parameters[1] != 1 ||
                parameters[0x90] != (byte)'O' || parameters[0x91] != 1)
                throw new InvalidOperationException(
                    "Exported EEPROM headers are invalid.");
            SmokeInput input = new SmokeInput();
            input.Temperature = 42.0;
            input.Fine = 2400;
            input.Coarse = 32768;
            SmokeOutput output;
            int result = Process(context, ref input, out output);
            if (result != 0)
                throw new InvalidOperationException(
                    "Processing failed with " + result + ".");
            return "state=" + output.State + ", action=" + output.Action +
                ", eeprom=" + size;
        }
        finally
        {
            Destroy(context);
        }
    }
}
"@

Add-Type -TypeDefinition $source -Language CSharp
$result = [NativeDisciplineSmoke]::Run()

$application = Join-Path $root `
    'TimeCardControlCenter\bin\Release\TimeCardControlCenter.exe'
$assembly = [Reflection.Assembly]::LoadFrom($application)
$managedAbiSizes = @{
    TimeCardCapabilitiesRaw = 64
    TimeCardPhaseSampleRaw = 80
    TimeCardPhaseControlRaw = 32
    TimeCardPhcAdjustRaw = 48
    TimeCardDisciplineBlobRaw = 528
}
$sizeOfType = [Runtime.InteropServices.Marshal].GetMethod(
    'SizeOf', [type[]]@([type]))
foreach ($entry in $managedAbiSizes.GetEnumerator()) {
    $rawType = $assembly.GetType(
        'TimeCardControlCenter.' + $entry.Key, $true)
    $sizeArguments = New-Object object[] 1
    $sizeArguments[0] = $rawType
    $actualSize = $sizeOfType.Invoke($null, $sizeArguments)
    if ($actualSize -ne $entry.Value) {
        throw "Managed $($entry.Key) ABI size is $actualSize, expected $($entry.Value)."
    }
}
$engineType = $assembly.GetType(
    'TimeCardControlCenter.NativeDisciplineEngine', $true)
$decode = $engineType.GetMethod('DecodeGnssEpoch',
    [Reflection.BindingFlags]'Static,NonPublic')
if (-not $decode) {
    throw 'Native GNSS epoch decoder was not found.'
}
$dictionaryType = [type]'System.Collections.Generic.Dictionary[UInt16,Byte[]]'
$messages = [Activator]::CreateInstance($dictionaryType)
$timTp = [byte[]]::new(16)
[BitConverter]::GetBytes([int]-247).CopyTo($timTp, 8)
$navPvt = [byte[]]::new(24)
$navPvt[20] = 3
$navPvt[21] = 1
$timSvin = [byte[]]::new(26)
$timSvin[24] = 1
$timSvin[25] = 0
$messages.Add([uint16]0x0d01, $timTp)
$messages.Add([uint16]0x0107, $navPvt)
$messages.Add([uint16]0x0d04, $timSvin)
$decodeArguments = New-Object object[] 1
$decodeArguments[0] = $messages
$epoch = $decode.Invoke($null, $decodeArguments)
$epochFlags = [Reflection.BindingFlags]'Instance,Public,NonPublic'
function Epoch-Field([string]$Name) {
    return $epoch.GetType().GetField($Name, $epochFlags).GetValue($epoch)
}
if (-not (Epoch-Field 'HasTimePulse') -or
    (Epoch-Field 'QuantizationErrorPicoseconds') -ne -247 -or
    -not (Epoch-Field 'FixValid') -or
    -not (Epoch-Field 'HasSurvey') -or
    -not (Epoch-Field 'SurveyValid') -or
    (Epoch-Field 'SurveyActive')) {
    throw 'Native GNSS epoch decoding failed.'
}

$header = Get-Content -LiteralPath `
    (Join-Path $root 'include\timecard_ioctl.h') -Raw
foreach ($required in @(
    'TIMECARD_ABI_VERSION 11u',
    'IOCTL_TIMECARD_GET_CAPABILITIES',
    'IOCTL_TIMECARD_PHASE_QUERY',
    'IOCTL_TIMECARD_PHASE_CONTROL',
    'IOCTL_TIMECARD_PHC_ADJUST',
    'IOCTL_TIMECARD_DISCIPLINE_READ',
    'IOCTL_TIMECARD_DISCIPLINE_WRITE')) {
    if ($header -notmatch [regex]::Escape($required)) {
        throw "Driver ABI is missing $required"
    }
}

$driver = Get-Content -LiteralPath (Join-Path $root 'discipline.c') -Raw
$i2c = Get-Content -LiteralPath (Join-Path $root 'i2c.c') -Raw
$engine = Get-Content -LiteralPath (Join-Path $root `
    'TimeCardControlCenter\NativeDiscipline.cs') -Raw
$client = Get-Content -LiteralPath (Join-Path $root `
    'TimeCardControlCenter\TimeCardClient.cs') -Raw
$window = Get-Content -LiteralPath (Join-Path $root `
    'TimeCardControlCenter\MainWindow.Oscillatord.cs') -Raw
$tool = Get-Content -LiteralPath (Join-Path $root `
    'tools\timecardctl.c') -Raw
$invariants = @(
    @($driver, 'TIMECARD_BOARD_ART[\s\S]*TIMECARD_CAP_MRO50_DIRECT',
        'ART mRO capability gating'),
    @($driver, 'TimestampCount[\s\S]*before\s*==\s*after',
        'consistent timestamp latching'),
    @($driver, 'InterruptMask,\s*0u',
        'polling phase capture with interrupts masked'),
    @($driver, 'phase\s*%=\s*TIMECARD_NANOSECONDS_PER_SECOND',
        'constant-time phase normalization'),
    @($i2c, 'offset\s*<\s*256u;\s*offset\s*\+=\s*16u',
        'page-aligned EEPROM writes'),
    @($i2c, 'RtlCompareMemory\(verify,\s*request->Data',
        'EEPROM read-back verification'),
    @($client, 'GetCapabilities\(', 'managed capability query'),
    @($client, 'GetPhaseSample\(', 'managed phase query'),
    @($client, 'WriteDisciplineParameters\(',
        'managed EEPROM persistence'),
    @($client, 'CaptureUbxMessagesPreserveBaud\(',
        'baud-preserving GNSS epoch capture'),
    @($engine, 'NativeDisciplineAlgorithm\.Create',
        'native miniCOD engine startup'),
    @($engine, 'GetCalibrationPlan\([\s\S]*CompleteCalibration',
        'full miniCOD calibration'),
    @($engine, 'finally[\s\S]*SetMro50FineAdjustment\(originalFine\)',
        'calibration fine-control restoration'),
    @($engine, 'SetPhaseMeter\(false', 'phase capture cleanup'),
    @($engine, 'QuantizationErrorPicoseconds\s*=\s*quantizationError',
        'previous-epoch u-blox quantization error'),
    @($engine, 'gnss\.HasTimePulse[\s\S]*gnss\.FixValid[\s\S]*surveyCompleted',
        'GNSS pulse, fix, and survey gating'),
    @($window, 'HasDirectMro50[\s\S]*HasHardwareDiscipline',
        'variant-aware workspace routing'),
    @($tool, 'cmd_capabilities', 'command-line capability discovery'),
    @($tool, 'cmd_discipline_write', 'command-line EEPROM restore')
)
foreach ($invariant in $invariants) {
    if ($invariant[0] -notmatch $invariant[1]) {
        throw "Native discipline invariant missing: $($invariant[2])"
    }
}

Write-Host "Native discipline smoke test passed ($result)."
