using System;
using System.ServiceProcess;

namespace TimeCardControlCenter
{
    internal sealed class OscillatordWindowsService : ServiceBase
    {
        public const string WindowsServiceName = "OcpTimeCardOscillatord";
        private readonly OscillatordConfiguration configuration;
        private ServiceLog log;
        private OscillatordRuntime runtime;

        public OscillatordWindowsService(
            OscillatordConfiguration activeConfiguration)
        {
            configuration = activeConfiguration;
            ServiceName = WindowsServiceName;
            CanStop = true;
            CanShutdown = true;
            CanHandlePowerEvent = true;
            AutoLog = false;
        }

        protected override void OnStart(string[] args)
        {
            ServiceLog startedLog = new ServiceLog(
                configuration.LogMaximumBytes,
                configuration.LogRetainedFiles);
            OscillatordRuntime startedRuntime = null;
            try
            {
                startedRuntime = new OscillatordRuntime(configuration,
                    startedLog);
                startedRuntime.Start();
                log = startedLog;
                runtime = startedRuntime;
            }
            catch
            {
                if (startedRuntime != null)
                {
                    try { startedRuntime.StopAsync().GetAwaiter().GetResult(); }
                    catch { }
                    try { startedRuntime.Dispose(); }
                    catch { }
                }
                startedLog.Dispose();
                throw;
            }
        }

        protected override void OnStop()
        {
            StopRuntime();
        }

        protected override void OnShutdown()
        {
            StopRuntime();
            base.OnShutdown();
        }

        protected override bool OnPowerEvent(PowerBroadcastStatus powerStatus)
        {
            if (powerStatus == PowerBroadcastStatus.Suspend)
            {
                if (runtime != null)
                    runtime.StopAsync().GetAwaiter().GetResult();
            }
            else if (powerStatus == PowerBroadcastStatus.ResumeAutomatic ||
                     powerStatus == PowerBroadcastStatus.ResumeSuspend)
            {
                if (runtime != null)
                    runtime.Start();
            }
            return base.OnPowerEvent(powerStatus);
        }

        private void StopRuntime()
        {
            OscillatordRuntime active = runtime;
            runtime = null;
            try
            {
                if (active != null)
                {
                    try
                    {
                        active.StopAsync().GetAwaiter().GetResult();
                    }
                    finally
                    {
                        active.Dispose();
                    }
                }
            }
            finally
            {
                if (log != null)
                {
                    log.Dispose();
                    log = null;
                }
            }
        }
    }
}
