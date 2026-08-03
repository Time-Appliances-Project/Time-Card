using System;
using System.Collections.Generic;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace TimeCardControlCenter
{
    internal enum MonitoringOrigin
    {
        Tcp,
        NamedPipeAdministrator,
        NamedPipeReadOnly
    }

    internal sealed class OscillatordRuntime : IDisposable
    {
        public const string ServiceVersion = "1.43.0";
        private readonly object deviceGate = new object();
        private readonly object snapshotGate = new object();
        private readonly SemaphoreSlim lifecycle = new SemaphoreSlim(1, 1);
        private readonly OscillatordConfiguration configuration;
        private readonly ServiceLog log;
        private CancellationTokenSource cancellation;
        private Task coordinator;
        private MonitoringServer tcpServer;
        private NamedPipeMonitoringServer pipeServer;
        private WindowsTimeSamplePublisher timePublisher;
        private NativeRtcmPublisher rtcmPublisher;
        private TimeCardClient client;
        private TimeCardCapabilities capabilities;
        private NativeDisciplineEngine discipline;
        private NativeDisciplineStatus nativeStatus;
        private Sa5xOscillatorManager sa5xManager;
        private Sa5xManagerSnapshot sa5xStatus;
        private Sa3xOscillatorMonitor sa3xMonitor;
        private Sa3xTelemetry sa3xStatus;
        private bool atomicLeaseAcquired;
        private string cardSerial;
        private OscillatordSnapshot snapshot;
        private string lastConnectionError;

        public OscillatordRuntime(OscillatordConfiguration activeConfiguration,
            ServiceLog serviceLog)
        {
            configuration = activeConfiguration ?? throw new ArgumentNullException(
                "activeConfiguration");
            log = serviceLog ?? throw new ArgumentNullException("serviceLog");
            snapshot = CreateBaseSnapshot("STARTING");
        }

        public void Start()
        {
            lifecycle.Wait();
            try
            {
                if (cancellation != null)
                {
                    if (cancellation.IsCancellationRequested)
                        throw new InvalidOperationException(
                            "Runtime cleanup is incomplete; call StopAsync again before restarting.");
                    return;
                }
                CancellationTokenSource source = new CancellationTokenSource();
                cancellation = source;
                try
                {
                    if (configuration.WindowsTimePublisher)
                    {
                        try
                        {
                            timePublisher = new WindowsTimeSamplePublisher();
                            log.Info("Windows Time sample bridge is available.");
                        }
                        catch (Exception ex)
                        {
                            /* Discipline remains usable when W32Time is absent. */
                            if (timePublisher != null)
                            {
                                try { timePublisher.Dispose(); }
                                catch { }
                                timePublisher = null;
                            }
                            log.Warning("Windows Time sample bridge is unavailable: " +
                                ex.Message);
                        }
                    }
                    if (configuration.DisciplineOptions.GnssRtcmEnabled)
                    {
                        rtcmPublisher = new NativeRtcmPublisher();
                        rtcmPublisher.Start(source.Token);
                        configuration.DisciplineOptions.GnssRawObserver =
                            rtcmPublisher.Feed;
                        log.Info("Native RTCM/RAWX/SFRBX pipe is available at " +
                            @"\\.\pipe\" + NativeRtcmPublisher.DefaultPipeName +
                            ".");
                    }
                    if (configuration.Monitoring)
                    {
                        tcpServer = new MonitoringServer(configuration, this, log);
                        tcpServer.Start(source.Token);
                        pipeServer = new NamedPipeMonitoringServer(
                            configuration.NamedPipe, this, log);
                        pipeServer.Start(source.Token);
                    }
                    coordinator = Task.Run(() => CoordinatorAsync(source.Token));
                    log.Info("Native Windows oscillatord runtime started.");
                }
                catch
                {
                    try
                    {
                        StopCoreAsync(source).GetAwaiter().GetResult();
                    }
                    catch (Exception cleanupError)
                    {
                        log.Error("Runtime startup rollback failed.", cleanupError);
                    }
                    throw;
                }
            }
            finally
            {
                lifecycle.Release();
            }
        }

        public async Task StopAsync()
        {
            await lifecycle.WaitAsync().ConfigureAwait(false);
            try
            {
                if (cancellation == null && coordinator == null &&
                    tcpServer == null && pipeServer == null &&
                    rtcmPublisher == null && timePublisher == null &&
                    client == null && discipline == null)
                    return;
                await StopCoreAsync(cancellation).ConfigureAwait(false);
            }
            finally
            {
                lifecycle.Release();
            }
        }

        private async Task StopCoreAsync(CancellationTokenSource source)
        {
            MonitoringServer activeTcp = tcpServer;
            NamedPipeMonitoringServer activePipe = pipeServer;
            Task activeCoordinator = coordinator;
            NativeRtcmPublisher activeRtcm = rtcmPublisher;
            WindowsTimeSamplePublisher activeTime = timePublisher;
            List<Exception> failures = new List<Exception>();
            bool tcpStopped = activeTcp == null;
            bool pipeStopped = activePipe == null;
            bool coordinatorStopped = activeCoordinator == null;
            bool disconnected = false;
            bool rtcmStopped = activeRtcm == null;
            bool timeStopped = activeTime == null;

            try
            {
                if (source != null)
                {
                    try { source.Cancel(); }
                    catch (ObjectDisposedException) { }
                }
                configuration.DisciplineOptions.GnssRawObserver = null;

                Task tcpStop = activeTcp == null ? Task.CompletedTask :
                    activeTcp.StopAsync();
                Task pipeStop = activePipe == null ? Task.CompletedTask :
                    activePipe.StopAsync();
                tcpStopped = await TryCleanupAsync(tcpStop,
                    "Monitoring TCP cleanup failed.", failures)
                    .ConfigureAwait(false);
                pipeStopped = await TryCleanupAsync(pipeStop,
                    "Monitoring pipe cleanup failed.", failures)
                    .ConfigureAwait(false);
                if (activeCoordinator != null)
                    coordinatorStopped = await TryCleanupAsync(activeCoordinator,
                        "Coordinator cleanup failed.", failures)
                        .ConfigureAwait(false);
                disconnected = await TryCleanupAsync(
                    DisconnectAsync("STOPPED"),
                    "Time Card cleanup failed.", failures)
                    .ConfigureAwait(false);

                if (activeRtcm != null)
                {
                    try
                    {
                        activeRtcm.Dispose();
                        rtcmStopped = true;
                    }
                    catch (Exception ex)
                    {
                        log.Error("Native GNSS publisher cleanup failed.", ex);
                        failures.Add(ex);
                    }
                }
                if (activeTime != null)
                {
                    try
                    {
                        activeTime.Dispose();
                        timeStopped = true;
                    }
                    catch (Exception ex)
                    {
                        log.Error("Windows Time bridge cleanup failed.", ex);
                        failures.Add(ex);
                    }
                }
            }
            finally
            {
                if (tcpStopped && ReferenceEquals(tcpServer, activeTcp))
                    tcpServer = null;
                if (pipeStopped && ReferenceEquals(pipeServer, activePipe))
                    pipeServer = null;
                if (coordinatorStopped &&
                    ReferenceEquals(coordinator, activeCoordinator))
                    coordinator = null;
                if (rtcmStopped && ReferenceEquals(rtcmPublisher, activeRtcm))
                    rtcmPublisher = null;
                if (timeStopped && ReferenceEquals(timePublisher, activeTime))
                    timePublisher = null;
                lastConnectionError = null;
                bool complete = tcpStopped && pipeStopped &&
                    coordinatorStopped && disconnected && rtcmStopped &&
                    timeStopped;
                if (complete)
                {
                    if (ReferenceEquals(cancellation, source) || source == null)
                        cancellation = null;
                    if (source != null)
                        source.Dispose();
                    Publish(CreateBaseSnapshot("STOPPED"));
                    log.Info("Native Windows oscillatord runtime stopped safely.");
                }
                else
                {
                    OscillatordSnapshot failed = CreateBaseSnapshot(
                        "STOP_CLEANUP_FAILED");
                    failed.Error = "One or more runtime resources did not stop; " +
                        "restart is inhibited until cleanup succeeds.";
                    Publish(failed);
                }
            }
            if (failures.Count != 0)
                throw new AggregateException(
                    "Native Windows oscillatord cleanup failed.", failures);
        }

        private async Task<bool> TryCleanupAsync(Task cleanup, string message,
            ICollection<Exception> failures)
        {
            try
            {
                await cleanup.ConfigureAwait(false);
                return true;
            }
            catch (OperationCanceledException)
            {
                return true;
            }
            catch (Exception ex)
            {
                log.Error(message, ex);
                failures.Add(ex);
                return false;
            }
        }

        public OscillatordSnapshot HandleRequest(OscillatordRequest request,
            string token, MonitoringOrigin origin)
        {
            OscillatordSnapshot response = CurrentSnapshot();
            /* Report authorization for this caller/transport, not merely the
             * server-wide TCP policy. This makes local Administrator status
             * and remote token status truthful before controls are enabled. */
            response.ControlEnabled = ControlAuthorized(token, origin);
            bool readEeprom = request == OscillatordRequest.ReadEeprom;
            if (request == OscillatordRequest.Status)
                return response;
            CancellationTokenSource activeCancellation = cancellation;
            if (activeCancellation == null ||
                activeCancellation.IsCancellationRequested)
            {
                response.Error = "oscillatord is stopping or is not running.";
                return response;
            }
            if (!readEeprom && !ControlAuthorized(token, origin))
            {
                response.Error = origin == MonitoringOrigin.NamedPipeReadOnly ?
                    "Administrator rights are required for this operation." :
                    "oscillatord control is disabled or the control token is invalid.";
                return response;
            }

            try
            {
                lock (deviceGate)
                {
                    activeCancellation = cancellation;
                    if (activeCancellation == null ||
                        activeCancellation.IsCancellationRequested)
                        throw new InvalidOperationException(
                            "oscillatord is stopping or is not running.");
                    if (client == null || capabilities == null)
                        throw new InvalidOperationException(
                            "No Time Card is currently connected.");
                    response.ActionRequested = ExecuteRequest(request, response);
                }
                response.Error = null;
            }
            catch (Exception ex)
            {
                response.Error = ex.Message;
                log.Warning("Monitoring request " + request + " failed: " +
                    ex.Message);
            }
            return response;
        }

        private async Task CoordinatorAsync(CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                try
                {
                    if (client == null)
                        Connect();
                    lock (deviceGate)
                    {
                        if (discipline != null && !discipline.IsRunning)
                            throw new InvalidOperationException(
                                nativeStatus == null ?
                                "The discipline worker stopped unexpectedly." :
                                nativeStatus.Detail);
                        PublishMonitoringLocked();
                    }
                    lastConnectionError = null;
                    await Task.Delay(TimeSpan.FromSeconds(1), token)
                        .ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (token.IsCancellationRequested)
                {
                    break;
                }
                catch (Exception ex)
                {
                    if (!string.Equals(lastConnectionError, ex.Message,
                        StringComparison.Ordinal))
                    {
                        log.Warning("Time Card unavailable: " + ex.Message);
                        lastConnectionError = ex.Message;
                    }
                    await DisconnectAsync("WAITING_FOR_DEVICE")
                        .ConfigureAwait(false);
                    OscillatordSnapshot waiting = CreateBaseSnapshot(
                        "WAITING_FOR_DEVICE");
                    waiting.Error = ex.Message;
                    Publish(waiting);
                    await Task.Delay(TimeSpan.FromSeconds(
                        configuration.DeviceRetrySeconds), token)
                        .ConfigureAwait(false);
                }
            }
        }

        private void Connect()
        {
            lock (deviceGate)
            {
                if (client != null)
                    return;
                Publish(CreateBaseSnapshot("VALIDATING"));
                TimeCardIdentity identity;
                TimeCardClient candidate = OpenConfiguredCard(out identity);
                try
                {
                    TimeCardCapabilities candidateCapabilities =
                        candidate.GetCapabilities();
                    client = candidate;
                    capabilities = candidateCapabilities;
                    cardSerial = identity != null && identity.IsValid ?
                        identity.SerialNumber : null;
                    nativeStatus = null;
                    if (configuration.Disciplining &&
                        candidateCapabilities.HasDirectMro50 &&
                        candidateCapabilities.HasPairedPhaseMeter)
                    {
                        discipline = new NativeDisciplineEngine(candidate,
                            candidateCapabilities, ReportNativeStatus,
                            configuration.DisciplineOptions);
                        discipline.Start();
                    }
                    else if (candidateCapabilities.HasAtomicUart &&
                             candidateCapabilities.HasHardwareDiscipline)
                    {
                        InitializeAtomicManager(candidate);
                    }
                    PublishMonitoringLocked();
                    log.Info("Connected to " + candidateCapabilities.BoardName +
                        " using ABI " + candidateCapabilities.AbiVersion.ToString(
                            CultureInfo.InvariantCulture) + ".");
                }
                catch
                {
                    client = null;
                    capabilities = null;
                    cardSerial = null;
                    candidate.Dispose();
                    throw;
                }
            }
        }

        private TimeCardClient OpenConfiguredCard(out TimeCardIdentity identity)
        {
            identity = null;
            string selectedPath = TimeCardDeviceSelector.SelectPath(
                TimeCardClient.EnumerateDevicePaths(),
                configuration.DevicePath, configuration.DeviceSerial,
                ProbeCardSerial);
            TimeCardClient selected = selectedPath == null ?
                new TimeCardClient(configuration.DeviceIndex) :
                new TimeCardClient(selectedPath);
            try
            {
                try { identity = selected.GetIdentity(); }
                catch when (string.IsNullOrWhiteSpace(
                    configuration.DeviceSerial)) { }
                if (!string.IsNullOrWhiteSpace(configuration.DeviceSerial))
                {
                    if (identity == null || !identity.IsValid ||
                        !string.Equals(TimeCardDeviceSelector.NormalizeSerial(
                            identity.SerialNumber),
                            TimeCardDeviceSelector.NormalizeSerial(
                                configuration.DeviceSerial),
                            StringComparison.Ordinal))
                        throw new InvalidOperationException(
                            "The selected Time Card no longer matches deviceSerial.");
                }
                return selected;
            }
            catch
            {
                selected.Dispose();
                throw;
            }
        }

        private static string ProbeCardSerial(string devicePath)
        {
            using (TimeCardClient probe = new TimeCardClient(devicePath))
            {
                TimeCardIdentity identity = probe.GetIdentity();
                return identity != null && identity.IsValid ?
                    identity.SerialNumber : null;
            }
        }

        private void InitializeAtomicManager(TimeCardClient candidate)
        {
            bool allowControl = configuration.Disciplining &&
                capabilities.AbiVersion >= 14u;
            if (configuration.Disciplining && !allowControl)
            {
                log.Warning("Atomic-clock discipline requires driver ABI 14 " +
                    "or newer; the UART oscillator will be monitored only.");
            }
            if (allowControl)
            {
                candidate.AcquireDisciplineLease();
                atomicLeaseAcquired = true;
            }

            Exception sa5xFailure = null;
            try
            {
                Sa5xManagerOptions options = new Sa5xManagerOptions
                {
                    ConfigurePhaseLimit = allowControl,
                    ResetTauOnStart = allowControl,
                    EnableDiscipliningWhenSafe = allowControl,
                    EnableLegacyAutoLatch = allowControl
                };
                Sa5xOscillatorManager manager =
                    new Sa5xOscillatorManager(new Sa5xDelegateTransport(
                        (command, timeout) => candidate.ExecuteSa53Command(
                            command, timeout)), options,
                        new Sa5xStopwatchClock());
                Sa5xIdentity identity = manager.Initialize();
                sa5xManager = manager;
                foreach (string warning in identity.Warnings)
                    log.Warning("SA5x: " + warning);
                log.Info("Connected to " + identity.Device + " serial " +
                    (identity.Serial ?? "unknown") + " firmware " +
                    (identity.Firmware ?? "unknown") + ".");
                return;
            }
            catch (Exception ex)
            {
                sa5xFailure = ex;
            }

            try
            {
                Sa3xOscillatorMonitor monitor = new Sa3xOscillatorMonitor(
                    new TimeCardSa3xTransport(candidate),
                    new Sa5xStopwatchClock(), 500u, 5000L);
                sa3xStatus = monitor.Poll();
                sa3xMonitor = monitor;
                if (atomicLeaseAcquired)
                {
                    candidate.ReleaseDisciplineLease();
                    atomicLeaseAcquired = false;
                }
                log.Info("Connected to read-only SA3x atomic oscillator " +
                    "serial " + (sa3xStatus.Serial ?? "unknown") + ".");
            }
            catch (Exception sa3xFailure)
            {
                log.Warning("Atomic UART protocol probe was inconclusive; " +
                    "SA5x: " + sa5xFailure.Message + "; SA3x: " +
                    sa3xFailure.Message + ". The card remains monitor-only.");
                if (atomicLeaseAcquired)
                {
                    try { candidate.ReleaseDisciplineLease(); } catch { }
                    atomicLeaseAcquired = false;
                }
            }
        }

        private async Task DisconnectAsync(string state)
        {
            NativeDisciplineEngine activeDiscipline;
            TimeCardClient activeClient;
            bool releaseAtomicLease;
            lock (deviceGate)
            {
                activeDiscipline = discipline;
                activeClient = client;
                discipline = null;
                client = null;
                capabilities = null;
                cardSerial = null;
                nativeStatus = null;
                sa5xManager = null;
                sa5xStatus = null;
                sa3xMonitor = null;
                sa3xStatus = null;
                releaseAtomicLease = atomicLeaseAcquired;
                atomicLeaseAcquired = false;
            }
            if (activeDiscipline != null)
            {
                try { await activeDiscipline.StopAsync().ConfigureAwait(false); }
                catch (Exception ex)
                {
                    log.Error("Discipline cleanup failed.", ex);
                }
                try { activeDiscipline.Dispose(); }
                catch (Exception ex)
                {
                    log.Error("Discipline disposal failed.", ex);
                }
            }
            if (releaseAtomicLease && activeClient != null)
            {
                try { activeClient.ReleaseDisciplineLease(); } catch { }
            }
            if (activeClient != null)
            {
                try { activeClient.Dispose(); }
                catch (Exception ex)
                {
                    log.Error("Time Card handle cleanup failed.", ex);
                }
            }
            if (timePublisher != null)
                timePublisher.Invalidate();
            if (!string.IsNullOrEmpty(state))
                Publish(CreateBaseSnapshot(state));
        }

        private void ReportNativeStatus(NativeDisciplineStatus value)
        {
            lock (snapshotGate)
                nativeStatus = value;
        }

        private void PublishMonitoringLocked()
        {
            TimeCardSnapshot card = client.GetSnapshot();
            if (sa5xManager != null)
            {
                sa5xManager.PushGnssFix(card.GnssFixOk,
                    card.GnssFixOk ? card.CardTimeUtc : (DateTime?)null);
                sa5xStatus = sa5xManager.Poll();
            }
            else if (sa3xMonitor != null)
            {
                sa3xStatus = sa3xMonitor.Poll();
            }
            Mro50Status mro = null;
            if (capabilities.HasDirectMro50)
                mro = client.GetMro50Status();
            NativeDisciplineStatus currentNative;
            lock (snapshotGate)
                currentNative = nativeStatus;
            if (timePublisher != null)
            {
                bool qualified = IsWindowsTimeQualified(card,
                    currentNative);
                if (qualified)
                    timePublisher.Publish(card, true,
                        currentNative != null ? currentNative.GnssValid :
                            sa5xStatus != null ? sa5xStatus.GnssFixFresh :
                            card.GnssFixOk);
                else
                    timePublisher.Invalidate();
            }
            string mode = discipline != null ? "NATIVE_DISCIPLINE" :
                sa5xManager != null ?
                    (configuration.Disciplining ? "HARDWARE_DISCIPLINE" :
                        "HARDWARE_MONITORING") :
                sa3xMonitor != null ? "HARDWARE_MONITORING" :
                "MONITORING";
            OscillatordSnapshot value = CreateBaseSnapshot(mode);
            value.Board = capabilities.BoardName;
            value.CardSerial = cardSerial;
            value.Clock = new OscillatordClock
            {
                Class = currentNative != null ? currentNative.ClockClass :
                    sa5xStatus != null ? sa5xStatus.ClockClass.ToString()
                        .ToUpperInvariant() :
                    (sa3xStatus != null && sa3xStatus.Locked ? "LOCKED" :
                        "UNCALIBRATED"),
                OffsetNanoseconds = currentNative != null ?
                    currentNative.PhaseNanoseconds :
                    sa5xStatus != null ? sa5xStatus.PhaseNanoseconds : 0
            };
            value.Disciplining = new OscillatordDisciplining
            {
                Status = currentNative != null ? currentNative.State :
                    sa5xStatus != null ? sa5xStatus.State.ToString()
                        .ToUpperInvariant() : mode,
                CurrentConvergenceCount = currentNative != null ?
                    currentNative.ConvergenceCount :
                    sa5xStatus != null ?
                        sa5xStatus.PhaseConvergenceSeconds : -1,
                ConvergenceThreshold = currentNative != null ?
                    currentNative.ConvergenceThreshold :
                    sa5xStatus != null ?
                        sa5xStatus.PhaseConvergenceThresholdSeconds : -1,
                ConvergenceProgress = currentNative != null ?
                    currentNative.ConvergencePercent :
                    sa5xStatus != null ?
                        sa5xStatus.ConvergenceProgressPercent : 0.0,
                ReadyForHoldover = currentNative != null ?
                    currentNative.ReadyForHoldover :
                    sa5xStatus != null && sa5xStatus.HoldoverReady
            };
            value.Oscillator = new OscillatordOscillator
            {
                Model = sa5xStatus != null ? sa5xStatus.Device :
                    sa3xStatus != null ? "Microchip SA3x" :
                    capabilities.OscillatorName,
                FineControl = mro != null ? (long)mro.FineAdjustment :
                    sa5xStatus != null ? (long)sa5xStatus.LastCorrection : 0L,
                CoarseControl = mro != null ? (long)mro.CoarseAdjustment :
                    sa5xStatus != null ? (long)sa5xStatus.Tau : 0L,
                Locked = mro != null ? mro.IsLocked :
                    sa5xStatus != null ? sa5xStatus.OscillatorLocked :
                    sa3xStatus != null && sa3xStatus.Locked,
                TemperatureCelsius = mro != null ?
                    ConvertMroTemperature(mro.TemperatureRaw) :
                    sa5xStatus != null ? sa5xStatus.TemperatureCelsius :
                    sa3xStatus != null ? sa3xStatus.TemperatureCelsius : -999
            };
            value.Gnss = new OscillatordGnss
            {
                Fix = currentNative != null ? currentNative.GnssFixType :
                    card.GnssFixCode,
                FixOk = currentNative != null ? currentNative.GnssValid :
                    card.GnssFixOk,
                Satellites = currentNative != null ?
                    currentNative.GnssSatellites : card.LockedSatellites,
                LeapSeconds = currentNative != null ?
                    currentNative.GnssLeapSeconds :
                    DecodeLeapSeconds(card.Leap),
                LeapSecondChange = currentNative != null ?
                    currentNative.GnssLeapSecondChange : 0,
                AntennaPower = currentNative != null ?
                    currentNative.GnssAntennaPower : 0,
                AntennaStatus = currentNative != null ?
                    currentNative.GnssAntennaStatus : 0,
                SurveyPositionErrorMeters = currentNative != null ?
                    currentNative.GnssSurveyPositionErrorMeters : 0.0,
                TimeAccuracyNanoseconds = currentNative != null ?
                    currentNative.GnssTimeAccuracyNanoseconds : 0
            };
            Publish(value);
        }

        private bool IsWindowsTimeQualified(TimeCardSnapshot card,
            NativeDisciplineStatus currentNative)
        {
            if (card == null || !card.IsClockSynchronized ||
                card.CardTimeUtc.Year < 2020 || card.CardTimeUtc.Year > 2100 ||
                card.SamplingWindowNanoseconds < 0 ||
                card.SamplingWindowNanoseconds > 10000000L)
                return false;
            if (currentNative != null)
            {
                bool qualifiedClass = string.Equals(
                    currentNative.ClockClass, "LOCKED",
                    StringComparison.Ordinal) ||
                    (string.Equals(currentNative.ClockClass, "HOLDOVER",
                        StringComparison.Ordinal) &&
                     currentNative.ReadyForHoldover);
                return currentNative.Running && qualifiedClass &&
                    (currentNative.GnssValid ||
                     currentNative.ReadyForHoldover);
            }
            if (sa5xStatus != null)
            {
                bool qualifiedClass =
                    sa5xStatus.ClockClass == Sa5xClockClass.Locked ||
                    (sa5xStatus.ClockClass == Sa5xClockClass.Holdover &&
                     sa5xStatus.HoldoverReady);
                return sa5xStatus.CommunicationsHealthy &&
                    sa5xStatus.Alarms == 0 && qualifiedClass &&
                    (sa5xStatus.GnssFixFresh || sa5xStatus.HoldoverReady);
            }
            return sa3xStatus != null && sa3xStatus.Locked &&
                card.GnssFixOk;
        }

        private string ExecuteRequest(OscillatordRequest request,
            OscillatordSnapshot response)
        {
            switch (request)
            {
                case OscillatordRequest.Calibration:
                    RequireDiscipline().RequestCalibration();
                    return "calibration requested";
                case OscillatordRequest.GnssStart:
                    SendGnssReset(0x0001, 0x09);
                    return "GNSS start requested";
                case OscillatordRequest.GnssStop:
                    SendGnssReset(0x0001, 0x08);
                    return "GNSS stop requested";
                case OscillatordRequest.GnssSoftReset:
                    SendGnssReset(0x0001, 0x01);
                    return "GNSS software reset requested";
                case OscillatordRequest.GnssHardReset:
                    SendGnssReset(0x0001, 0x04);
                    return "GNSS hardware reset requested";
                case OscillatordRequest.GnssColdReset:
                    SendGnssReset(0xffff, 0x02);
                    return "GNSS cold reset requested";
                case OscillatordRequest.ReadEeprom:
                    TimeCardDisciplineParameters parameters =
                        client.ReadDisciplineParameters();
                    response.Eeprom = new OscillatordEeprom
                    {
                        Present = parameters.IsPresent,
                        Valid = parameters.IsValid,
                        Length = parameters.Data == null ? 0 :
                            parameters.Data.Length,
                        Sha256 = parameters.Data == null ? null :
                            Sha256(parameters.Data),
                        DataBase64 = parameters.Data == null ? null :
                            Convert.ToBase64String(parameters.Data)
                    };
                    return "EEPROM metadata read";
                case OscillatordRequest.SaveEeprom:
                    RequireDiscipline().SaveNow();
                    return "discipline parameters saved";
                case OscillatordRequest.FakeHoldoverStart:
                    RequireDiscipline().SetFakeHoldover(true);
                    return "simulated holdover started";
                case OscillatordRequest.FakeHoldoverStop:
                    RequireDiscipline().SetFakeHoldover(false);
                    return "simulated holdover stopped";
                case OscillatordRequest.MroCoarseIncrement:
                    AdjustCoarse(1);
                    return "mRO-50 coarse control incremented";
                case OscillatordRequest.MroCoarseDecrement:
                    AdjustCoarse(-1);
                    return "mRO-50 coarse control decremented";
                case OscillatordRequest.ResetUbloxSerial:
                    client.ConfigureUart(0u, configuration.GnssBaud);
                    return "u-blox serial port reset";
                default:
                    throw new InvalidOperationException(
                        "Unknown monitoring request.");
            }
        }

        private NativeDisciplineEngine RequireDiscipline()
        {
            if (discipline == null || !discipline.IsRunning)
                throw new NotSupportedException(
                    "This card is not running native mRO-50 discipline.");
            return discipline;
        }

        private void AdjustCoarse(int delta)
        {
            if (discipline != null && discipline.IsRunning)
            {
                discipline.RequestCoarseAdjustment(delta);
                return;
            }
            if (!capabilities.HasDirectMro50)
                throw new NotSupportedException(
                    "This card does not expose direct mRO-50 control.");
            Mro50Status mro = client.GetMro50Status();
            long next = (long)mro.CoarseAdjustment + delta;
            if (next < capabilities.CoarseMinimum ||
                next > capabilities.CoarseMaximum)
                throw new InvalidOperationException(
                    "The requested coarse value is outside the driver limits.");
            client.SetMro50CoarseAdjustment((uint)next);
        }

        private void SendGnssReset(ushort backupMask, byte resetMode)
        {
            if (!capabilities.HasGnssUart)
                throw new NotSupportedException(
                    "This card does not expose a GNSS UART.");
            byte[] payload = { (byte)(backupMask & 0xff),
                (byte)(backupMask >> 8), resetMode, 0 };
            byte[] frame = BuildUbxFrame(0x06, 0x04, payload);
            UartWriteResult result = client.WriteUart(0u, frame, 1500u);
            if (result.BytesTransferred != (uint)frame.Length)
                throw new InvalidOperationException(
                    "The complete UBX-CFG-RST frame was not transmitted.");
        }

        private bool ControlAuthorized(string token, MonitoringOrigin origin)
        {
            if (origin == MonitoringOrigin.NamedPipeAdministrator)
                return true;
            if (origin == MonitoringOrigin.NamedPipeReadOnly ||
                !configuration.MonitoringAllowControl)
                return false;
            return FixedTimeEquals(token ?? string.Empty,
                configuration.MonitoringControlToken ?? string.Empty);
        }

        private static bool FixedTimeEquals(string left, string right)
        {
            byte[] a = Encoding.UTF8.GetBytes(left);
            byte[] b = Encoding.UTF8.GetBytes(right);
            int difference = a.Length ^ b.Length;
            int length = Math.Max(a.Length, b.Length);
            for (int index = 0; index < length; ++index)
            {
                byte av = index < a.Length ? a[index] : (byte)0;
                byte bv = index < b.Length ? b[index] : (byte)0;
                difference |= av ^ bv;
            }
            return difference == 0;
        }

        private void Publish(OscillatordSnapshot value)
        {
            value.UpdatedUtc = DateTime.UtcNow.ToString("O",
                CultureInfo.InvariantCulture);
            lock (snapshotGate)
                snapshot = value;
        }

        private OscillatordSnapshot CurrentSnapshot()
        {
            lock (snapshotGate)
                return Clone(snapshot);
        }

        private OscillatordSnapshot CreateBaseSnapshot(string state)
        {
            return new OscillatordSnapshot
            {
                Service = "oscillatord-windows",
                Version = ServiceVersion,
                ProtocolVersion = 1,
                ControlEnabled = configuration != null &&
                    configuration.MonitoringAllowControl,
                ServiceState = state,
                UpdatedUtc = DateTime.UtcNow.ToString("O",
                    CultureInfo.InvariantCulture)
            };
        }

        private static OscillatordSnapshot Clone(OscillatordSnapshot value)
        {
            return new OscillatordSnapshot
            {
                Service = value.Service,
                Version = value.Version,
                ProtocolVersion = value.ProtocolVersion,
                ControlEnabled = value.ControlEnabled,
                Error = value.Error,
                ActionRequested = value.ActionRequested,
                ServiceState = value.ServiceState,
                Board = value.Board,
                CardSerial = value.CardSerial,
                UpdatedUtc = value.UpdatedUtc,
                Clock = value.Clock == null ? null : new OscillatordClock
                {
                    Class = value.Clock.Class,
                    OffsetNanoseconds = value.Clock.OffsetNanoseconds
                },
                Disciplining = value.Disciplining == null ? null :
                    new OscillatordDisciplining
                    {
                        Status = value.Disciplining.Status,
                        CurrentConvergenceCount =
                            value.Disciplining.CurrentConvergenceCount,
                        ConvergenceThreshold =
                            value.Disciplining.ConvergenceThreshold,
                        ConvergenceProgress =
                            value.Disciplining.ConvergenceProgress,
                        ReadyForHoldover =
                            value.Disciplining.ReadyForHoldover
                    },
                Oscillator = value.Oscillator == null ? null :
                    new OscillatordOscillator
                    {
                        Model = value.Oscillator.Model,
                        FineControl = value.Oscillator.FineControl,
                        CoarseControl = value.Oscillator.CoarseControl,
                        Locked = value.Oscillator.Locked,
                        TemperatureCelsius =
                            value.Oscillator.TemperatureCelsius
                    },
                Gnss = value.Gnss == null ? null : new OscillatordGnss
                {
                    Fix = value.Gnss.Fix,
                    FixOk = value.Gnss.FixOk,
                    AntennaPower = value.Gnss.AntennaPower,
                    AntennaStatus = value.Gnss.AntennaStatus,
                    LeapSecondChange = value.Gnss.LeapSecondChange,
                    LeapSeconds = value.Gnss.LeapSeconds,
                    Satellites = value.Gnss.Satellites,
                    SurveyPositionErrorMeters =
                        value.Gnss.SurveyPositionErrorMeters,
                    TimeAccuracyNanoseconds =
                        value.Gnss.TimeAccuracyNanoseconds
                },
                Eeprom = value.Eeprom == null ? null : new OscillatordEeprom
                {
                    Present = value.Eeprom.Present,
                    Valid = value.Eeprom.Valid,
                    Length = value.Eeprom.Length,
                    Sha256 = value.Eeprom.Sha256,
                    DataBase64 = value.Eeprom.DataBase64
                }
            };
        }

        private static byte[] BuildUbxFrame(byte messageClass,
            byte messageId, byte[] payload)
        {
            byte[] frame = new byte[payload.Length + 8];
            frame[0] = 0xb5;
            frame[1] = 0x62;
            frame[2] = messageClass;
            frame[3] = messageId;
            frame[4] = (byte)(payload.Length & 0xff);
            frame[5] = (byte)(payload.Length >> 8);
            Buffer.BlockCopy(payload, 0, frame, 6, payload.Length);
            byte a = 0;
            byte b = 0;
            for (int index = 2; index < frame.Length - 2; ++index)
            {
                unchecked
                {
                    a += frame[index];
                    b += a;
                }
            }
            frame[frame.Length - 2] = a;
            frame[frame.Length - 1] = b;
            return frame;
        }

        private static string Sha256(byte[] value)
        {
            using (SHA256 hash = SHA256.Create())
            {
                byte[] digest = hash.ComputeHash(value);
                StringBuilder text = new StringBuilder(digest.Length * 2);
                foreach (byte item in digest)
                    text.Append(item.ToString("x2", CultureInfo.InvariantCulture));
                return text.ToString();
            }
        }

        private static double ConvertMroTemperature(uint raw)
        {
            if (raw == 0 || raw >= 4095)
                return -999;
            double ratio = raw / 4095.0;
            double resistance = 47000.0 * ratio / (1.0 - ratio);
            return 4100.0 * 298.15 /
                (298.15 * Math.Log(0.00001 * resistance) + 4100.0) - 273.14;
        }

        private static int DecodeLeapSeconds(uint leap)
        {
            int value = unchecked((int)leap);
            return value >= -128 && value <= 128 ? value : 0;
        }

        public void Dispose()
        {
            StopAsync().GetAwaiter().GetResult();
        }
    }
}
