using System;
using System.Collections.Generic;
using System.Globalization;

namespace TimeCardControlCenter
{
    public sealed class Sa53Snapshot
    {
        private readonly Dictionary<string, string> values;

        internal Sa53Snapshot(Dictionary<string, string> values, IList<string> warnings)
        {
            this.values = values;
            List<string> warningList = warnings as List<string> ?? new List<string>(warnings);
            Warnings = warningList.AsReadOnly();
        }

        public IList<string> Warnings { get; private set; }

        public string Value(string name)
        {
            string result;
            return values.TryGetValue(name, out result) ? result : null;
        }

        public bool TryInt64(string name, out long value)
        {
            string text = Value(name);
            return long.TryParse(text, NumberStyles.Integer,
                CultureInfo.InvariantCulture, out value);
        }

        public bool TryDouble(string name, out double value)
        {
            string text = Value(name);
            return double.TryParse(text, NumberStyles.Float,
                CultureInfo.InvariantCulture, out value);
        }

        public bool TryBoolean(string name, out bool value)
        {
            string text = Value(name);
            if (string.Equals(text, "1", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(text, "true", StringComparison.OrdinalIgnoreCase))
            {
                value = true;
                return true;
            }
            if (string.Equals(text, "0", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(text, "false", StringComparison.OrdinalIgnoreCase))
            {
                value = false;
                return true;
            }
            value = false;
            return false;
        }
    }

    public sealed class Sa53Client
    {
        private static readonly string[] IdentityCommands =
        {
            "device?", "pn?", "serial?", "swrev?", "hwrev?"
        };

        private static readonly string[] TelemetryParameters =
        {
            "Alarms", "PpsInDetected", "TotalRuntime", "TotalLocktime", "Locked",
            "TimeOfDay", "DisciplineLocked", "PpsOffset", "PpsWidth", "CableDelay",
            "Disciplining", "PpsSource", "TauPps0", "PpsQErr", "PhaseLimit",
            "JamSyncing", "Phase", "LastCorrection", "TauPps1", "PhaseMetering",
            "DisciplineThresholdPps0", "DisciplineThresholdPps1", "LaserTempSet",
            "OscTuning", "OvenCurrent", "DCSignal", "AnalogTuning", "Temperature",
            "DigitalTuning", "PowerSupply", "AnalogTuningEnabled", "EffectiveTuning",
            "LockProgress", "DisciplineTuning"
        };

        private static readonly HashSet<string> WritableParameters =
            new HashSet<string>(StringComparer.Ordinal)
            {
                "TimeOfDay", "PpsOffset", "PpsWidth", "CableDelay", "Disciplining",
                "PpsSource", "TauPps0", "PpsQErr", "PhaseLimit", "TauPps1",
                "PhaseMetering", "DisciplineThresholdPps0", "DisciplineThresholdPps1",
                "DigitalTuning", "AnalogTuningEnabled"
            };

        private readonly TimeCardClient transport;

        public Sa53Client(TimeCardClient transport)
        {
            if (transport == null)
                throw new ArgumentNullException("transport");
            this.transport = transport;
        }

        public Sa53Snapshot Refresh()
        {
            Dictionary<string, string> values =
                new Dictionary<string, string>(StringComparer.Ordinal);
            List<string> warnings = new List<string>();

            string device = Query("device?");
            values["device?"] = device;
            if (device.IndexOf("sa5", StringComparison.OrdinalIgnoreCase) < 0 &&
                device.IndexOf("sa53", StringComparison.OrdinalIgnoreCase) < 0)
                warnings.Add("The serial device identified itself as '" + device +
                    "', not as an SA5x family clock.");

            foreach (string command in IdentityCommands)
            {
                if (command == "device?")
                    continue;
                TryQuery(command, command, values, warnings);
            }
            foreach (string parameter in TelemetryParameters)
                TryQuery("get," + parameter, parameter, values, warnings);

            return new Sa53Snapshot(values, warnings);
        }

        private string Query(string command)
        {
            return Unquote(transport.ExecuteSa53Command(command, 1200));
        }

        public void Set(string parameter, long value)
        {
            if (!WritableParameters.Contains(parameter))
                throw new ArgumentException("The SA53 parameter is not writable by this application.",
                    "parameter");
            Query("set," + parameter + "," +
                value.ToString(CultureInfo.InvariantCulture));
        }

        public void AddDigitalTuning(long delta)
        {
            Query("add,DigitalTuning," + delta.ToString(CultureInfo.InvariantCulture));
        }

        public void Store()
        {
            Query("store");
        }

        public void Load()
        {
            Query("load");
        }

        public void JamSync()
        {
            Query("sync");
        }

        public void AcknowledgeAlarms(long alarmMask)
        {
            if (alarmMask <= 0 || alarmMask > uint.MaxValue)
                throw new ArgumentOutOfRangeException("alarmMask");
            Query("ackalm," + alarmMask.ToString(CultureInfo.InvariantCulture));
        }

        private void TryQuery(string command, string key,
            IDictionary<string, string> values, ICollection<string> warnings)
        {
            try
            {
                values[key] = Query(command);
            }
            catch (Exception ex)
            {
                warnings.Add(command + ": " + ex.Message);
            }
        }

        private static string Unquote(string value)
        {
            if (value == null)
                return null;
            value = value.Trim();
            if (value.Length >= 2 && value[0] == '"' &&
                value[value.Length - 1] == '"')
                return value.Substring(1, value.Length - 2);
            return value;
        }
    }
}
