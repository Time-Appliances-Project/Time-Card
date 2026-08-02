using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;

namespace TimeCardControlCenter
{
    internal sealed class ServiceLog : IDisposable
    {
        public const string EventSource = "OCP Time Card Oscillatord";
        private readonly object gate = new object();
        private readonly string path;
        private readonly long maximumBytes;
        private readonly int retainedFiles;

        public ServiceLog(long maximumLogBytes, int retainedLogFiles)
        {
            maximumBytes = maximumLogBytes;
            retainedFiles = retainedLogFiles;
            string directory = Path.Combine(Environment.GetFolderPath(
                Environment.SpecialFolder.CommonApplicationData),
                "OCP Time Card", "Oscillatord", "Logs");
            Directory.CreateDirectory(directory);
            path = Path.Combine(directory, "oscillatord.log");
        }

        public void Info(string message) { Write("INFO", message, null); }
        public void Warning(string message) { Write("WARN", message, null); }
        public void Error(string message, Exception exception)
        {
            Write("ERROR", message, exception);
        }

        private void Write(string level, string message, Exception exception)
        {
            string detail = exception == null ? string.Empty :
                " | " + exception.GetType().Name + ": " + exception.Message;
            string line = string.Format(CultureInfo.InvariantCulture,
                "{0:O} [{1}] {2}{3}{4}", DateTime.UtcNow, level,
                message ?? string.Empty, detail, Environment.NewLine);
            lock (gate)
            {
                try
                {
                    RotateIfNeeded(Encoding.UTF8.GetByteCount(line));
                    File.AppendAllText(path, line, new UTF8Encoding(false));
                }
                catch
                {
                    // Event Log remains the fallback if ProgramData is full.
                }
            }
            if (level == "ERROR" || level == "WARN")
            {
                try
                {
                    if (EventLog.SourceExists(EventSource))
                        EventLog.WriteEntry(EventSource,
                            (message ?? string.Empty) + detail,
                            level == "ERROR" ? EventLogEntryType.Error :
                                EventLogEntryType.Warning);
                }
                catch
                {
                }
            }
        }

        private void RotateIfNeeded(int additionalBytes)
        {
            FileInfo info = new FileInfo(path);
            if (!info.Exists || info.Length + additionalBytes <= maximumBytes)
                return;
            string oldest = path + "." + retainedFiles.ToString(
                CultureInfo.InvariantCulture);
            if (File.Exists(oldest))
                File.Delete(oldest);
            for (int index = retainedFiles - 1; index >= 1; --index)
            {
                string source = path + "." + index.ToString(
                    CultureInfo.InvariantCulture);
                if (File.Exists(source))
                    File.Move(source, path + "." + (index + 1).ToString(
                        CultureInfo.InvariantCulture));
            }
            File.Move(path, path + ".1");
        }

        public void Dispose()
        {
        }
    }
}
