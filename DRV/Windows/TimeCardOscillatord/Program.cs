using System;
using System.IO;
using System.ServiceProcess;
using System.Threading;

namespace TimeCardControlCenter
{
    internal static class Program
    {
        private static int Main(string[] args)
        {
            try
            {
                string configPath = GetOption(args, "--config") ??
                    OscillatordConfiguration.DefaultPath;
                if (HasOption(args, "--validate-config"))
                {
                    OscillatordConfiguration.Load(configPath);
                    Console.WriteLine("Configuration valid: " +
                        Path.GetFullPath(configPath));
                    return 0;
                }
                if (HasOption(args, "--import-config"))
                {
                    string legacy = GetOption(args, "--import-config");
                    if (string.IsNullOrWhiteSpace(legacy))
                        throw new ArgumentException(
                            "--import-config requires an upstream configuration path.");
                    OscillatordConfiguration.ImportLegacy(legacy, configPath);
                    Console.WriteLine("Imported configuration: " +
                        Path.GetFullPath(configPath));
                    return 0;
                }
                OscillatordConfiguration configuration =
                    OscillatordConfiguration.Load(configPath);
                if (HasOption(args, "--console"))
                    return RunConsole(configuration);
                if (HasOption(args, "--service") ||
                    !Environment.UserInteractive)
                {
                    ServiceBase.Run(new OscillatordWindowsService(
                        configuration));
                    return 0;
                }
                Console.Error.WriteLine(
                    "Usage: TimeCardOscillatord.exe --service | --console | " +
                    "--validate-config [--config PATH] | " +
                    "--import-config PATH [--config OUTPUT]");
                return 2;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("TimeCardOscillatord: " + ex.Message);
                return 1;
            }
        }

        private static int RunConsole(OscillatordConfiguration configuration)
        {
            using (ServiceLog log = new ServiceLog(
                configuration.LogMaximumBytes,
                configuration.LogRetainedFiles))
            using (OscillatordRuntime runtime = new OscillatordRuntime(
                configuration, log))
            using (ManualResetEvent exit = new ManualResetEvent(false))
            {
                ConsoleCancelEventHandler handler = (sender, eventArgs) =>
                {
                    eventArgs.Cancel = true;
                    exit.Set();
                };
                Console.CancelKeyPress += handler;
                runtime.Start();
                Console.WriteLine(
                    "Native Windows oscillatord is running. Press Ctrl+C to stop.");
                exit.WaitOne();
                runtime.StopAsync().GetAwaiter().GetResult();
                Console.CancelKeyPress -= handler;
            }
            return 0;
        }

        private static bool HasOption(string[] args, string option)
        {
            foreach (string argument in args)
            {
                if (string.Equals(argument, option,
                    StringComparison.OrdinalIgnoreCase))
                    return true;
            }
            return false;
        }

        private static string GetOption(string[] args, string option)
        {
            for (int index = 0; index < args.Length; ++index)
            {
                if (!string.Equals(args[index], option,
                    StringComparison.OrdinalIgnoreCase))
                    continue;
                return index + 1 < args.Length &&
                    !args[index + 1].StartsWith("--", StringComparison.Ordinal) ?
                    args[index + 1] : null;
            }
            return null;
        }
    }
}
