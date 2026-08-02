using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Text;
using System.Web.Script.Serialization;

namespace TimeCardControlCenter
{
    internal sealed class OscillatordConfiguration
    {
        private static readonly HashSet<string> KnownKeys =
            new HashSet<string>(StringComparer.Ordinal)
            {
                "schemaVersion", "disciplining", "monitoring", "deviceIndex",
                "devicePath", "deviceSerial",
                "deviceRetrySeconds", "socketAddress", "socketPort",
                "monitoringAllowControl", "monitoringControlToken",
                "namedPipe", "gnssBaud", "gnssBypassSurvey",
                "gnssReceiverReconfigure", "gnssPersistConfiguration",
                "gnssPreferredTimeScale", "gnssCableDelayNanoseconds",
                "gnssRtcmEnabled",
                "initializePhcFromGnss", "startupAlignmentTimeoutSeconds",
                "startupAlignmentSettlingSeconds",
                "oppositePhaseError", "calibrateFirst", "phaseResolutionNs",
                "debug",
                "refFluctuationsNs", "phaseJumpThresholdNs", "reactivityMin",
                "reactivityMax", "reactivityPower", "fineStopTolerance",
                "maxAllowedCoarse", "nbCalibration", "learnTemperatureTable",
                "useTemperatureTable", "oscillatorFactorySettings",
                "trackingOnly", "parameterSaveMinutes", "logMaximumBytes",
                "logRetainedFiles", "windowsTimePublisher"
            };

        private OscillatordConfiguration()
        {
        }

        public int SchemaVersion { get; private set; }
        public bool Disciplining { get; private set; }
        public bool Monitoring { get; private set; }
        public uint DeviceIndex { get; private set; }
        public string DevicePath { get; private set; }
        public string DeviceSerial { get; private set; }
        public int DeviceRetrySeconds { get; private set; }
        public IPAddress SocketAddress { get; private set; }
        public int SocketPort { get; private set; }
        public bool MonitoringAllowControl { get; private set; }
        public string MonitoringControlToken { get; private set; }
        public string NamedPipe { get; private set; }
        public uint GnssBaud { get; private set; }
        public bool WindowsTimePublisher { get; private set; }
        public NativeDisciplineOptions DisciplineOptions { get; private set; }
        public long LogMaximumBytes { get; private set; }
        public int LogRetainedFiles { get; private set; }

        public static string DefaultPath
        {
            get
            {
                return Path.Combine(Environment.GetFolderPath(
                    Environment.SpecialFolder.CommonApplicationData),
                    "OCP Time Card", "Oscillatord", "oscillatord.json");
            }
        }

        public static OscillatordConfiguration Load(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
                path = DefaultPath;
            string fullPath = Path.GetFullPath(path);
            if (!File.Exists(fullPath))
                throw new FileNotFoundException(
                    "The oscillatord configuration file was not found.",
                    fullPath);
            string json = File.ReadAllText(fullPath, Encoding.UTF8);
            JavaScriptSerializer serializer = new JavaScriptSerializer
            {
                MaxJsonLength = 1024 * 1024,
                RecursionLimit = 8
            };
            Dictionary<string, object> values;
            try
            {
                values = serializer.Deserialize<Dictionary<string, object>>(
                    json);
            }
            catch (Exception ex)
            {
                throw new InvalidDataException(
                    "oscillatord.json is not valid JSON.", ex);
            }
            if (values == null)
                throw new InvalidDataException(
                    "oscillatord.json must contain one JSON object.");
            foreach (string key in values.Keys)
            {
                if (!KnownKeys.Contains(key))
                    throw new InvalidDataException(
                        "Unknown oscillatord setting: " + key + ".");
            }

            OscillatordConfiguration result = new OscillatordConfiguration
            {
                SchemaVersion = GetInt(values, "schemaVersion", 1),
                Disciplining = GetBool(values, "disciplining", true),
                Monitoring = GetBool(values, "monitoring", true),
                DeviceIndex = checked((uint)GetInt(values, "deviceIndex", 0)),
                DevicePath = GetString(values, "devicePath", string.Empty),
                DeviceSerial = GetString(values, "deviceSerial", string.Empty),
                DeviceRetrySeconds = GetInt(values, "deviceRetrySeconds", 5),
                SocketPort = GetInt(values, "socketPort", 2958),
                MonitoringAllowControl = GetBool(values,
                    "monitoringAllowControl", false),
                MonitoringControlToken = GetString(values,
                    "monitoringControlToken", string.Empty),
                NamedPipe = GetString(values, "namedPipe",
                    "OcpTimeCard.Oscillatord.v1"),
                GnssBaud = checked((uint)GetInt(values, "gnssBaud", 115200)),
                WindowsTimePublisher = GetBool(values,
                    "windowsTimePublisher", true),
                LogMaximumBytes = GetLong(values, "logMaximumBytes", 2097152),
                LogRetainedFiles = GetInt(values, "logRetainedFiles", 5)
            };
            IPAddress address;
            if (!IPAddress.TryParse(GetString(values, "socketAddress",
                "127.0.0.1"), out address))
                throw new InvalidDataException(
                    "socketAddress must be a numeric IPv4 or IPv6 address.");
            result.SocketAddress = address;
            result.DisciplineOptions = new NativeDisciplineOptions
            {
                BypassSurvey = GetBool(values, "gnssBypassSurvey", false),
                OppositePhaseError = GetBool(values,
                    "oppositePhaseError", false),
                CalibrateFirst = GetBool(values, "calibrateFirst", false),
                Debug = GetInt(values, "debug", 2),
                PhaseResolutionNanoseconds = GetInt(values,
                    "phaseResolutionNs", 5),
                ReferenceFluctuationsNanoseconds = GetInt(values,
                    "refFluctuationsNs", 30),
                PhaseJumpThresholdNanoseconds = GetInt(values,
                    "phaseJumpThresholdNs", 300),
                ReactivityMinimum = GetInt(values, "reactivityMin", 10),
                ReactivityMaximum = GetInt(values, "reactivityMax", 30),
                ReactivityPower = GetInt(values, "reactivityPower", 2),
                FineStopTolerance = GetInt(values, "fineStopTolerance", 100),
                MaximumAllowedCoarse = GetInt(values,
                    "maxAllowedCoarse", 20),
                CalibrationSamples = GetInt(values, "nbCalibration", 50),
                LearnTemperatureTable = GetBool(values,
                    "learnTemperatureTable", false),
                UseTemperatureTable = GetBool(values,
                    "useTemperatureTable", false),
                OscillatorFactorySettings = GetBool(values,
                    "oscillatorFactorySettings", true),
                TrackingOnly = GetBool(values, "trackingOnly", false),
                GnssBaud = result.GnssBaud,
                InitializePhcFromGnss = GetBool(values,
                    "initializePhcFromGnss", true),
                StartupAlignmentTimeout = TimeSpan.FromSeconds(GetInt(values,
                    "startupAlignmentTimeoutSeconds", 300)),
                StartupAlignmentSettling = TimeSpan.FromSeconds(GetInt(values,
                    "startupAlignmentSettlingSeconds", 6)),
                GnssReceiverReconfigure = GetBool(values,
                    "gnssReceiverReconfigure", false),
                GnssPersistConfiguration = GetBool(values,
                    "gnssPersistConfiguration", false),
                GnssPreferredTimeScale = GetString(values,
                    "gnssPreferredTimeScale", "GPS"),
                GnssCableDelayNanoseconds = GetInt(values,
                    "gnssCableDelayNanoseconds", 0),
                GnssRtcmEnabled = GetBool(values,
                    "gnssRtcmEnabled", false),
                ParameterSaveInterval = TimeSpan.FromMinutes(GetInt(values,
                    "parameterSaveMinutes", 60))
            };
            result.Validate();
            return result;
        }

        public static void ImportLegacy(string legacyPath, string outputPath)
        {
            if (!File.Exists(legacyPath))
                throw new FileNotFoundException(
                    "The upstream oscillatord configuration was not found.",
                    legacyPath);
            Dictionary<string, object> output = DefaultDictionary();
            Dictionary<string, string> mapping = LegacyMapping();
            foreach (string rawLine in File.ReadAllLines(legacyPath))
            {
                string line = rawLine.Trim();
                if (line.Length == 0 || line.StartsWith("#",
                    StringComparison.Ordinal))
                    continue;
                int separator = line.IndexOf('=');
                if (separator <= 0)
                    throw new InvalidDataException(
                        "Invalid legacy configuration line: " + rawLine);
                string legacyKey = line.Substring(0, separator).Trim();
                string targetKey;
                if (!mapping.TryGetValue(legacyKey, out targetKey))
                    continue; // Linux device paths have no Windows analogue.
                string value = line.Substring(separator + 1).Trim();
                object parsed;
                bool boolean;
                int number;
                if (bool.TryParse(value, out boolean))
                    parsed = boolean;
                else if (int.TryParse(value, NumberStyles.Integer,
                    CultureInfo.InvariantCulture, out number))
                    parsed = number;
                else
                    parsed = value;
                output[targetKey] = parsed;
            }
            string fullOutputPath = Path.GetFullPath(outputPath);
            string directory = Path.GetDirectoryName(fullOutputPath);
            Directory.CreateDirectory(directory);
            JavaScriptSerializer serializer = new JavaScriptSerializer();
            string temporary = fullOutputPath + ".tmp-" +
                Guid.NewGuid().ToString("N");
            try
            {
                File.WriteAllText(temporary, serializer.Serialize(output),
                    new UTF8Encoding(false));
                /* Validate before replacing an operator's known-good file. */
                Load(temporary);
                if (File.Exists(fullOutputPath))
                    File.Replace(temporary, fullOutputPath, null);
                else
                    File.Move(temporary, fullOutputPath);
            }
            finally
            {
                if (File.Exists(temporary))
                    File.Delete(temporary);
            }
        }

        private void Validate()
        {
            if (SchemaVersion != 1)
                throw new InvalidDataException(
                    "Only oscillatord configuration schemaVersion 1 is supported.");
            if (!Disciplining && !Monitoring)
                throw new InvalidDataException(
                    "At least one of disciplining or monitoring must be enabled.");
            if (DeviceIndex > 31u)
                throw new InvalidDataException("deviceIndex must be 0 through 31.");
            DevicePath = (DevicePath ?? string.Empty).Trim();
            DeviceSerial = (DeviceSerial ?? string.Empty).Trim();
            if (DevicePath.Length > 1024 ||
                (DevicePath.Length != 0 && !DevicePath.StartsWith(
                    @"\\?\", StringComparison.Ordinal)) ||
                DevicePath.IndexOf('\0') >= 0)
                throw new InvalidDataException(
                    "devicePath must be an enumerated Windows device-interface path beginning with \\\\?\\.");
            if (DeviceSerial.Length != 0)
            {
                try { TimeCardDeviceSelector.NormalizeSerial(DeviceSerial); }
                catch (FormatException ex)
                {
                    throw new InvalidDataException(
                        "deviceSerial must contain exactly 12 hexadecimal digits (a six-byte card MAC serial).",
                        ex);
                }
            }
            RequireRange(DeviceRetrySeconds, 1, 300, "deviceRetrySeconds");
            RequireRange(SocketPort, 1, 65535, "socketPort");
            if (string.IsNullOrWhiteSpace(NamedPipe) || NamedPipe.Length > 128 ||
                NamedPipe.IndexOfAny(new[] { '\\', '/', ':' }) >= 0)
                throw new InvalidDataException("namedPipe contains invalid characters.");
            if (MonitoringAllowControl &&
                (MonitoringControlToken == null ||
                 MonitoringControlToken.Length < 32))
                throw new InvalidDataException(
                    "TCP control requires a monitoringControlToken of at least 32 characters.");
            RequireRange((int)GnssBaud, 1200, 2000000, "gnssBaud");
            NativeDisciplineOptions d = DisciplineOptions;
            RequireRange(d.PhaseResolutionNanoseconds, 1, 1000000,
                "phaseResolutionNs");
            RequireRange(d.Debug, 0, 9, "debug");
            RequireRange(d.ReferenceFluctuationsNanoseconds, 0, 1000000,
                "refFluctuationsNs");
            RequireRange(d.PhaseJumpThresholdNanoseconds, 1, 499999999,
                "phaseJumpThresholdNs");
            RequireRange(d.ReactivityMinimum, 1, 100000, "reactivityMin");
            RequireRange(d.ReactivityMaximum, d.ReactivityMinimum, 100000,
                "reactivityMax");
            RequireRange(d.ReactivityPower, 1, 16, "reactivityPower");
            RequireRange(d.CalibrationSamples, 1, 1000, "nbCalibration");
            RequireRange(d.FineStopTolerance, 0, 4800,
                "fineStopTolerance");
            RequireRange(d.MaximumAllowedCoarse, 0, 0x003fffff,
                "maxAllowedCoarse");
            if (d.StartupAlignmentTimeout < TimeSpan.FromSeconds(10) ||
                d.StartupAlignmentTimeout > TimeSpan.FromMinutes(30))
                throw new InvalidDataException(
                    "startupAlignmentTimeoutSeconds must be between 10 and 1800.");
            if (d.StartupAlignmentSettling < TimeSpan.Zero ||
                d.StartupAlignmentSettling > TimeSpan.FromSeconds(60))
                throw new InvalidDataException(
                    "startupAlignmentSettlingSeconds must be between 0 and 60.");
            string timeScale = (d.GnssPreferredTimeScale ?? string.Empty)
                .Trim().ToUpperInvariant();
            if (timeScale != "UTC" && timeScale != "GPS" &&
                timeScale != "GLO" && timeScale != "BDS" &&
                timeScale != "GAL")
                throw new InvalidDataException(
                    "gnssPreferredTimeScale must be UTC, GPS, GLO, BDS, or GAL.");
            if (d.InitializePhcFromGnss && timeScale != "GPS")
                throw new InvalidDataException(
                    "Native PHC startup currently requires gnssPreferredTimeScale GPS so TIM-TP and NAV-PVT share an unambiguous iTOW grid.");
            RequireRange(d.GnssCableDelayNanoseconds, 0, short.MaxValue,
                "gnssCableDelayNanoseconds");
            if (d.ParameterSaveInterval < TimeSpan.FromMinutes(1) ||
                d.ParameterSaveInterval > TimeSpan.FromDays(7))
                throw new InvalidDataException(
                    "parameterSaveMinutes must be between 1 and 10080.");
            if (LogMaximumBytes < 65536 || LogMaximumBytes > 1073741824)
                throw new InvalidDataException(
                    "logMaximumBytes must be between 65536 and 1073741824.");
            RequireRange(LogRetainedFiles, 1, 20, "logRetainedFiles");
        }

        private static void RequireRange(int value, int minimum, int maximum,
            string name)
        {
            if (value < minimum || value > maximum)
                throw new InvalidDataException(string.Format(
                    CultureInfo.InvariantCulture,
                    "{0} must be between {1} and {2}.", name, minimum,
                    maximum));
        }

        private static int GetInt(IDictionary<string, object> values,
            string key, int fallback)
        {
            object value;
            if (!values.TryGetValue(key, out value))
                return fallback;
            try { return Convert.ToInt32(value, CultureInfo.InvariantCulture); }
            catch (Exception ex) { throw TypeError(key, "an integer", ex); }
        }

        private static long GetLong(IDictionary<string, object> values,
            string key, long fallback)
        {
            object value;
            if (!values.TryGetValue(key, out value))
                return fallback;
            try { return Convert.ToInt64(value, CultureInfo.InvariantCulture); }
            catch (Exception ex) { throw TypeError(key, "an integer", ex); }
        }

        private static bool GetBool(IDictionary<string, object> values,
            string key, bool fallback)
        {
            object value;
            if (!values.TryGetValue(key, out value))
                return fallback;
            if (value is bool)
                return (bool)value;
            throw TypeError(key, "true or false", null);
        }

        private static string GetString(IDictionary<string, object> values,
            string key, string fallback)
        {
            object value;
            if (!values.TryGetValue(key, out value))
                return fallback;
            string text = value as string;
            if (text == null)
                throw TypeError(key, "a string", null);
            return text;
        }

        private static InvalidDataException TypeError(string key,
            string expected, Exception inner)
        {
            return new InvalidDataException(key + " must be " + expected + ".",
                inner);
        }

        private static Dictionary<string, object> DefaultDictionary()
        {
            return new JavaScriptSerializer().Deserialize<
                Dictionary<string, object>>(File.ReadAllText(Path.Combine(
                    AppDomain.CurrentDomain.BaseDirectory,
                    "oscillatord.example.json"), Encoding.UTF8));
        }

        private static Dictionary<string, string> LegacyMapping()
        {
            return new Dictionary<string, string>(StringComparer.Ordinal)
            {
                { "disciplining", "disciplining" },
                { "monitoring", "monitoring" },
                { "timecard-index", "deviceIndex" },
                { "socket-address", "socketAddress" },
                { "socket-port", "socketPort" },
                { "monitoring-allow-control", "monitoringAllowControl" },
                { "monitoring-control-token", "monitoringControlToken" },
                { "gnss-bypass-survey", "gnssBypassSurvey" },
                { "gnss-receiver-reconfigure", "gnssReceiverReconfigure" },
                { "gnss-preferred-time-scale", "gnssPreferredTimeScale" },
                { "gnss-cable-delay", "gnssCableDelayNanoseconds" },
                { "gnss-rtcm-enabled", "gnssRtcmEnabled" },
                { "opposite-phase-error", "oppositePhaseError" },
                { "calibrate_first", "calibrateFirst" },
                { "debug", "debug" },
                { "phase_resolution_ns", "phaseResolutionNs" },
                { "ref_fluctuations_ns", "refFluctuationsNs" },
                { "phase_jump_threshold_ns", "phaseJumpThresholdNs" },
                { "reactivity_min", "reactivityMin" },
                { "reactivity_max", "reactivityMax" },
                { "reactivity_power", "reactivityPower" },
                { "fine_stop_tolerance", "fineStopTolerance" },
                { "max_allowed_coarse", "maxAllowedCoarse" },
                { "nb_calibration", "nbCalibration" },
                { "learn_temperature_table", "learnTemperatureTable" },
                { "use_temperature_table", "useTemperatureTable" },
                { "oscillator_factory_settings", "oscillatorFactorySettings" },
                { "tracking_only", "trackingOnly" }
            };
        }
    }

    internal static class TimeCardDeviceSelector
    {
        public static string NormalizeSerial(string serial)
        {
            string value = (serial ?? string.Empty).Trim();
            StringBuilder normalized = new StringBuilder(12);
            foreach (char character in value)
            {
                if ((character >= '0' && character <= '9') ||
                    (character >= 'a' && character <= 'f') ||
                    (character >= 'A' && character <= 'F'))
                {
                    normalized.Append(char.ToUpperInvariant(character));
                }
                else if (character != ':' && character != '-')
                {
                    throw new FormatException(
                        "The Time Card serial contains a non-hexadecimal character.");
                }
            }
            if (normalized.Length != 12)
                throw new FormatException(
                    "The Time Card serial is not six bytes long.");
            return normalized.ToString();
        }

        public static string SelectPath(IList<string> availablePaths,
            string configuredPath, string configuredSerial,
            Func<string, string> serialResolver)
        {
            if (availablePaths == null)
                throw new ArgumentNullException("availablePaths");
            string requiredPath = (configuredPath ?? string.Empty).Trim();
            string requiredSerial = string.IsNullOrWhiteSpace(configuredSerial) ?
                string.Empty : NormalizeSerial(configuredSerial);
            if (requiredPath.Length == 0 && requiredSerial.Length == 0)
                return null;
            if (requiredSerial.Length != 0 && serialResolver == null)
                throw new ArgumentNullException("serialResolver");

            if (requiredPath.Length != 0)
            {
                string selected = null;
                foreach (string available in availablePaths)
                {
                    if (string.Equals(available, requiredPath,
                        StringComparison.OrdinalIgnoreCase))
                    {
                        if (selected != null)
                            throw new InvalidOperationException(
                                "The configured Time Card device path is ambiguous.");
                        selected = available;
                    }
                }
                if (selected == null)
                    throw new InvalidOperationException(
                        "The configured Time Card device path is not present.");
                if (requiredSerial.Length != 0)
                {
                    string actual = serialResolver(selected);
                    if (string.IsNullOrWhiteSpace(actual) ||
                        !string.Equals(NormalizeSerial(actual), requiredSerial,
                            StringComparison.Ordinal))
                        throw new InvalidOperationException(
                            "The card at devicePath does not match deviceSerial.");
                }
                return selected;
            }

            string serialMatch = null;
            Exception lastProbeError = null;
            foreach (string available in availablePaths)
            {
                string actual;
                string normalizedActual;
                try
                {
                    actual = serialResolver(available);
                    normalizedActual = string.IsNullOrWhiteSpace(actual) ?
                        string.Empty : NormalizeSerial(actual);
                }
                catch (Exception ex)
                {
                    lastProbeError = ex;
                    continue;
                }
                if (normalizedActual.Length == 0 ||
                    !string.Equals(normalizedActual, requiredSerial,
                        StringComparison.Ordinal))
                    continue;
                if (serialMatch != null)
                    throw new InvalidOperationException(
                        "More than one Time Card reports the configured deviceSerial; selection is unsafe.");
                serialMatch = available;
            }
            if (serialMatch == null)
                throw new InvalidOperationException(
                    "No present Time Card reports the configured deviceSerial.",
                    lastProbeError);
            return serialMatch;
        }
    }
}
