[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root `
    'TimeCardDiscipline\bin\Release\TimeCardDiscipline.dll'
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Native discipline DLL not found: $dll"
}
$application = Join-Path $root `
    'TimeCardControlCenter\bin\Release\TimeCardControlCenter.exe'
if (-not (Test-Path -LiteralPath $application)) {
    throw "Control Center executable not found: $application"
}
$sidecar = Join-Path (Split-Path -Parent $application) `
    'TimeCardDiscipline.dll'
if (Test-Path -LiteralPath $sidecar) {
    throw 'The Control Center still has a loose TimeCardDiscipline.dll sidecar.'
}

$assembly = [Reflection.Assembly]::LoadFrom($application)
$resourceName = 'TimeCardControlCenter.Native.TimeCardDiscipline.dll'
if ($assembly.GetManifestResourceNames() -notcontains $resourceName) {
    throw 'The native miniCOD DLL is not embedded in the Control Center.'
}
$loaderType = $assembly.GetType(
    'TimeCardControlCenter.NativeDisciplineLibrary', $true)
$loaderFlags = [Reflection.BindingFlags]'Static,NonPublic'
$ensureLoaded = $loaderType.GetMethod('EnsureLoaded', $loaderFlags)
$loadedPathProperty = $loaderType.GetProperty('LoadedPath', $loaderFlags)
if (-not $ensureLoaded -or -not $loadedPathProperty) {
    throw 'The embedded native-library loader contract is incomplete.'
}
$ensureLoaded.Invoke($null, $null)
$loadedPath = [string]$loadedPathProperty.GetValue($null, $null)
if (-not (Test-Path -LiteralPath $loadedPath -PathType Leaf)) {
    throw "The embedded native library was not staged: $loadedPath"
}
$resourceStream = $assembly.GetManifestResourceStream($resourceName)
$sha256 = [Security.Cryptography.SHA256]::Create()
try {
    $resourceHash = [BitConverter]::ToString(
        $sha256.ComputeHash($resourceStream)).Replace('-', '')
} finally {
    $sha256.Dispose()
    $resourceStream.Dispose()
}
$loadedHash = (Get-FileHash -LiteralPath $loadedPath `
    -Algorithm SHA256).Hash
if ($loadedHash -ne $resourceHash) {
    throw 'The staged native library does not match the embedded resource.'
}
$algorithmType = $assembly.GetType(
    'TimeCardControlCenter.NativeDisciplineAlgorithm', $true)
$create = $algorithmType.GetMethod('Create',
    [Reflection.BindingFlags]'Static,Public', $null,
    [type[]]@([uint32], [byte[]]), $null)
if (-not $create) {
    throw 'The native discipline creation entry point was not found.'
}
$createArguments = New-Object object[] 2
$createArguments[0] = [uint32]32768
$createArguments[1] = $null
$embeddedAlgorithm = $create.Invoke($null, $createArguments)
try {
    if (-not $embeddedAlgorithm) {
        throw 'The embedded native miniCOD engine did not start.'
    }
    $embeddedParameters = [byte[]]$algorithmType.GetMethod(
        'GetParameters', [Reflection.BindingFlags]'Instance,Public').Invoke(
            $embeddedAlgorithm, $null)
    if ($embeddedParameters.Length -ne 512 -or
        $embeddedParameters[0] -ne [byte][char]'O' -or
        $embeddedParameters[0x90] -ne [byte][char]'O') {
        throw 'The embedded native miniCOD parameter ABI is invalid.'
    }
} finally {
    if ($embeddedAlgorithm) {
        ([IDisposable]$embeddedAlgorithm).Dispose()
    }
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

$managedAbiSizes = @{
    TimeCardCapabilitiesRaw = 64
    TimeCardPhaseSampleRaw = 80
    TimeCardPhaseControlRaw = 32
    TimeCardPhcAdjustRaw = 48
    TimeCardDisciplineBlobRaw = 528
    TimeCardDisciplineLeaseRaw = 32
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
$navPvt[23] = 12
$timSvin = [byte[]]::new(26)
[BitConverter]::GetBytes([uint32]1200).CopyTo($timSvin, 0)
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
    'TIMECARD_ABI_VERSION 15u',
    'IOCTL_TIMECARD_DISCIPLINE_LEASE',
    'TIMECARD_DISCIPLINE_LEASE_ACQUIRE',
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
$driverLifecycle = Get-Content -LiteralPath (Join-Path $root 'driver.c') -Raw
$dispatcher = Get-Content -LiteralPath (Join-Path $root 'ioctl.c') -Raw
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
    @($engine, 'AcquireDisciplineLease\(',
        'driver-enforced discipline ownership'),
    @($engine, 'NativeGnssSessionManager',
        'continuous associated-epoch GNSS parser'),
    @($engine, 'Interlocked\.Exchange\(\s*ref calibrationRequested',
        'lossless calibration command queue'),
    @($engine, 'PreviousQuantizationErrorPicoseconds\s*\?\?\s*0',
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

if ($driverLifecycle -notmatch `
        'TimeCardDisciplineLeaseControl[\s\S]*TimeCardPhaseDisableLocked\(context\)[\s\S]*DisciplineOwner\s*=\s*NULL' -or
    $driverLifecycle -notmatch `
        'TimeCardEvtFileCleanup[\s\S]*WdfWaitLockAcquire[\s\S]*TimeCardPhaseDisableLocked\(context\)[\s\S]*DisciplineOwner\s*=\s*NULL[\s\S]*WdfWaitLockRelease') {
    throw 'Driver lease crash cleanup is missing.'
}
foreach ($critical in @(
    'IOCTL_TIMECARD_SET_TIME',
    'IOCTL_TIMECARD_CLOCK_SOURCE_SET',
    'IOCTL_TIMECARD_PPS_SET',
    'IOCTL_TIMECARD_MRO50_CONTROL',
    'IOCTL_TIMECARD_PHASE_CONTROL',
    'IOCTL_TIMECARD_PHC_ADJUST',
    'IOCTL_TIMECARD_DISCIPLINE_WRITE')) {
    $case = [regex]::Match($dispatcher,
        "case $critical\s*:(?<body>[\s\S]*?)(?=\s*case\s+IOCTL_|\s*default\s*:)")
    if (-not $case.Success -or
        $case.Groups['body'].Value -notmatch
            'TimeCardDisciplineAccessAllowed') {
        throw "Discipline lease does not guard $critical."
    }
}

Write-Host "Native discipline smoke test passed ($result)."
