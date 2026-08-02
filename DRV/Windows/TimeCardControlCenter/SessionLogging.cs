using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace TimeCardControlCenter
{
    public enum SessionLogSeverity
    {
        Trace,
        Information,
        Warning,
        Error
    }

    public sealed class SessionLogRecord
    {
        public SessionLogRecord(DateTime timestampUtc,
                                SessionLogSeverity severity,
                                string category,
                                string cardContext,
                                string message)
        {
            TimestampUtc = timestampUtc.Kind == DateTimeKind.Utc ? timestampUtc :
                timestampUtc.ToUniversalTime();
            Severity = severity;
            Category = SessionLogStore.CleanField(category, "Application");
            CardContext = SessionLogStore.CleanField(cardContext, "offline");
            Message = SessionLogStore.CleanField(message, "(empty message)");
        }

        public DateTime TimestampUtc { get; private set; }
        public SessionLogSeverity Severity { get; private set; }
        public string Category { get; private set; }
        public string CardContext { get; private set; }
        public string Message { get; private set; }

        public string SeverityLabel
        {
            get
            {
                switch (Severity)
                {
                    case SessionLogSeverity.Trace:
                        return "TRACE";
                    case SessionLogSeverity.Warning:
                        return "WARN";
                    case SessionLogSeverity.Error:
                        return "ERROR";
                    default:
                        return "INFO";
                }
            }
        }

        public string ToText()
        {
            return string.Format(CultureInfo.InvariantCulture,
                "{0:yyyy-MM-ddTHH:mm:ss.fffZ} [{1}] [{2}] [{3}] {4}",
                TimestampUtc, SeverityLabel, Category, CardContext, Message);
        }
    }

    public sealed class SessionLogFilter
    {
        public SessionLogSeverity? Severity { get; set; }
        public string Category { get; set; }
        public string SearchText { get; set; }

        internal bool Matches(SessionLogRecord record)
        {
            if (record == null)
                return false;
            if (Severity.HasValue && record.Severity != Severity.Value)
                return false;
            if (!string.IsNullOrWhiteSpace(Category) &&
                !string.Equals(record.Category, Category,
                    StringComparison.OrdinalIgnoreCase))
                return false;
            if (string.IsNullOrWhiteSpace(SearchText))
                return true;

            string search = SearchText.Trim();
            return Contains(record.Message, search) ||
                Contains(record.Category, search) ||
                Contains(record.CardContext, search) ||
                Contains(record.SeverityLabel, search) ||
                Contains(record.TimestampUtc.ToString("O",
                    CultureInfo.InvariantCulture), search);
        }

        private static bool Contains(string value, string search)
        {
            return value != null && value.IndexOf(search,
                StringComparison.OrdinalIgnoreCase) >= 0;
        }
    }

    public sealed class SessionLogStore
    {
        private readonly object gate = new object();
        private readonly List<SessionLogRecord> records =
            new List<SessionLogRecord>();
        private long droppedRecordCount;

        public SessionLogStore(int capacity)
        {
            if (capacity < 1)
                throw new ArgumentOutOfRangeException("capacity",
                    "Session log capacity must be at least one record.");
            Capacity = capacity;
        }

        public int Capacity { get; private set; }

        public int Count
        {
            get
            {
                lock (gate)
                    return records.Count;
            }
        }

        public long DroppedRecordCount
        {
            get
            {
                lock (gate)
                    return droppedRecordCount;
            }
        }

        public SessionLogRecord Append(DateTime timestampUtc,
                                       SessionLogSeverity severity,
                                       string category,
                                       string cardContext,
                                       string message)
        {
            SessionLogRecord record = new SessionLogRecord(timestampUtc,
                severity, category, cardContext, message);
            lock (gate)
            {
                if (records.Count == Capacity)
                {
                    records.RemoveAt(0);
                    droppedRecordCount++;
                }
                records.Add(record);
            }
            return record;
        }

        public void Clear()
        {
            lock (gate)
            {
                records.Clear();
                droppedRecordCount = 0;
            }
        }

        public List<SessionLogRecord> Query(SessionLogFilter filter)
        {
            lock (gate)
            {
                List<SessionLogRecord> result =
                    new List<SessionLogRecord>(records.Count);
                foreach (SessionLogRecord record in records)
                {
                    if (filter == null || filter.Matches(record))
                        result.Add(record);
                }
                return result;
            }
        }

        public string ToText()
        {
            return ToText(null);
        }

        public string ToText(SessionLogFilter filter)
        {
            List<SessionLogRecord> snapshot = Query(filter);
            StringBuilder text = new StringBuilder();
            foreach (SessionLogRecord record in snapshot)
                text.Append(record.ToText()).Append("\r\n");
            return text.ToString();
        }

        public string ToJson()
        {
            return ToJson(null);
        }

        public string ToJson(SessionLogFilter filter)
        {
            List<SessionLogRecord> snapshot;
            int retainedCount;
            long droppedCount;
            lock (gate)
            {
                retainedCount = records.Count;
                droppedCount = droppedRecordCount;
                snapshot = new List<SessionLogRecord>(records.Count);
                foreach (SessionLogRecord record in records)
                {
                    if (filter == null || filter.Matches(record))
                        snapshot.Add(record);
                }
            }

            StringBuilder json = new StringBuilder();
            json.Append("{\r\n")
                .Append("  \"schemaVersion\": 1,\r\n")
                .AppendFormat(CultureInfo.InvariantCulture,
                    "  \"exportedAtUtc\": \"{0:yyyy-MM-ddTHH:mm:ss.fffZ}\",\r\n",
                    DateTime.UtcNow)
                .AppendFormat(CultureInfo.InvariantCulture,
                    "  \"capacity\": {0},\r\n", Capacity)
                .AppendFormat(CultureInfo.InvariantCulture,
                    "  \"retainedRecordCount\": {0},\r\n", retainedCount)
                .AppendFormat(CultureInfo.InvariantCulture,
                    "  \"exportedRecordCount\": {0},\r\n", snapshot.Count)
                .AppendFormat(CultureInfo.InvariantCulture,
                    "  \"droppedRecordCount\": {0},\r\n", droppedCount)
                .Append("  \"records\": [\r\n");

            for (int index = 0; index < snapshot.Count; index++)
            {
                SessionLogRecord record = snapshot[index];
                json.Append("    {")
                    .AppendFormat(CultureInfo.InvariantCulture,
                        "\"timestampUtc\":\"{0:yyyy-MM-ddTHH:mm:ss.fffZ}\",",
                        record.TimestampUtc)
                    .Append("\"severity\":\"")
                    .Append(EscapeJson(record.Severity.ToString()))
                    .Append("\",\"category\":\"")
                    .Append(EscapeJson(record.Category))
                    .Append("\",\"cardContext\":\"")
                    .Append(EscapeJson(record.CardContext))
                    .Append("\",\"message\":\"")
                    .Append(EscapeJson(record.Message))
                    .Append("\"}");
                if (index + 1 < snapshot.Count)
                    json.Append(',');
                json.Append("\r\n");
            }
            json.Append("  ]\r\n}\r\n");
            return json.ToString();
        }

        public static SessionLogSeverity InferSeverity(string message)
        {
            string value = message ?? string.Empty;
            if (ContainsAny(value, " failed", "failure", "error", "exception",
                "denied", "rejected", "connection lost", "could not"))
                return SessionLogSeverity.Error;
            if (ContainsAny(value, "warning", "unavailable", "not present",
                "not connected", "partial", "timeout", "timed out",
                "not fitted", "not implemented"))
                return SessionLogSeverity.Warning;
            if (ContainsAny(value, " refreshed", "sampled", "observed",
                "poll completed"))
                return SessionLogSeverity.Trace;
            return SessionLogSeverity.Information;
        }

        public static string InferCategory(string message)
        {
            string value = message ?? string.Empty;
            if (ContainsAny(value, "SA53", "mRO-50", "atomic", "oscillator",
                "holdover", "disciplin"))
                return "Atomic clock";
            if (ContainsAny(value, "UART", "COM", "NMEA", "u-blox", "UBX",
                "GNSS", "satellite", "receiver"))
                return "Serial & GNSS";
            if (ContainsAny(value, "I2C", "IIC", "sensor", "EEPROM", "LED",
                "BME", "INA219", "BNO", "IMU", "temperature"))
                return "I2C & Sensors";
            if (ContainsAny(value, "FPGA", "flash", "bitstream", "firmware",
                "JEDEC"))
                return "FPGA";
            if (ContainsAny(value, "SMA", "signal generator", "frequency counter",
                "IRIG", "DCF77", "PPS", "timing I/O"))
                return "Timing I/O";
            if (ContainsAny(value, "PHC", "clock", "UTC", "time sync",
                "cross-timestamp"))
                return "Clock";
            if (ContainsAny(value, "profile", "configuration", "setting",
                "configured", "set to"))
                return "Configuration";
            if (ContainsAny(value, "diagnostic", "self-test", "support bundle",
                "session log"))
                return "Diagnostics";
            if (ContainsAny(value, "connected", "connection", "driver", "ABI",
                "administrator", "elevation", "device"))
                return "Connection";
            return "Application";
        }

        internal static string CleanField(string value, string fallback)
        {
            string cleaned = string.IsNullOrWhiteSpace(value) ? fallback :
                value.Trim();
            return cleaned.Replace("\r\n", " | ").Replace('\r', ' ')
                .Replace('\n', ' ');
        }

        private static bool ContainsAny(string value, params string[] terms)
        {
            foreach (string term in terms)
            {
                if (value.IndexOf(term, StringComparison.OrdinalIgnoreCase) >= 0)
                    return true;
            }
            return false;
        }

        private static string EscapeJson(string value)
        {
            if (string.IsNullOrEmpty(value))
                return string.Empty;
            StringBuilder escaped = new StringBuilder(value.Length + 8);
            foreach (char character in value)
            {
                switch (character)
                {
                    case '\\':
                        escaped.Append("\\\\");
                        break;
                    case '"':
                        escaped.Append("\\\"");
                        break;
                    case '\b':
                        escaped.Append("\\b");
                        break;
                    case '\f':
                        escaped.Append("\\f");
                        break;
                    case '\n':
                        escaped.Append("\\n");
                        break;
                    case '\r':
                        escaped.Append("\\r");
                        break;
                    case '\t':
                        escaped.Append("\\t");
                        break;
                    default:
                        if (character < 0x20)
                            escaped.AppendFormat(CultureInfo.InvariantCulture,
                                "\\u{0:X4}", (int)character);
                        else
                            escaped.Append(character);
                        break;
                }
            }
            return escaped.ToString();
        }
    }
}
