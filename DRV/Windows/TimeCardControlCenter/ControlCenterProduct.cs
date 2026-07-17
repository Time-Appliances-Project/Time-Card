using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Xml.Serialization;

namespace TimeCardControlCenter
{
    public enum HealthSeverity
    {
        Healthy,
        Attention,
        Unavailable,
        Informational
    }

    public sealed class HealthNode
    {
        public HealthNode(string key, string name, string workspace,
                          HealthSeverity severity, string status, string detail)
        {
            Key = key;
            Name = name;
            Workspace = workspace;
            Severity = severity;
            Status = status;
            Detail = detail;
        }

        public string Key { get; private set; }
        public string Name { get; private set; }
        public string Workspace { get; private set; }
        public HealthSeverity Severity { get; private set; }
        public string Status { get; private set; }
        public string Detail { get; private set; }
    }

    public sealed class HealthReport
    {
        public HealthReport(IEnumerable<HealthNode> nodes)
        {
            Nodes = new List<HealthNode>(nodes).AsReadOnly();
            AttentionCount = Nodes.Count(node =>
                node.Severity == HealthSeverity.Attention ||
                node.Severity == HealthSeverity.Unavailable);
            Overall = Nodes.Any(node => node.Severity == HealthSeverity.Unavailable)
                ? HealthSeverity.Unavailable
                : Nodes.Any(node => node.Severity == HealthSeverity.Attention)
                    ? HealthSeverity.Attention : HealthSeverity.Healthy;
        }

        public IList<HealthNode> Nodes { get; private set; }
        public int AttentionCount { get; private set; }
        public HealthSeverity Overall { get; private set; }

        public HealthNode Find(string key)
        {
            return Nodes.FirstOrDefault(node => string.Equals(
                node.Key, key, StringComparison.OrdinalIgnoreCase));
        }
    }

    internal static class ControlCenterHealth
    {
        public static HealthReport Evaluate(TimeCardSnapshot snapshot,
            UbloxReceiverSnapshot ublox, Sa53Snapshot atomic,
            SensorTelemetrySnapshot sensors, bool connected, bool demoMode)
        {
            List<HealthNode> nodes = new List<HealthNode>();
            if (demoMode)
            {
                nodes.Add(Node("controller", "PCIe controller", "Subsystems", true,
                    "SIMULATED", "Demo data source"));
                nodes.Add(Node("gnss", "GNSS receiver", "Gnss", true,
                    "3D FIX", "18 satellites used"));
                nodes.Add(Node("tod", "Time-of-Day engine", "Gnss", true,
                    "UTC VALID", "Leap information valid"));
                nodes.Add(Node("atomic", "SA53 atomic clock", "Atomic", true,
                    "LOCKED", "Discipline loop locked"));
                nodes.Add(Node("phc", "Precision hardware clock", "Clock", true,
                    "IN SYNC", "Offset +4 ns"));
                nodes.Add(Node("sma", "SMA timing I/O", "Sma", true,
                    "ROUTED", "Four connectors available"));
                nodes.Add(Node("i2c", "I2C management bus", "I2c", true,
                    "READY", "Sensors and identity available"));
                nodes.Add(Node("system", "Windows system clock", "Clock", true,
                    "TRACKING", "Cross timestamp active"));
                return new HealthReport(nodes);
            }

            if (!connected || snapshot == null)
            {
                nodes.Add(new HealthNode("controller", "PCIe controller",
                    "Subsystems", HealthSeverity.Unavailable, "OFFLINE",
                    "The Time Card driver is not connected."));
                foreach (Tuple<string, string, string> item in new[]
                {
                    Tuple.Create("gnss", "GNSS receiver", "Gnss"),
                    Tuple.Create("tod", "Time-of-Day engine", "Gnss"),
                    Tuple.Create("atomic", "SA53 atomic clock", "Atomic"),
                    Tuple.Create("phc", "Precision hardware clock", "Clock"),
                    Tuple.Create("sma", "SMA timing I/O", "Sma"),
                    Tuple.Create("i2c", "I2C management bus", "I2c"),
                    Tuple.Create("system", "Windows system clock", "Clock")
                })
                    nodes.Add(new HealthNode(item.Item1, item.Item2, item.Item3,
                        HealthSeverity.Unavailable, "WAITING", "Connect the card to evaluate this stage."));
                return new HealthReport(nodes);
            }

            nodes.Add(Node("controller", "PCIe controller", "Subsystems", true,
                "ACTIVE", string.Format(CultureInfo.InvariantCulture,
                    "Driver {0} / ABI {1} / {2}", snapshot.DriverVersion,
                    snapshot.AbiVersion, snapshot.Layout)));
            bool gnssOk = snapshot.GnssFixOk || (ublox != null && ublox.FixValid);
            int used = ublox == null ? snapshot.LockedSatellites : ublox.SatellitesUsed;
            nodes.Add(Node("gnss", "GNSS receiver", "Gnss", gnssOk,
                gnssOk ? "FIX VALID" : "NO FIX",
                used.ToString(CultureInfo.InvariantCulture) + " satellites used"));
            bool utcValid = (snapshot.UtcStatus & (1u << 8)) != 0;
            nodes.Add(Node("tod", "Time-of-Day engine", "Gnss", utcValid,
                utcValid ? "UTC VALID" : "UTC INVALID",
                string.Format(CultureInfo.InvariantCulture, "ToD status 0x{0:X8}", snapshot.TodStatus)));

            bool atomicLocked = false;
            string atomicDetail = "Open the Atomic workspace to sample the SA53.";
            if (atomic != null)
            {
                atomic.TryBoolean("Locked", out atomicLocked);
                atomicDetail = atomicLocked ? "SA53 reports oscillator lock." :
                    "SA53 is present but has not asserted lock.";
            }
            nodes.Add(new HealthNode("atomic", "SA53 atomic clock", "Atomic",
                atomic == null ? HealthSeverity.Informational :
                    atomicLocked ? HealthSeverity.Healthy : HealthSeverity.Attention,
                atomic == null ? "NOT SAMPLED" : atomicLocked ? "LOCKED" : "UNLOCKED",
                atomicDetail));
            nodes.Add(Node("phc", "Precision hardware clock", "Clock",
                snapshot.IsClockSynchronized,
                snapshot.IsClockSynchronized ? "IN SYNC" : "NOT LOCKED",
                FormatSigned(snapshot.OffsetNanoseconds) + " ns to Windows"));
            nodes.Add(new HealthNode("sma", "SMA timing I/O", "Sma",
                snapshot.AbiVersion >= 4 ? HealthSeverity.Healthy : HealthSeverity.Unavailable,
                snapshot.AbiVersion >= 4 ? "READY" : "ABI UPDATE",
                snapshot.AbiVersion >= 4 ? "Verified routing controls available." :
                    "SMA controls require ABI 4 or newer."));
            bool i2cAvailable = snapshot.AbiVersion >= 3;
            bool i2cValid = i2cAvailable && (sensors == null || sensors.ControllerStatus == 0);
            nodes.Add(new HealthNode("i2c", "I2C management bus", "I2c",
                !i2cAvailable ? HealthSeverity.Unavailable :
                    i2cValid ? HealthSeverity.Healthy : HealthSeverity.Attention,
                !i2cAvailable ? "ABI UPDATE" : i2cValid ? "READY" : "CHECK BUS",
                !i2cAvailable ? "I2C controls require ABI 3 or newer." :
                    "Identity, LEDs, environmental sensors and power rails."));
            bool offsetReasonable = Math.Abs(snapshot.OffsetNanoseconds) < 1000000000L;
            nodes.Add(Node("system", "Windows system clock", "Clock", offsetReasonable,
                offsetReasonable ? "TRACKING" : "OFFSET HIGH",
                "Cross timestamp window " + snapshot.SamplingWindowNanoseconds.ToString(
                    CultureInfo.InvariantCulture) + " ns"));
            return new HealthReport(nodes);
        }

        private static HealthNode Node(string key, string name, string workspace,
                                       bool healthy, string status, string detail)
        {
            return new HealthNode(key, name, workspace,
                healthy ? HealthSeverity.Healthy : HealthSeverity.Attention,
                status, detail);
        }

        private static string FormatSigned(long value)
        {
            return value > 0 ? "+" + value.ToString(CultureInfo.InvariantCulture) :
                value.ToString(CultureInfo.InvariantCulture);
        }
    }

    [Serializable]
    public sealed class ControlCenterSettings
    {
        public string Theme { get; set; }
        public double RefreshSeconds { get; set; }
        public bool DemoMode { get; set; }
        public bool CompactNavigation { get; set; }
        public int TelemetryCapacity { get; set; }
        public string LastExportDirectory { get; set; }
        public List<string> ConfigurationHistory { get; set; }
        public ConfigurationProfile LastKnownGoodProfile { get; set; }

        public ControlCenterSettings()
        {
            Theme = "Dark";
            RefreshSeconds = 1.0;
            TelemetryCapacity = 1800;
            ConfigurationHistory = new List<string>();
        }
    }

    internal static class SettingsStore
    {
        private static readonly string DirectoryPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "OCP", "TimeCardControlCenter");
        public static readonly string SettingsPath = Path.Combine(DirectoryPath, "settings.xml");
        public static readonly string ProfilesPath = Path.Combine(DirectoryPath, "profiles.xml");

        public static T Load<T>(string path, T fallback)
        {
            try
            {
                if (!File.Exists(path))
                    return fallback;
                using (FileStream stream = File.OpenRead(path))
                    return (T)new XmlSerializer(typeof(T)).Deserialize(stream);
            }
            catch
            {
                return fallback;
            }
        }

        public static void Save<T>(string path, T value)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temporary = path + ".tmp";
            using (FileStream stream = File.Create(temporary))
                new XmlSerializer(typeof(T)).Serialize(stream, value);
            if (File.Exists(path))
                File.Replace(temporary, path, null);
            else
                File.Move(temporary, path);
        }
    }

    [Serializable]
    public sealed class SmaProfileSetting
    {
        public uint Connector { get; set; }
        public SmaDirection Direction { get; set; }
        public uint Function { get; set; }
    }

    [Serializable]
    public sealed class ConfigurationProfile
    {
        public string Name { get; set; }
        public string Description { get; set; }
        public bool HasClockSource { get; set; }
        public uint ClockSource { get; set; }
        public bool HasNmea { get; set; }
        public bool NmeaEnabled { get; set; }
        public uint NmeaBaud { get; set; }
        public bool NmeaInverted { get; set; }
        public List<SmaProfileSetting> Sma { get; set; }
        public bool IsBuiltIn { get; set; }

        public ConfigurationProfile()
        {
            Sma = new List<SmaProfileSetting>();
        }

        public override string ToString() { return Name; }
    }

    [Serializable]
    public sealed class ConfigurationProfileList
    {
        public List<ConfigurationProfile> Profiles { get; set; }
        public ConfigurationProfileList() { Profiles = new List<ConfigurationProfile>(); }
    }

    internal static class BuiltInProfiles
    {
        public static IList<ConfigurationProfile> Create()
        {
            return new List<ConfigurationProfile>
            {
                Profile("GNSS disciplined", "Use the GNSS Time-of-Day engine as the PHC reference.", 1),
                Profile("External PPS", "Discipline the PHC from an external PPS reference.", 3),
                Profile("PTP disciplined", "Use the PTP clock source exposed by the FPGA.", 4),
                new ConfigurationProfile
                {
                    Name = "NMEA service", IsBuiltIn = true,
                    Description = "Enable standard NMEA output at 115200 baud.",
                    HasNmea = true, NmeaEnabled = true, NmeaBaud = 115200
                },
                new ConfigurationProfile
                {
                    Name = "Lab timing outputs", IsBuiltIn = true,
                    Description = "Route GNSS PPS, PHC pulse, atomic clock and 10 MHz reference to SMA 1-4.",
                    Sma = new List<SmaProfileSetting>
                    {
                        new SmaProfileSetting { Connector = 1, Direction = SmaDirection.Output, Function = 0x0004 },
                        new SmaProfileSetting { Connector = 2, Direction = SmaDirection.Output, Function = 0x0001 },
                        new SmaProfileSetting { Connector = 3, Direction = SmaDirection.Output, Function = 0x0002 },
                        new SmaProfileSetting { Connector = 4, Direction = SmaDirection.Output, Function = 0x0000 }
                    }
                }
            }.AsReadOnly();
        }

        private static ConfigurationProfile Profile(string name, string description, uint source)
        {
            return new ConfigurationProfile
            {
                Name = name, Description = description, IsBuiltIn = true,
                HasClockSource = true, ClockSource = source
            };
        }
    }

    public sealed class TelemetryPoint
    {
        public DateTime TimestampUtc { get; set; }
        public double OffsetNanoseconds { get; set; }
        public double SamplingWindowNanoseconds { get; set; }
        public int SatellitesSeen { get; set; }
        public int SatellitesLocked { get; set; }
        public double BoardTemperatureCelsius { get; set; }
        public double AtomicPhase { get; set; }
        public double VibrationMetersPerSecondSquared { get; set; }
    }

    public static class TelemetryMath
    {
        public static double VectorMagnitude(double x, double y, double z)
        {
            return Math.Sqrt(x * x + y * y + z * z);
        }

        public static double RootMeanSquare(IEnumerable<double> values)
        {
            if (values == null)
                return 0;
            double sumOfSquares = 0;
            int count = 0;
            foreach (double value in values)
            {
                if (double.IsNaN(value) || double.IsInfinity(value))
                    continue;
                sumOfSquares += value * value;
                count++;
            }
            return count == 0 ? 0 : Math.Sqrt(sumOfSquares / count);
        }
    }

    public sealed class TelemetrySession
    {
        private readonly List<TelemetryPoint> points = new List<TelemetryPoint>();
        public int Capacity { get; set; }
        public bool IsRecording { get; set; }
        public IList<TelemetryPoint> Points { get { return points.AsReadOnly(); } }

        public TelemetrySession(int capacity)
        {
            Capacity = Math.Max(60, capacity);
        }

        public void Add(TelemetryPoint point)
        {
            if (point == null)
                return;
            points.Add(point);
            while (points.Count > Capacity)
                points.RemoveAt(0);
        }

        public void Clear() { points.Clear(); }

        public string ToCsv()
        {
            StringBuilder text = new StringBuilder();
            text.AppendLine("timestamp_utc,offset_ns,sampling_window_ns,satellites_seen,satellites_locked,board_temperature_c,atomic_phase,vibration_m_s2");
            foreach (TelemetryPoint point in points)
                text.AppendFormat(CultureInfo.InvariantCulture,
                    "{0:O},{1:R},{2:R},{3},{4},{5:R},{6:R},{7:R}\r\n",
                    point.TimestampUtc, point.OffsetNanoseconds,
                    point.SamplingWindowNanoseconds, point.SatellitesSeen,
                    point.SatellitesLocked, point.BoardTemperatureCelsius,
                    point.AtomicPhase, point.VibrationMetersPerSecondSquared);
            return text.ToString();
        }

        public string ToJson()
        {
            StringBuilder text = new StringBuilder("[\r\n");
            for (int index = 0; index < points.Count; index++)
            {
                TelemetryPoint point = points[index];
                text.AppendFormat(CultureInfo.InvariantCulture,
                    "  {{\"timestampUtc\":\"{0:O}\",\"offsetNanoseconds\":{1:R},\"samplingWindowNanoseconds\":{2:R},\"satellitesSeen\":{3},\"satellitesLocked\":{4},\"boardTemperatureCelsius\":{5:R},\"atomicPhase\":{6:R},\"vibrationMetersPerSecondSquared\":{7:R}}}",
                    point.TimestampUtc, point.OffsetNanoseconds,
                    point.SamplingWindowNanoseconds, point.SatellitesSeen,
                    point.SatellitesLocked, point.BoardTemperatureCelsius,
                    point.AtomicPhase, point.VibrationMetersPerSecondSquared);
                text.AppendLine(index + 1 == points.Count ? string.Empty : ",");
            }
            return text.Append("]\r\n").ToString();
        }
    }

    public enum SelfTestOutcome
    {
        Pass,
        Warning,
        Fail,
        Skipped
    }

    public sealed class SelfTestResult
    {
        public string Name { get; set; }
        public SelfTestOutcome Outcome { get; set; }
        public string Detail { get; set; }
        public TimeSpan Duration { get; set; }

        public override string ToString()
        {
            return string.Format(CultureInfo.InvariantCulture, "{0,-8}  {1}  —  {2}",
                Outcome.ToString().ToUpperInvariant(), Name, Detail);
        }
    }
}
