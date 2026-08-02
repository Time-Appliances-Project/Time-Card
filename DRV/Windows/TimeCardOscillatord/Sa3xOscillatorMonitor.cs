using System;
using System.Globalization;

namespace TimeCardControlCenter
{
    public interface ISa3xTelemetryTransport
    {
        string QueryTelemetry(uint timeoutMilliseconds);
    }

    public sealed class Sa3xTelemetry
    {
        public int Bite { get; internal set; }
        public string Firmware { get; internal set; }
        public string Serial { get; internal set; }
        public int TecControlMilliCelsius { get; internal set; }
        public int RfControlTenthsMillivolt { get; internal set; }
        public int DdsFrequencyCenterHundredthsHertz { get; internal set; }
        public int CellHeaterCurrentMilliamps { get; internal set; }
        public int DcSignalMillivolts { get; internal set; }
        public double TemperatureCelsius { get; internal set; }
        public int DigitalTuningHundredthsHertz { get; internal set; }
        public bool AnalogTuningEnabled { get; internal set; }
        public int AnalogTuningMillivolts { get; internal set; }

        public bool Locked
        {
            get { return Bite == 0; }
        }
    }

    public static class Sa3xTelemetryParser
    {
        public static bool TryParse(string line, out Sa3xTelemetry telemetry,
            out string error)
        {
            telemetry = null;
            error = null;
            if (string.IsNullOrWhiteSpace(line))
            {
                error = "SA3x telemetry is empty.";
                return false;
            }

            string normalized = line.Trim('\r', '\n', ' ');
            if (normalized.Length > 0 && normalized[0] == '\0')
                normalized = "0" + normalized.Substring(1);
            string[] fields = normalized.Split(',');
            if (fields.Length < 9)
            {
                error = "SA3x telemetry contains only " +
                    fields.Length.ToString(CultureInfo.InvariantCulture) +
                    " fields; at least 9 are required.";
                return false;
            }

            int bite;
            if (!TryParseBite(fields[0], out bite))
            {
                error = "The SA3x BITE field is invalid.";
                return false;
            }

            int tec;
            int rf;
            int dds;
            int heater;
            int dc;
            int temperature;
            if (!TryInt(fields, 3, out tec) || !TryInt(fields, 4, out rf) ||
                !TryInt(fields, 5, out dds) || !TryInt(fields, 6, out heater) ||
                !TryInt(fields, 7, out dc) || !TryInt(fields, 8, out temperature))
            {
                error = "One of the required SA3x numeric telemetry fields is invalid.";
                return false;
            }

            int digital = 0;
            int analog = 0;
            bool analogEnabled = false;
            if (fields.Length > 9 && !TryInt(fields, 9, out digital))
            {
                error = "The SA3x digital-tuning field is invalid.";
                return false;
            }
            if (fields.Length > 10 && !TryParseSwitch(fields[10],
                out analogEnabled))
            {
                error = "The SA3x analog-tuning state is invalid.";
                return false;
            }
            if (fields.Length > 11 && !TryInt(fields, 11, out analog))
            {
                error = "The SA3x analog-tuning value is invalid.";
                return false;
            }

            telemetry = new Sa3xTelemetry
            {
                Bite = bite,
                Firmware = fields[1].Trim(),
                Serial = fields[2].Trim(),
                TecControlMilliCelsius = tec,
                RfControlTenthsMillivolt = rf,
                DdsFrequencyCenterHundredthsHertz = dds,
                CellHeaterCurrentMilliamps = heater,
                DcSignalMillivolts = dc,
                TemperatureCelsius = temperature / 1000.0,
                DigitalTuningHundredthsHertz = digital,
                AnalogTuningEnabled = analogEnabled,
                AnalogTuningMillivolts = analog
            };
            return true;
        }

        private static bool TryParseBite(string value, out int parsed)
        {
            string normalized = value == null ? string.Empty : value.Trim();
            if (int.TryParse(normalized, NumberStyles.Integer,
                CultureInfo.InvariantCulture, out parsed))
                return true;
            if (normalized.Length == 1)
            {
                parsed = normalized[0];
                return true;
            }
            parsed = 0;
            return false;
        }

        private static bool TryParseSwitch(string value, out bool parsed)
        {
            string normalized = value == null ? string.Empty : value.Trim();
            if (string.Equals(normalized, "1", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(normalized, "Y", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(normalized, "ON", StringComparison.OrdinalIgnoreCase))
            {
                parsed = true;
                return true;
            }
            if (string.Equals(normalized, "0", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(normalized, "N", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(normalized, "OFF", StringComparison.OrdinalIgnoreCase))
            {
                parsed = false;
                return true;
            }
            parsed = false;
            return false;
        }

        private static bool TryInt(string[] fields, int index, out int value)
        {
            if (index >= fields.Length)
            {
                value = 0;
                return false;
            }
            return int.TryParse(fields[index].Trim(), NumberStyles.Integer,
                CultureInfo.InvariantCulture, out value);
        }
    }

    public sealed class Sa3xOscillatorMonitor
    {
        private readonly object gate = new object();
        private readonly ISa3xTelemetryTransport transport;
        private readonly ISa5xMonotonicClock clock;
        private readonly uint timeoutMilliseconds;
        private readonly long minimumPollIntervalMilliseconds;
        private Sa3xTelemetry cached;
        private long lastPollMilliseconds = -1;

        public Sa3xOscillatorMonitor(ISa3xTelemetryTransport telemetryTransport,
            ISa5xMonotonicClock monotonicClock, uint commandTimeoutMilliseconds,
            long minimumIntervalMilliseconds)
        {
            if (telemetryTransport == null)
                throw new ArgumentNullException("telemetryTransport");
            if (monotonicClock == null)
                throw new ArgumentNullException("monotonicClock");
            if (commandTimeoutMilliseconds < 10 || commandTimeoutMilliseconds > 5000)
                throw new ArgumentOutOfRangeException("commandTimeoutMilliseconds");
            if (minimumIntervalMilliseconds < 0 || minimumIntervalMilliseconds > 60000)
                throw new ArgumentOutOfRangeException("minimumIntervalMilliseconds");
            transport = telemetryTransport;
            clock = monotonicClock;
            timeoutMilliseconds = commandTimeoutMilliseconds;
            minimumPollIntervalMilliseconds = minimumIntervalMilliseconds;
        }

        public Sa3xTelemetry Poll()
        {
            lock (gate)
            {
                long now = clock.Milliseconds;
                if (cached != null && lastPollMilliseconds >= 0 &&
                    now >= lastPollMilliseconds &&
                    now - lastPollMilliseconds < minimumPollIntervalMilliseconds)
                    return cached;

                string response = transport.QueryTelemetry(timeoutMilliseconds);
                Sa3xTelemetry parsed;
                string error;
                if (!Sa3xTelemetryParser.TryParse(response, out parsed, out error))
                    throw new InvalidOperationException(error);
                cached = parsed;
                lastPollMilliseconds = now;
                return parsed;
            }
        }
    }
}
