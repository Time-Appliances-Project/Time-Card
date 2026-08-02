using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Text.RegularExpressions;

namespace TimeCardControlCenter
{
    public interface ISa5xCommandTransport
    {
        string Execute(string command, uint timeoutMilliseconds);
    }

    public sealed class Sa5xDelegateTransport : ISa5xCommandTransport
    {
        private readonly Func<string, uint, string> execute;

        public Sa5xDelegateTransport(Func<string, uint, string> executeCommand)
        {
            if (executeCommand == null)
                throw new ArgumentNullException("executeCommand");
            execute = executeCommand;
        }

        public string Execute(string command, uint timeoutMilliseconds)
        {
            return execute(command, timeoutMilliseconds);
        }
    }

    public interface ISa5xMonotonicClock
    {
        long Milliseconds { get; }
    }

    internal sealed class Sa5xStopwatchClock : ISa5xMonotonicClock
    {
        private readonly Stopwatch stopwatch = Stopwatch.StartNew();

        public long Milliseconds
        {
            get { return stopwatch.ElapsedMilliseconds; }
        }
    }

    public enum Sa5xFirmwareLatchSafety
    {
        Unknown,
        LegacyAffected,
        FixedOrNewer
    }

    public enum Sa5xClockClass
    {
        Uncalibrated,
        Calibrating,
        Holdover,
        Locked
    }

    public enum Sa5xDiscipliningState
    {
        Initialization,
        Tracking,
        Holdover,
        Calibration
    }

    public sealed class Sa5xManagerOptions
    {
        public Sa5xManagerOptions()
        {
            CommandTimeoutMilliseconds = 1200;
            ConfigurePhaseLimit = true;
            ResetTauOnStart = true;
            EnableDiscipliningWhenSafe = true;
            EnableLegacyAutoLatch = true;
        }

        public uint CommandTimeoutMilliseconds { get; set; }
        public bool ConfigurePhaseLimit { get; set; }
        public bool ResetTauOnStart { get; set; }
        public bool EnableDiscipliningWhenSafe { get; set; }
        public bool EnableLegacyAutoLatch { get; set; }

        internal void Validate()
        {
            if (CommandTimeoutMilliseconds < 100 ||
                CommandTimeoutMilliseconds > 5000)
                throw new ArgumentOutOfRangeException(
                    "CommandTimeoutMilliseconds",
                    "The SA5x command timeout must be between 100 and 5000 ms.");
        }
    }

    public sealed class Sa5xIdentity
    {
        internal Sa5xIdentity(string device, string firmware, string serial,
            Sa5xFirmwareLatchSafety latchSafety, bool phaseLimitVerified,
            IList<string> warnings)
        {
            Device = device;
            Firmware = firmware;
            Serial = serial;
            FirmwareLatchSafety = latchSafety;
            PhaseLimitVerified = phaseLimitVerified;
            Warnings = new List<string>(warnings).AsReadOnly();
        }

        public string Device { get; private set; }
        public string Firmware { get; private set; }
        public string Serial { get; private set; }
        public Sa5xFirmwareLatchSafety FirmwareLatchSafety { get; private set; }
        public bool PhaseLimitVerified { get; private set; }
        public IList<string> Warnings { get; private set; }
    }

    public sealed class Sa5xManagerSnapshot
    {
        internal Sa5xManagerSnapshot()
        {
            ActiveAlarms = new List<string>().AsReadOnly();
            Warnings = new List<string>().AsReadOnly();
            Actions = new List<string>().AsReadOnly();
        }

        public string Device { get; internal set; }
        public string Firmware { get; internal set; }
        public string Serial { get; internal set; }
        public bool CommunicationsHealthy { get; internal set; }
        public uint Alarms { get; internal set; }
        public IList<string> ActiveAlarms { get; internal set; }
        public bool PpsInputDetected { get; internal set; }
        public bool OscillatorLocked { get; internal set; }
        public bool DisciplineLocked { get; internal set; }
        public bool DiscipliningEnabled { get; internal set; }
        public int PhaseNanoseconds { get; internal set; }
        public int LastCorrection { get; internal set; }
        public long DigitalTuning { get; internal set; }
        public double TemperatureCelsius { get; internal set; }
        public uint Tau { get; internal set; }
        public int LockProgressPercent { get; internal set; }
        public bool GnssFixFresh { get; internal set; }
        public DateTime? LastGnssFixUtc { get; internal set; }
        public bool Holdover { get; internal set; }
        public bool HoldoverReady { get; internal set; }
        public int TauPhase { get; internal set; }
        public Sa5xClockClass ClockClass { get; internal set; }
        public Sa5xDiscipliningState State { get; internal set; }
        public int PhaseConvergenceSeconds { get; internal set; }
        public int PhaseConvergenceThresholdSeconds { get; internal set; }
        public float ConvergenceProgressPercent { get; internal set; }
        public Sa5xFirmwareLatchSafety FirmwareLatchSafety { get; internal set; }
        public bool PhaseLimitVerified { get; internal set; }
        public bool LatchPendingReenable { get; internal set; }
        public IList<string> Warnings { get; internal set; }
        public IList<string> Actions { get; internal set; }
    }

    public static class Sa5xProtocol
    {
        public const int DigitalTuningLimit = 20000000;
        public const int SafePhaseLimit = 100000;
        public const int NoGnssFixTimeoutSeconds = 9;
        public const uint NoPpsAlarm = 1u << 17;
        public const uint DiscipliningRangeAlarm = 1u << 18;

        private static readonly Regex FirmwarePattern = new Regex(
            @"(?:^|[^A-Za-z0-9])V?(\d+)\.(\d+)(?:[^0-9]|$)",
            RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);

        private static readonly IDictionary<int, string> AlarmDescriptions =
            new Dictionary<int, string>
            {
                { 0, "FPGA fault" },
                { 1, "PLL fault" },
                { 2, "Flash fault" },
                { 3, "Acquisition failed" },
                { 4, "No external oscillator" },
                { 5, "Cell heater fault" },
                { 6, "Incompatible firmware" },
                { 16, "Temperature warning" },
                { 17, "No PPS input" },
                { 18, "Disciplining range warning" }
            };

        public static bool IsSa5xIdentity(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return false;
            string normalized = Unquote(value);
            return Regex.IsMatch(normalized, @"\b(?:MAC[- ]?)?SA5[0-9A-Z]*\b",
                RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        }

        public static Sa5xFirmwareLatchSafety ClassifyFirmwareForLatch(
            string value)
        {
            int major;
            int minor;
            if (!TryParseFirmwareVersion(value, out major, out minor))
                return Sa5xFirmwareLatchSafety.Unknown;
            if (major > 1 || (major == 1 && minor >= 1))
                return Sa5xFirmwareLatchSafety.FixedOrNewer;
            return Sa5xFirmwareLatchSafety.LegacyAffected;
        }

        public static bool TryParseFirmwareVersion(string value, out int major,
            out int minor)
        {
            major = 0;
            minor = 0;
            if (string.IsNullOrWhiteSpace(value))
                return false;
            Match match = FirmwarePattern.Match(Unquote(value));
            if (!match.Success ||
                !int.TryParse(match.Groups[1].Value, NumberStyles.None,
                    CultureInfo.InvariantCulture, out major) ||
                !int.TryParse(match.Groups[2].Value, NumberStyles.None,
                    CultureInfo.InvariantCulture, out minor))
                return false;
            return major >= 0 && minor >= 0 && minor <= 255;
        }

        public static IList<string> DecodeAlarms(uint alarms)
        {
            List<string> result = new List<string>();
            foreach (KeyValuePair<int, string> alarm in AlarmDescriptions)
            {
                if ((alarms & (1u << alarm.Key)) != 0)
                    result.Add(alarm.Value + " (bit " +
                        alarm.Key.ToString(CultureInfo.InvariantCulture) + ")");
            }
            uint known = 0;
            foreach (int bit in AlarmDescriptions.Keys)
                known |= 1u << bit;
            uint unknown = alarms & ~known;
            if (unknown != 0)
                result.Add("Unknown alarm bits 0x" + unknown.ToString("X8",
                    CultureInfo.InvariantCulture));
            return result.AsReadOnly();
        }

        public static bool TryParseBoolean(string value, out bool parsed)
        {
            string normalized = Unquote(value);
            if (string.Equals(normalized, "1", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(normalized, "true", StringComparison.OrdinalIgnoreCase))
            {
                parsed = true;
                return true;
            }
            if (string.Equals(normalized, "0", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(normalized, "false", StringComparison.OrdinalIgnoreCase))
            {
                parsed = false;
                return true;
            }
            parsed = false;
            return false;
        }

        public static bool TryParseInt32(string value, out int parsed)
        {
            return int.TryParse(Unquote(value), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out parsed);
        }

        public static bool TryParseUInt32(string value, out uint parsed)
        {
            return uint.TryParse(Unquote(value), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out parsed);
        }

        public static bool TryParseInt64(string value, out long parsed)
        {
            return long.TryParse(Unquote(value), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out parsed);
        }

        public static bool TryParsePhase(string value, out int parsed)
        {
            double phase;
            if (!double.TryParse(Unquote(value), NumberStyles.Float,
                CultureInfo.InvariantCulture, out phase) ||
                double.IsNaN(phase) || double.IsInfinity(phase) ||
                phase > int.MaxValue || phase < int.MinValue)
            {
                parsed = 0;
                return false;
            }
            parsed = (int)Math.Round(phase, MidpointRounding.AwayFromZero);
            return true;
        }

        public static string Unquote(string value)
        {
            if (value == null)
                return string.Empty;
            string normalized = value.Trim();
            if (normalized.Length >= 2 && normalized[0] == '"' &&
                normalized[normalized.Length - 1] == '"')
                return normalized.Substring(1, normalized.Length - 2).Trim();
            return normalized;
        }
    }

    public sealed class Sa5xOscillatorManager
    {
        private static readonly uint[] TauValues = { 50u, 500u, 10000u };
        private static readonly int[] TauIntervalsSeconds = { 600, 7200, 86400 };

        private readonly object gate = new object();
        private readonly ISa5xCommandTransport transport;
        private readonly ISa5xMonotonicClock clock;
        private readonly Sa5xManagerOptions options;
        private bool initialized;
        private string device;
        private string firmware;
        private string serial;
        private Sa5xFirmwareLatchSafety latchSafety;
        private bool phaseLimitVerified;
        private long lastGnssFixMilliseconds = -1;
        private DateTime? lastGnssFixUtc;
        private long discipliningStartMilliseconds;
        private long lastLatchMilliseconds = -1;
        private bool latchPendingReenable;
        private long latchReenableMilliseconds;
        private int tauPhase;
        private Sa5xClockClass clockClass = Sa5xClockClass.Calibrating;
        private Sa5xDiscipliningState state = Sa5xDiscipliningState.Initialization;

        public Sa5xOscillatorManager(ISa5xCommandTransport commandTransport)
            : this(commandTransport, new Sa5xManagerOptions(),
                new Sa5xStopwatchClock())
        {
        }

        public Sa5xOscillatorManager(ISa5xCommandTransport commandTransport,
            Sa5xManagerOptions managerOptions, ISa5xMonotonicClock monotonicClock)
        {
            if (commandTransport == null)
                throw new ArgumentNullException("commandTransport");
            if (managerOptions == null)
                throw new ArgumentNullException("managerOptions");
            if (monotonicClock == null)
                throw new ArgumentNullException("monotonicClock");
            managerOptions.Validate();
            transport = commandTransport;
            options = managerOptions;
            clock = monotonicClock;
        }

        public Sa5xIdentity Initialize()
        {
            lock (gate)
            {
                List<string> warnings = new List<string>();
                string identifiedDevice = Query("device?");
                if (!Sa5xProtocol.IsSa5xIdentity(identifiedDevice))
                    throw new InvalidOperationException("The atomic-clock UART identified itself as '" +
                        Sa5xProtocol.Unquote(identifiedDevice) +
                        "', so no SA5x settings were changed.");

                device = Sa5xProtocol.Unquote(identifiedDevice);
                firmware = TryQueryText("swrev?", warnings);
                serial = TryQueryText("serial?", warnings);
                latchSafety = Sa5xProtocol.ClassifyFirmwareForLatch(firmware);
                if (latchSafety == Sa5xFirmwareLatchSafety.Unknown)
                    warnings.Add("The firmware revision is not unambiguous; automatic latching is disabled.");
                else if (latchSafety == Sa5xFirmwareLatchSafety.FixedOrNewer)
                    warnings.Add("This firmware includes or supersedes the latch fix; automatic latching is disabled.");

                phaseLimitVerified = VerifyPhaseLimit(warnings);
                tauPhase = 0;
                discipliningStartMilliseconds = clock.Milliseconds;
                if (options.ResetTauOnStart)
                    SetTau(TauValues[0], warnings, "startup");
                initialized = true;

                return new Sa5xIdentity(device, firmware, serial, latchSafety,
                    phaseLimitVerified, warnings);
            }
        }

        public void PushGnssFix(bool fixOk, DateTime? fixUtc)
        {
            lock (gate)
            {
                if (fixOk)
                {
                    lastGnssFixMilliseconds = clock.Milliseconds;
                    lastGnssFixUtc = fixUtc.HasValue ?
                        DateTime.SpecifyKind(fixUtc.Value, DateTimeKind.Utc) :
                        (DateTime?)null;
                }
            }
        }

        public Sa5xManagerSnapshot Poll()
        {
            lock (gate)
            {
                if (!initialized)
                    throw new InvalidOperationException(
                        "Initialize the SA5x manager before polling it.");

                List<string> warnings = new List<string>();
                List<string> actions = new List<string>();
                RawTelemetry telemetry = ReadTelemetry(warnings);
                long now = clock.Milliseconds;
                bool gnssFresh = lastGnssFixMilliseconds >= 0 &&
                    Elapsed(now, lastGnssFixMilliseconds) <
                    Sa5xProtocol.NoGnssFixTimeoutSeconds * 1000L;
                bool criticalTelemetry = telemetry.PpsInputDetected.HasValue &&
                    telemetry.DisciplineLocked.HasValue && telemetry.Disciplining.HasValue &&
                    telemetry.Phase.HasValue && telemetry.Tau.HasValue;

                if (criticalTelemetry && latchPendingReenable &&
                    now >= latchReenableMilliseconds)
                {
                    TryCompleteLatchReenable(telemetry, gnssFresh, warnings, actions);
                }

                bool latchIssued = false;
                if (criticalTelemetry && ShouldLatch(telemetry, now))
                    latchIssued = TryLatch(telemetry, now, warnings, actions);

                bool noPps = telemetry.PpsInputDetected.HasValue &&
                    !telemetry.PpsInputDetected.Value;
                bool noPpsAlarm = telemetry.Alarms.HasValue &&
                    (telemetry.Alarms.Value & Sa5xProtocol.NoPpsAlarm) != 0;
                bool holdover = !gnssFresh || noPps || noPpsAlarm || latchIssued ||
                    latchPendingReenable;

                if (criticalTelemetry && !holdover && !latchPendingReenable &&
                    options.EnableDiscipliningWhenSafe &&
                    !telemetry.Disciplining.Value)
                {
                    if (!phaseLimitVerified)
                        warnings.Add("Disciplining remains off because the 100000 phase limit was not verified.");
                    else if (!telemetry.Alarms.HasValue || telemetry.Alarms.Value != 0)
                        warnings.Add("Disciplining remains off while the SA5x reports alarms or its alarm state is unknown.");
                    else if (!telemetry.OscillatorLocked.HasValue ||
                        !telemetry.OscillatorLocked.Value)
                        warnings.Add("Disciplining remains off until the atomic oscillator is locked.");
                    else if (telemetry.Phase.Value != 0)
                        warnings.Add("Disciplining remains off because the measured phase is not exactly zero.");
                    else
                    {
                        SetInteger("Disciplining", 1);
                        telemetry.Disciplining = true;
                        actions.Add("Enabled disciplining after verifying GNSS, PPS, phase zero, and phase limit.");
                    }
                }

                UpdateState(telemetry, holdover, now, warnings, actions);
                return MakeSnapshot(telemetry, criticalTelemetry, gnssFresh,
                    holdover, now, warnings, actions);
            }
        }

        private RawTelemetry ReadTelemetry(ICollection<string> warnings)
        {
            RawTelemetry result = new RawTelemetry();
            result.Alarms = QueryUInt32("Alarms", warnings);
            result.PpsInputDetected = QueryBoolean("PpsInDetected", warnings);
            result.OscillatorLocked = QueryBoolean("Locked", warnings);
            result.DisciplineLocked = QueryBoolean("DisciplineLocked", warnings);
            result.Disciplining = QueryBoolean("Disciplining", warnings);
            result.Phase = QueryPhase("Phase", warnings);
            result.LastCorrection = QueryInt32("LastCorrection", warnings);
            result.DigitalTuning = QueryInt64("DigitalTuning", warnings);
            result.TemperatureMilliCelsius = QueryInt32("Temperature", warnings);
            result.Tau = QueryUInt32("TauPps0", warnings);
            result.LockProgress = QueryInt32("LockProgress", warnings);
            return result;
        }

        private bool ShouldLatch(RawTelemetry telemetry, long now)
        {
            if (!options.EnableLegacyAutoLatch ||
                latchSafety != Sa5xFirmwareLatchSafety.LegacyAffected ||
                latchPendingReenable || !phaseLimitVerified ||
                !telemetry.Alarms.HasValue ||
                !telemetry.LastCorrection.HasValue ||
                !telemetry.DigitalTuning.HasValue ||
                !telemetry.DisciplineLocked.HasValue)
                return false;

            bool latchIntervalElapsed = lastLatchMilliseconds < 0 ||
                Elapsed(now, lastLatchMilliseconds) >
                TauIntervalsSeconds[0] * 1000L;
            bool rangeAlarm = (telemetry.Alarms.Value &
                Sa5xProtocol.DiscipliningRangeAlarm) != 0;
            if (rangeAlarm && telemetry.LastCorrection.Value == 0)
                return latchIntervalElapsed;

            return telemetry.Alarms.Value == 0 &&
                telemetry.LastCorrection.Value == 0 &&
                !telemetry.DisciplineLocked.Value &&
                (telemetry.DigitalTuning.Value == Sa5xProtocol.DigitalTuningLimit ||
                 telemetry.DigitalTuning.Value == -Sa5xProtocol.DigitalTuningLimit) &&
                latchIntervalElapsed;
        }

        private bool TryLatch(RawTelemetry telemetry, long now,
            ICollection<string> warnings, ICollection<string> actions)
        {
            if (!telemetry.Disciplining.HasValue)
                return false;
            bool enabled = telemetry.Disciplining.Value;
            for (int attempt = 0; enabled && attempt < 3; ++attempt)
            {
                SetInteger("Disciplining", 0);
                bool? confirmed = QueryBoolean("Disciplining", warnings);
                if (!confirmed.HasValue)
                {
                    warnings.Add("Automatic latch aborted because disabling disciplining could not be verified.");
                    return false;
                }
                enabled = confirmed.Value;
            }
            if (enabled)
            {
                warnings.Add("Automatic latch aborted after three attempts to disable disciplining.");
                return false;
            }

            bool accepted;
            if (!Sa5xProtocol.TryParseBoolean(Query("latch"), out accepted) ||
                !accepted)
            {
                warnings.Add("The legacy SA5x rejected the guarded latch command.");
                return false;
            }

            telemetry.Disciplining = false;
            lastLatchMilliseconds = now;
            latchPendingReenable = true;
            latchReenableMilliseconds = AddSaturating(now, 1000);
            actions.Add("Issued the firmware-gated legacy latch; disciplining will only resume after one second and a zero-phase check.");
            return true;
        }

        private void TryCompleteLatchReenable(RawTelemetry telemetry,
            bool gnssFresh, ICollection<string> warnings,
            ICollection<string> actions)
        {
            if (!telemetry.Phase.HasValue)
                return;
            if (!gnssFresh || !telemetry.PpsInputDetected.Value)
            {
                warnings.Add("Legacy latch completed, but disciplining remains off until GNSS and PPS are valid.");
                return;
            }
            if (!phaseLimitVerified || telemetry.Phase.Value != 0)
            {
                latchPendingReenable = false;
                warnings.Add("Legacy latch completed without an exact zero phase; automatic re-enable was cancelled.");
                return;
            }
            SetInteger("Disciplining", 1);
            telemetry.Disciplining = true;
            latchPendingReenable = false;
            actions.Add("Re-enabled disciplining after the guarded latch delay and zero-phase verification.");
        }

        private void UpdateState(RawTelemetry telemetry, bool holdover, long now,
            ICollection<string> warnings, ICollection<string> actions)
        {
            if (holdover)
            {
                bool wasCalibrating = clockClass == Sa5xClockClass.Calibrating;
                bool overOneDay = lastGnssFixMilliseconds < 0 ||
                    Elapsed(now, lastGnssFixMilliseconds) > 24L * 3600L * 1000L;
                clockClass = wasCalibrating || overOneDay ?
                    Sa5xClockClass.Uncalibrated : Sa5xClockClass.Holdover;
                state = Sa5xDiscipliningState.Holdover;
                return;
            }

            if (clockClass == Sa5xClockClass.Holdover ||
                clockClass == Sa5xClockClass.Uncalibrated)
            {
                tauPhase = 0;
                discipliningStartMilliseconds = now;
                if (options.ResetTauOnStart)
                {
                    SetTau(TauValues[0], warnings, "GNSS recovery");
                    actions.Add("Restarted SA5x convergence at TAU 50 after holdover.");
                }
                clockClass = Sa5xClockClass.Calibrating;
                state = Sa5xDiscipliningState.Tracking;
            }

            if (!telemetry.DisciplineLocked.HasValue ||
                !telemetry.DisciplineLocked.Value)
            {
                discipliningStartMilliseconds = now;
                if (clockClass != Sa5xClockClass.Calibrating || tauPhase != 0)
                {
                    tauPhase = 0;
                    if (options.ResetTauOnStart)
                    {
                        SetTau(TauValues[0], warnings, "discipline unlock");
                        actions.Add("Reset SA5x convergence to TAU 50 because discipline lock is not asserted.");
                    }
                }
                clockClass = Sa5xClockClass.Calibrating;
                state = Sa5xDiscipliningState.Tracking;
                return;
            }

            long elapsed = Elapsed(now, discipliningStartMilliseconds);
            if (tauPhase < TauValues.Length - 1 &&
                elapsed > TauIntervalsSeconds[tauPhase] * 1000L)
            {
                ++tauPhase;
                if (options.ResetTauOnStart)
                {
                    SetTau(TauValues[tauPhase], warnings, "convergence phase");
                    actions.Add("Advanced SA5x convergence to TAU " +
                        TauValues[tauPhase].ToString(CultureInfo.InvariantCulture) + ".");
                }
            }

            if (tauPhase == 0)
            {
                clockClass = Sa5xClockClass.Calibrating;
                state = Sa5xDiscipliningState.Tracking;
            }
            else
            {
                clockClass = Sa5xClockClass.Locked;
                state = Sa5xDiscipliningState.Calibration;
            }
        }

        private Sa5xManagerSnapshot MakeSnapshot(RawTelemetry telemetry,
            bool criticalTelemetry, bool gnssFresh, bool holdover, long now,
            IList<string> warnings, IList<string> actions)
        {
            long elapsed = Math.Max(0, Elapsed(now,
                discipliningStartMilliseconds) / 1000L);
            int threshold = TauIntervalsSeconds[Math.Min(tauPhase,
                TauIntervalsSeconds.Length - 1)];
            int count = (int)Math.Min(int.MaxValue, elapsed);
            float progress = threshold <= 0 ? 0 :
                (float)Math.Min(100.0, 100.0 * elapsed / threshold);

            Sa5xManagerSnapshot snapshot = new Sa5xManagerSnapshot();
            snapshot.Device = device;
            snapshot.Firmware = firmware;
            snapshot.Serial = serial;
            snapshot.CommunicationsHealthy = criticalTelemetry;
            snapshot.Alarms = telemetry.Alarms.GetValueOrDefault();
            snapshot.ActiveAlarms = Sa5xProtocol.DecodeAlarms(snapshot.Alarms);
            snapshot.PpsInputDetected = telemetry.PpsInputDetected.GetValueOrDefault();
            snapshot.OscillatorLocked = telemetry.OscillatorLocked.GetValueOrDefault();
            snapshot.DisciplineLocked = telemetry.DisciplineLocked.GetValueOrDefault();
            snapshot.DiscipliningEnabled = telemetry.Disciplining.GetValueOrDefault();
            snapshot.PhaseNanoseconds = telemetry.Phase.GetValueOrDefault();
            snapshot.LastCorrection = telemetry.LastCorrection.GetValueOrDefault();
            snapshot.DigitalTuning = telemetry.DigitalTuning.GetValueOrDefault();
            snapshot.TemperatureCelsius = telemetry.TemperatureMilliCelsius.HasValue ?
                telemetry.TemperatureMilliCelsius.Value / 1000.0 : double.NaN;
            snapshot.Tau = telemetry.Tau.GetValueOrDefault();
            snapshot.LockProgressPercent = telemetry.LockProgress.GetValueOrDefault();
            snapshot.GnssFixFresh = gnssFresh;
            snapshot.LastGnssFixUtc = lastGnssFixUtc;
            snapshot.Holdover = holdover;
            snapshot.HoldoverReady = tauPhase == TauValues.Length - 1;
            snapshot.TauPhase = tauPhase;
            snapshot.ClockClass = clockClass;
            snapshot.State = state;
            snapshot.PhaseConvergenceSeconds = count;
            snapshot.PhaseConvergenceThresholdSeconds = threshold;
            snapshot.ConvergenceProgressPercent = progress;
            snapshot.FirmwareLatchSafety = latchSafety;
            snapshot.PhaseLimitVerified = phaseLimitVerified;
            snapshot.LatchPendingReenable = latchPendingReenable;
            snapshot.Warnings = new List<string>(warnings).AsReadOnly();
            snapshot.Actions = new List<string>(actions).AsReadOnly();
            return snapshot;
        }

        private bool VerifyPhaseLimit(ICollection<string> warnings)
        {
            int value;
            try
            {
                if (!Sa5xProtocol.TryParseInt32(Query("get,PhaseLimit"), out value))
                {
                    warnings.Add("The SA5x phase limit response could not be parsed; automatic disciplining is inhibited.");
                    return false;
                }
                if (value == Sa5xProtocol.SafePhaseLimit)
                    return true;
                if (!options.ConfigurePhaseLimit)
                {
                    warnings.Add("The SA5x phase limit is not 100000 and configuration is disabled.");
                    return false;
                }
                SetInteger("PhaseLimit", Sa5xProtocol.SafePhaseLimit);
                if (!Sa5xProtocol.TryParseInt32(Query("get,PhaseLimit"), out value) ||
                    value != Sa5xProtocol.SafePhaseLimit)
                {
                    warnings.Add("The SA5x did not verify the requested 100000 phase limit; automatic disciplining is inhibited.");
                    return false;
                }
                return true;
            }
            catch (Exception ex)
            {
                warnings.Add("Phase-limit verification failed: " + ex.Message);
                return false;
            }
        }

        private void SetTau(uint value, ICollection<string> warnings, string reason)
        {
            try
            {
                SetInteger("TauPps0", value);
            }
            catch (Exception ex)
            {
                warnings.Add("Could not set TAU to " +
                    value.ToString(CultureInfo.InvariantCulture) + " during " +
                    reason + ": " + ex.Message);
            }
        }

        private string TryQueryText(string command, ICollection<string> warnings)
        {
            try
            {
                return Sa5xProtocol.Unquote(Query(command));
            }
            catch (Exception ex)
            {
                warnings.Add(command + ": " + ex.Message);
                return string.Empty;
            }
        }

        private bool? QueryBoolean(string parameter, ICollection<string> warnings)
        {
            bool value;
            try
            {
                if (Sa5xProtocol.TryParseBoolean(Query("get," + parameter), out value))
                    return value;
                warnings.Add(parameter + " returned an invalid Boolean value.");
            }
            catch (Exception ex) { warnings.Add(parameter + ": " + ex.Message); }
            return null;
        }

        private int? QueryInt32(string parameter, ICollection<string> warnings)
        {
            int value;
            try
            {
                if (Sa5xProtocol.TryParseInt32(Query("get," + parameter), out value))
                    return value;
                warnings.Add(parameter + " returned an invalid Int32 value.");
            }
            catch (Exception ex) { warnings.Add(parameter + ": " + ex.Message); }
            return null;
        }

        private uint? QueryUInt32(string parameter, ICollection<string> warnings)
        {
            uint value;
            try
            {
                if (Sa5xProtocol.TryParseUInt32(Query("get," + parameter), out value))
                    return value;
                warnings.Add(parameter + " returned an invalid UInt32 value.");
            }
            catch (Exception ex) { warnings.Add(parameter + ": " + ex.Message); }
            return null;
        }

        private long? QueryInt64(string parameter, ICollection<string> warnings)
        {
            long value;
            try
            {
                if (Sa5xProtocol.TryParseInt64(Query("get," + parameter), out value))
                    return value;
                warnings.Add(parameter + " returned an invalid Int64 value.");
            }
            catch (Exception ex) { warnings.Add(parameter + ": " + ex.Message); }
            return null;
        }

        private int? QueryPhase(string parameter, ICollection<string> warnings)
        {
            int value;
            try
            {
                if (Sa5xProtocol.TryParsePhase(Query("get," + parameter), out value))
                    return value;
                warnings.Add(parameter + " returned an invalid phase value.");
            }
            catch (Exception ex) { warnings.Add(parameter + ": " + ex.Message); }
            return null;
        }

        private void SetInteger(string parameter, long value)
        {
            Query("set," + parameter + "," +
                value.ToString(CultureInfo.InvariantCulture));
        }

        private string Query(string command)
        {
            return transport.Execute(command, options.CommandTimeoutMilliseconds);
        }

        private static long Elapsed(long later, long earlier)
        {
            if (earlier < 0)
                return long.MaxValue;
            return later >= earlier ? later - earlier : 0;
        }

        private static long AddSaturating(long value, long increment)
        {
            return value > long.MaxValue - increment ? long.MaxValue :
                value + increment;
        }

        private sealed class RawTelemetry
        {
            internal uint? Alarms;
            internal bool? PpsInputDetected;
            internal bool? OscillatorLocked;
            internal bool? DisciplineLocked;
            internal bool? Disciplining;
            internal int? Phase;
            internal int? LastCorrection;
            internal long? DigitalTuning;
            internal int? TemperatureMilliCelsius;
            internal uint? Tau;
            internal int? LockProgress;
        }
    }
}
