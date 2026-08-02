[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$windowsRoot = Split-Path -Parent $PSScriptRoot
$sa5xSource = Join-Path $windowsRoot `
    'TimeCardOscillatord\Sa5xOscillatorManager.cs'
$sa3xSource = Join-Path $windowsRoot `
    'TimeCardOscillatord\Sa3xOscillatorMonitor.cs'

foreach ($path in @($sa5xSource, $sa3xSource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing oscillator manager source: $path"
    }
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) `
    ('timecard-sa5x-test-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($temporary)

try {
    $managerAssembly = Join-Path $temporary 'SaManagers.dll'
    Add-Type -Path @($sa5xSource, $sa3xSource) `
        -OutputAssembly $managerAssembly -OutputType Library -ErrorAction Stop
    [void][Reflection.Assembly]::Load(
        [IO.File]::ReadAllBytes($managerAssembly))

    $harness = @'
using System;
using System.Collections.Generic;
using TimeCardControlCenter;

public sealed class FakeClock : ISa5xMonotonicClock
{
    public long Milliseconds { get; set; }
}

public sealed class FakeSa5xTransport : ISa5xCommandTransport
{
    public readonly Dictionary<string, string> Values =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    public readonly List<string> Commands = new List<string>();

    public FakeSa5xTransport(string firmware)
    {
        Values["device?"] = "MAC-SA53";
        Values["swrev?"] = firmware;
        Values["serial?"] = "SA53TEST001";
        Values["get,PhaseLimit"] = "90000";
        Values["get,Alarms"] = "0";
        Values["get,PpsInDetected"] = "1";
        Values["get,Locked"] = "1";
        Values["get,DisciplineLocked"] = "1";
        Values["get,Disciplining"] = "1";
        Values["get,Phase"] = "0.0";
        Values["get,LastCorrection"] = "10";
        Values["get,DigitalTuning"] = "0";
        Values["get,Temperature"] = "42375";
        Values["get,TauPps0"] = "50";
        Values["get,LockProgress"] = "100";
    }

    public string Execute(string command, uint timeoutMilliseconds)
    {
        Commands.Add(command);
        if (command.StartsWith("set,", StringComparison.OrdinalIgnoreCase))
        {
            string[] parts = command.Split(',');
            if (parts.Length != 3)
                throw new InvalidOperationException("Bad fake set command.");
            Values["get," + parts[1]] = parts[2];
            return "1";
        }
        if (string.Equals(command, "latch", StringComparison.OrdinalIgnoreCase))
        {
            Values["get,Phase"] = "0";
            Values["get,DigitalTuning"] = "0";
            return "1";
        }
        string answer;
        if (!Values.TryGetValue(command, out answer))
            throw new InvalidOperationException("No fake answer for " + command);
        return answer;
    }

    public bool Saw(string command)
    {
        return Commands.Contains(command);
    }
}

public sealed class FakeSa3xTransport : ISa3xTelemetryTransport
{
    public int Count;
    public string QueryTelemetry(uint timeoutMilliseconds)
    {
        ++Count;
        return "0,V1.2,SN123,41000,33000,12,125,48,43750,-24,1,3300\r\n";
    }
}

public static class SaManagerSmoke
{
    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static Sa5xOscillatorManager Create(FakeSa5xTransport transport,
        FakeClock clock)
    {
        return new Sa5xOscillatorManager(transport,
            new Sa5xManagerOptions(), clock);
    }

    public static string Run()
    {
        Assert(Sa5xProtocol.ClassifyFirmwareForLatch("V1.00,build") ==
            Sa5xFirmwareLatchSafety.LegacyAffected,
            "V1.00 must be treated as the affected legacy firmware.");
        Assert(Sa5xProtocol.ClassifyFirmwareForLatch("V1.01") ==
            Sa5xFirmwareLatchSafety.FixedOrNewer,
            "V1.01 must be treated as latch-fixed firmware.");
        Assert(Sa5xProtocol.ClassifyFirmwareForLatch("V2.4") ==
            Sa5xFirmwareLatchSafety.FixedOrNewer,
            "New firmware must never be auto-latched.");
        Assert(Sa5xProtocol.ClassifyFirmwareForLatch("unknown") ==
            Sa5xFirmwareLatchSafety.Unknown,
            "Ambiguous firmware must disable automatic latching.");

        FakeClock unknownClock = new FakeClock();
        FakeSa5xTransport unknownDevice = new FakeSa5xTransport("V1.00");
        unknownDevice.Values["device?"] = "not-an-atomic-clock";
        bool rejected = false;
        try { Create(unknownDevice, unknownClock).Initialize(); }
        catch (InvalidOperationException) { rejected = true; }
        Assert(rejected, "A non-SA5x identity was not rejected.");
        Assert(unknownDevice.Commands.Count == 1 &&
            unknownDevice.Commands[0] == "device?",
            "A rejected identity caused an unsafe write.");

        FakeClock clock = new FakeClock();
        FakeSa5xTransport transport = new FakeSa5xTransport("V1.02");
        Sa5xOscillatorManager manager = Create(transport, clock);
        Sa5xIdentity identity = manager.Initialize();
        Assert(identity.PhaseLimitVerified,
            "Initialization did not verify the 100000 phase limit.");
        Assert(identity.FirmwareLatchSafety ==
            Sa5xFirmwareLatchSafety.FixedOrNewer,
            "Fixed firmware was classified incorrectly.");
        Assert(transport.Saw("set,PhaseLimit,100000"),
            "Initialization did not correct the phase limit.");
        Assert(transport.Saw("set,TauPps0,50"),
            "Initialization did not reset TAU to 50.");

        transport.Commands.Clear();
        transport.Values["get,Disciplining"] = "0";
        transport.Values["get,DisciplineLocked"] = "0";
        manager.PushGnssFix(true, DateTime.UtcNow);
        Sa5xManagerSnapshot enabled = manager.Poll();
        Assert(enabled.DiscipliningEnabled &&
            transport.Saw("set,Disciplining,1"),
            "Safe zero-phase disciplining enable did not occur.");
        Assert(Math.Abs(enabled.TemperatureCelsius - 42.375) < 0.0001,
            "SA5x temperature conversion is incorrect.");

        clock.Milliseconds = 1000;
        manager.PushGnssFix(false, null);
        Sa5xManagerSnapshot transientNoFix = manager.Poll();
        Assert(!transientNoFix.Holdover,
            "A transient GNSS-fix loss entered holdover before nine seconds.");
        clock.Milliseconds = 9001;
        Sa5xManagerSnapshot holdover = manager.Poll();
        Assert(holdover.Holdover &&
            holdover.State == Sa5xDiscipliningState.Holdover,
            "Nine seconds without a fresh GNSS fix did not enter holdover.");

        FakeClock tauClock = new FakeClock();
        FakeSa5xTransport tauTransport = new FakeSa5xTransport("V1.02");
        Sa5xOscillatorManager tauManager = Create(tauTransport, tauClock);
        tauManager.Initialize();
        tauTransport.Commands.Clear();
        tauManager.PushGnssFix(true, DateTime.UtcNow);
        tauManager.Poll();
        tauClock.Milliseconds = 600001;
        tauManager.PushGnssFix(true, DateTime.UtcNow);
        Sa5xManagerSnapshot tau500 = tauManager.Poll();
        Assert(tau500.TauPhase == 1 &&
            tauTransport.Saw("set,TauPps0,500"),
            "The 600-second convergence transition did not set TAU 500.");
        tauClock.Milliseconds = 7200001;
        tauManager.PushGnssFix(true, DateTime.UtcNow);
        Sa5xManagerSnapshot tau10000 = tauManager.Poll();
        Assert(tau10000.TauPhase == 2 && tau10000.HoldoverReady &&
            tauTransport.Saw("set,TauPps0,10000"),
            "The 7200-second convergence transition did not set TAU 10000.");

        FakeClock legacyClock = new FakeClock();
        FakeSa5xTransport legacyTransport = new FakeSa5xTransport("V1.00");
        Sa5xOscillatorManager legacy = Create(legacyTransport, legacyClock);
        legacy.Initialize();
        legacyTransport.Commands.Clear();
        legacyTransport.Values["get,Alarms"] =
            Sa5xProtocol.DiscipliningRangeAlarm.ToString();
        legacyTransport.Values["get,LastCorrection"] = "0";
        legacy.PushGnssFix(true, DateTime.UtcNow);
        Sa5xManagerSnapshot latched = legacy.Poll();
        Assert(legacyTransport.Saw("set,Disciplining,0") &&
            legacyTransport.Saw("latch") && latched.LatchPendingReenable,
            "Known affected firmware did not perform the guarded latch sequence.");
        legacyClock.Milliseconds = 1001;
        legacy.PushGnssFix(true, DateTime.UtcNow);
        Sa5xManagerSnapshot relatched = legacy.Poll();
        Assert(legacyTransport.Saw("set,Disciplining,1") &&
            !relatched.LatchPendingReenable,
            "The guarded latch did not re-enable after delay and zero phase. Commands: " +
            string.Join(";", legacyTransport.Commands.ToArray()) +
            ", pending=" + relatched.LatchPendingReenable.ToString());

        FakeClock fixedClock = new FakeClock();
        FakeSa5xTransport fixedTransport = new FakeSa5xTransport("V1.01");
        Sa5xOscillatorManager fixedManager = Create(fixedTransport, fixedClock);
        fixedManager.Initialize();
        fixedTransport.Commands.Clear();
        fixedTransport.Values["get,Alarms"] =
            Sa5xProtocol.DiscipliningRangeAlarm.ToString();
        fixedTransport.Values["get,LastCorrection"] = "0";
        fixedManager.PushGnssFix(true, DateTime.UtcNow);
        fixedManager.Poll();
        Assert(!fixedTransport.Saw("latch"),
            "Latch-fixed firmware was auto-latched.");

        FakeClock ambiguousClock = new FakeClock();
        FakeSa5xTransport ambiguousTransport =
            new FakeSa5xTransport("engineering-build");
        Sa5xOscillatorManager ambiguous =
            Create(ambiguousTransport, ambiguousClock);
        ambiguous.Initialize();
        ambiguousTransport.Commands.Clear();
        ambiguousTransport.Values["get,Alarms"] =
            Sa5xProtocol.DiscipliningRangeAlarm.ToString();
        ambiguousTransport.Values["get,LastCorrection"] = "0";
        ambiguous.PushGnssFix(true, DateTime.UtcNow);
        ambiguous.Poll();
        Assert(!ambiguousTransport.Saw("latch"),
            "Unknown firmware was auto-latched.");

        FakeClock brokenClock = new FakeClock();
        FakeSa5xTransport brokenTransport = new FakeSa5xTransport("V1.02");
        Sa5xOscillatorManager broken = Create(brokenTransport, brokenClock);
        broken.Initialize();
        brokenTransport.Commands.Clear();
        brokenTransport.Values["get,PpsInDetected"] = "garbage";
        brokenTransport.Values["get,Disciplining"] = "0";
        broken.PushGnssFix(true, DateTime.UtcNow);
        Sa5xManagerSnapshot brokenSnapshot = broken.Poll();
        Assert(!brokenSnapshot.CommunicationsHealthy &&
            !brokenTransport.Saw("set,Disciplining,1") &&
            !brokenTransport.Saw("latch"),
            "Incomplete telemetry caused an automatic mutation.");

        Sa3xTelemetry sa3x;
        string parseError;
        Assert(Sa3xTelemetryParser.TryParse(
            "0,V1.2,SN123,41000,33000,12,125,48,43750,-24,1,3300",
            out sa3x, out parseError), "Valid SA3x telemetry was rejected.");
        Assert(sa3x.Locked && Math.Abs(sa3x.TemperatureCelsius - 43.75) < 0.0001 &&
            sa3x.AnalogTuningEnabled,
            "SA3x telemetry fields were decoded incorrectly.");
        Assert(!Sa3xTelemetryParser.TryParse("bad,line", out sa3x,
            out parseError), "Truncated SA3x telemetry was accepted.");

        FakeSa3xTransport sa3xTransport = new FakeSa3xTransport();
        FakeClock sa3xClock = new FakeClock();
        Sa3xOscillatorMonitor sa3xMonitor = new Sa3xOscillatorMonitor(
            sa3xTransport, sa3xClock, 100, 5000);
        sa3xMonitor.Poll();
        sa3xMonitor.Poll();
        Assert(sa3xTransport.Count == 1,
            "SA3x telemetry was not cached for its settling interval.");
        sa3xClock.Milliseconds = 5001;
        sa3xMonitor.Poll();
        Assert(sa3xTransport.Count == 2,
            "SA3x telemetry was not refreshed after its settling interval.");

        return "SA5x manager and SA3x monitor tests passed.";
    }
}
'@

    Add-Type -TypeDefinition $harness -Language CSharp `
        -ReferencedAssemblies $managerAssembly -ErrorAction Stop
    [SaManagerSmoke]::Run()
}
finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Recurse -Force
    }
}
