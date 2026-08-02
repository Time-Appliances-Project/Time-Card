using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace TimeCardControlCenter
{
    internal enum DisciplineAction : uint
    {
        None = 0,
        PhaseJump = 1,
        AdjustFine = 2,
        AdjustCoarse = 3,
        Calibrate = 4,
        SaveParameters = 5
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeDisciplineInput
    {
        public double Temperature;
        public long PhaseErrorNanoseconds;
        public uint FineSetpoint;
        public int CoarseSetpoint;
        public int QuantizationErrorPicoseconds;
        public uint Flags;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeDisciplineOutput
    {
        public uint Action;
        public uint Setpoint;
        public int PhaseJumpNanoseconds;
        public uint State;
        public uint ClockClass;
        public int ConvergenceCount;
        public int ConvergenceThreshold;
        public float ConvergencePercent;
        public uint ReadyForHoldover;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct NativeDisciplineConfiguration
    {
        public uint Size;
        public int ReferenceFluctuationsNanoseconds;
        public int PhaseJumpThresholdNanoseconds;
        public int PhaseResolutionNanoseconds;
        public int Debug;
        public int ReactivityMinimum;
        public int ReactivityMaximum;
        public int ReactivityPower;
        public int CalibrationSamples;
        public int FineStopTolerance;
        public int MaximumAllowedCoarse;
        public uint Flags;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
        public uint[] Reserved;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 220)]
        public string FineTableOutputPath;
    }

    internal sealed class NativeDisciplineOptions
    {
        public NativeDisciplineOptions()
        {
            ReferenceFluctuationsNanoseconds = 30;
            PhaseJumpThresholdNanoseconds = 300;
            PhaseResolutionNanoseconds = 5;
            ReactivityMinimum = 10;
            ReactivityMaximum = 30;
            ReactivityPower = 2;
            CalibrationSamples = 50;
            FineStopTolerance = 100;
            MaximumAllowedCoarse = 20;
            OscillatorFactorySettings = true;
            ParameterSaveInterval = TimeSpan.FromHours(1);
            FineTableOutputPath = string.Empty;
            GnssBaud = 115200u;
            InitializePhcFromGnss = true;
            StartupAlignmentTimeout = TimeSpan.FromMinutes(5);
            StartupAlignmentSettling = TimeSpan.FromSeconds(6);
            GnssPreferredTimeScale = "GPS";
        }

        public int ReferenceFluctuationsNanoseconds { get; set; }
        public int PhaseJumpThresholdNanoseconds { get; set; }
        public int PhaseResolutionNanoseconds { get; set; }
        public int Debug { get; set; }
        public int ReactivityMinimum { get; set; }
        public int ReactivityMaximum { get; set; }
        public int ReactivityPower { get; set; }
        public int CalibrationSamples { get; set; }
        public int FineStopTolerance { get; set; }
        public int MaximumAllowedCoarse { get; set; }
        public bool CalibrateFirst { get; set; }
        public bool OscillatorFactorySettings { get; set; }
        public bool LearnTemperatureTable { get; set; }
        public bool UseTemperatureTable { get; set; }
        public bool BypassSurvey { get; set; }
        public bool OppositePhaseError { get; set; }
        public bool TrackingOnly { get; set; }
        public TimeSpan ParameterSaveInterval { get; set; }
        public string FineTableOutputPath { get; set; }
        public uint GnssBaud { get; set; }
        public bool InitializePhcFromGnss { get; set; }
        public TimeSpan StartupAlignmentTimeout { get; set; }
        public TimeSpan StartupAlignmentSettling { get; set; }
        public bool GnssReceiverReconfigure { get; set; }
        public bool GnssPersistConfiguration { get; set; }
        public string GnssPreferredTimeScale { get; set; }
        public int GnssCableDelayNanoseconds { get; set; }
        public bool GnssRtcmEnabled { get; set; }
        internal Action<byte[]> GnssRawObserver { get; set; }

        internal NativeDisciplineConfiguration ToNative()
        {
            string tablePath = FineTableOutputPath ?? string.Empty;
            if (Encoding.Default.GetByteCount(tablePath) >= 220)
                throw new InvalidOperationException(
                    "The miniCOD temperature-table path is too long.");
            uint flags = (CalibrateFirst ? 1u : 0u) |
                (OscillatorFactorySettings ? 2u : 0u) |
                (LearnTemperatureTable ? 4u : 0u) |
                (UseTemperatureTable ? 8u : 0u);
            return new NativeDisciplineConfiguration
            {
                Size = (uint)Marshal.SizeOf(
                    typeof(NativeDisciplineConfiguration)),
                ReferenceFluctuationsNanoseconds =
                    ReferenceFluctuationsNanoseconds,
                PhaseJumpThresholdNanoseconds =
                    PhaseJumpThresholdNanoseconds,
                PhaseResolutionNanoseconds = PhaseResolutionNanoseconds,
                Debug = Debug,
                ReactivityMinimum = ReactivityMinimum,
                ReactivityMaximum = ReactivityMaximum,
                ReactivityPower = ReactivityPower,
                CalibrationSamples = CalibrationSamples,
                FineStopTolerance = FineStopTolerance,
                MaximumAllowedCoarse = MaximumAllowedCoarse,
                Flags = flags,
                Reserved = new uint[4],
                FineTableOutputPath = tablePath
            };
        }
    }

    internal sealed class NativeDisciplineAlgorithm : IDisposable
    {
        private IntPtr context;

        private NativeDisciplineAlgorithm(IntPtr value)
        {
            context = value;
        }

        public static NativeDisciplineAlgorithm Create(uint factoryCoarse,
                                                        byte[] savedParameters)
        {
            return Create(factoryCoarse, savedParameters,
                new NativeDisciplineOptions());
        }

        public static NativeDisciplineAlgorithm Create(uint factoryCoarse,
            byte[] savedParameters, NativeDisciplineOptions options)
        {
            if (options == null)
                throw new ArgumentNullException("options");
            StringBuilder error = new StringBuilder(1024);
            NativeDisciplineConfiguration configuration = options.ToNative();
            IntPtr value = NativeMethods.CreateConfigured(factoryCoarse,
                savedParameters,
                savedParameters == null ? 0u : (uint)savedParameters.Length,
                ref configuration, error, (uint)error.Capacity);
            if (value == IntPtr.Zero)
                throw new InvalidOperationException(
                    "The miniCOD disciplining engine could not start. " + error);
            return new NativeDisciplineAlgorithm(value);
        }

        public NativeDisciplineOutput Process(NativeDisciplineInput input)
        {
            EnsureOpen();
            NativeDisciplineOutput output;
            int result = NativeMethods.Process(context, ref input, out output);
            if (result != 0)
                throw new InvalidOperationException(string.Format(
                    CultureInfo.InvariantCulture,
                    "miniCOD processing failed with error {0}.", result));
            return output;
        }

        public CalibrationPlan GetCalibrationPlan()
        {
            EnsureOpen();
            ushort[] points = new ushort[10];
            uint count;
            uint samples;
            int result = NativeMethods.GetCalibrationPlan(context, points,
                (uint)points.Length, out count, out samples);
            if (result != 0 || count == 0 || count > (uint)points.Length ||
                samples == 0 || samples > 1000)
                throw new InvalidOperationException(string.Format(
                    CultureInfo.InvariantCulture,
                    "miniCOD returned an invalid calibration plan ({0}).", result));
            Array.Resize(ref points, (int)count);
            return new CalibrationPlan(points, (int)samples);
        }

        public void CompleteCalibration(float[] samples)
        {
            EnsureOpen();
            int result = NativeMethods.CompleteCalibration(context, samples,
                (uint)samples.Length);
            if (result != 0)
                throw new InvalidOperationException(string.Format(
                    CultureInfo.InvariantCulture,
                    "miniCOD rejected the calibration samples ({0}).", result));
        }

        public byte[] GetParameters()
        {
            EnsureOpen();
            uint size = NativeMethods.ParametersSize();
            if (size == 0 || size > 4096)
                throw new InvalidOperationException(
                    "miniCOD reported an invalid parameter size.");
            byte[] bytes = new byte[checked((int)size)];
            int result = NativeMethods.GetParameters(context, bytes, size);
            if (result != 0)
                throw new InvalidOperationException(string.Format(
                    CultureInfo.InvariantCulture,
                    "miniCOD parameter export failed ({0}).", result));
            return bytes;
        }

        public void Dispose()
        {
            if (context != IntPtr.Zero)
            {
                NativeMethods.Destroy(context);
                context = IntPtr.Zero;
            }
        }

        private void EnsureOpen()
        {
            if (context == IntPtr.Zero)
                throw new ObjectDisposedException("NativeDisciplineAlgorithm");
        }

        internal sealed class CalibrationPlan
        {
            public CalibrationPlan(ushort[] points, int samplesPerPoint)
            {
                Points = points;
                SamplesPerPoint = samplesPerPoint;
            }

            public ushort[] Points { get; private set; }
            public int SamplesPerPoint { get; private set; }
        }

        private static class NativeMethods
        {
            private const string Library = "TimeCardDiscipline.dll";

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
                EntryPoint = "tcod_create", CharSet = CharSet.Ansi)]
            internal static extern IntPtr Create(uint factoryCoarse,
                byte[] savedParameters, uint savedParametersLength,
                StringBuilder error, uint errorLength);

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
                EntryPoint = "tcod_create_configured", CharSet = CharSet.Ansi)]
            internal static extern IntPtr CreateConfigured(uint factoryCoarse,
                byte[] savedParameters, uint savedParametersLength,
                ref NativeDisciplineConfiguration configuration,
                StringBuilder error, uint errorLength);

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
                EntryPoint = "tcod_process")]
            internal static extern int Process(IntPtr context,
                ref NativeDisciplineInput input,
                out NativeDisciplineOutput output);

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
                EntryPoint = "tcod_destroy")]
            internal static extern void Destroy(IntPtr context);

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
                EntryPoint = "tcod_get_calibration_plan")]
            internal static extern int GetCalibrationPlan(IntPtr context,
                [Out] ushort[] points, uint capacity, out uint pointCount,
                out uint samplesPerPoint);

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
                EntryPoint = "tcod_complete_calibration")]
            internal static extern int CompleteCalibration(IntPtr context,
                float[] phaseSamples, uint sampleCount);

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
                EntryPoint = "tcod_parameters_size")]
            internal static extern uint ParametersSize();

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
                EntryPoint = "tcod_get_parameters")]
            internal static extern int GetParameters(IntPtr context,
                [Out] byte[] buffer, uint bufferLength);
        }
    }

    internal sealed class NativeDisciplineStatus
    {
        public string State { get; set; }
        public string ClockClass { get; set; }
        public string Detail { get; set; }
        public string LastAction { get; set; }
        public long PhaseNanoseconds { get; set; }
        public uint Fine { get; set; }
        public uint Coarse { get; set; }
        public double TemperatureCelsius { get; set; }
        public bool GnssValid { get; set; }
        public bool OscillatorLocked { get; set; }
        public bool ReadyForHoldover { get; set; }
        public float ConvergencePercent { get; set; }
        public int ConvergenceCount { get; set; }
        public int ConvergenceThreshold { get; set; }
        public bool Running { get; set; }
        public bool Calibrating { get; set; }
        public int GnssFixType { get; set; }
        public int GnssSatellites { get; set; }
        public int GnssLeapSeconds { get; set; }
        public int GnssLeapSecondChange { get; set; }
        public int GnssAntennaPower { get; set; }
        public int GnssAntennaStatus { get; set; }
        public double GnssSurveyPositionErrorMeters { get; set; }
        public uint GnssTimeAccuracyNanoseconds { get; set; }
    }

    internal sealed class NativeDisciplineEngine : IDisposable
    {
        private readonly TimeCardClient client;
        private readonly TimeCardCapabilities capabilities;
        private readonly Action<NativeDisciplineStatus> report;
        private readonly NativeDisciplineOptions options;
        private readonly string cardKey;
        private NativeDisciplineAlgorithm algorithm;
        private CancellationTokenSource cancellation;
        private Task worker;
        private volatile bool fakeHoldover;
        private int calibrationRequested;
        private uint lastReferenceCounter;
        private uint lastOscillatorCounter;
        private bool surveyCompleted;
        private byte[] lastCardParameters;
        private byte[] lastHostParameters;
        private bool leaseAcquired;
        private long lastParameterSaveTimestamp;
        private bool ignoreNextPhaseSample;
        private NativeGnssSessionManager gnssMonitor;
        private ulong lastTimePulseSequence;
        private int saveRequested;
        private int coarseAdjustmentRequested;

        public NativeDisciplineEngine(TimeCardClient activeClient,
            TimeCardCapabilities activeCapabilities,
            Action<NativeDisciplineStatus> statusReport,
            bool allowSurveyBypass)
            : this(activeClient, activeCapabilities, statusReport,
                new NativeDisciplineOptions
                {
                    BypassSurvey = allowSurveyBypass
                })
        {
        }

        public NativeDisciplineEngine(TimeCardClient activeClient,
            TimeCardCapabilities activeCapabilities,
            Action<NativeDisciplineStatus> statusReport,
            NativeDisciplineOptions disciplineOptions)
        {
            client = activeClient ?? throw new ArgumentNullException(
                "activeClient");
            capabilities = activeCapabilities ?? throw new ArgumentNullException(
                "activeCapabilities");
            report = statusReport ?? throw new ArgumentNullException(
                "statusReport");
            options = disciplineOptions ?? throw new ArgumentNullException(
                "disciplineOptions");
            cardKey = ResolveCardKey();
            options.FineTableOutputPath = Path.Combine(
                Environment.GetFolderPath(
                    Environment.SpecialFolder.CommonApplicationData),
                "OCP Time Card", "Oscillatord", "Cards", cardKey);
            Directory.CreateDirectory(options.FineTableOutputPath);
        }

        public bool IsRunning
        {
            get { return worker != null && !worker.IsCompleted; }
        }
        public bool FakeHoldover { get { return fakeHoldover; } }

        public void Start()
        {
            if (IsRunning)
                return;
            if (!capabilities.HasDirectMro50 ||
                !capabilities.HasPairedPhaseMeter)
                throw new NotSupportedException(
                    "Native miniCOD software discipline requires the Orolia ART mRO-50 and paired PPS phase meter. SA53 cards use their hardware discipline on the Atomic Clock workspace.");
            if (capabilities.AbiVersion < 14u)
                throw new NotSupportedException(
                    "Native service discipline requires Time Card driver ABI 14 or newer for exclusive ownership and crash cleanup.");
            if (worker != null && worker.IsCompleted)
            {
                if (cancellation != null)
                    cancellation.Dispose();
                cancellation = null;
                worker = null;
                if (algorithm != null)
                    algorithm.Dispose();
                algorithm = null;
            }
            try
            {
                client.AcquireDisciplineLease();
                leaseAcquired = true;
            }
            catch (Exception ex)
            {
                throw new InvalidOperationException(
                    "Another process owns oscillator discipline, or the installed driver does not support safe ownership.", ex);
            }
            try
            {
                Mro50Status oscillator = client.GetMro50Status();
                if (!oscillator.IsFineValid || !oscillator.IsCoarseValid)
                    throw new InvalidOperationException(
                        "The mRO-50 fine and coarse controls are not readable; discipline was not started.");
                byte[] saved = LoadParameters();
                algorithm = NativeDisciplineAlgorithm.Create(
                    oscillator.CoarseAdjustment, saved, options);
                client.SetPhaseMeter(true, false, false);
                cancellation = new CancellationTokenSource();
                worker = Task.Run(() => RunGuardedAsync(cancellation.Token));
                lastParameterSaveTimestamp = Stopwatch.GetTimestamp();
            }
            catch
            {
                try { client.SetPhaseMeter(false, false, false); } catch { }
                if (cancellation != null)
                    cancellation.Dispose();
                cancellation = null;
                if (algorithm != null)
                    algorithm.Dispose();
                algorithm = null;
                worker = null;
                ReleaseLease();
                throw;
            }
        }

        public async Task StopAsync()
        {
            Task active = worker;
            if (active == null)
                return;
            cancellation.Cancel();
            try { await active.ConfigureAwait(false); }
            catch (OperationCanceledException) { }
            finally
            {
                try { SaveParameters(); } catch { }
                try { client.SetPhaseMeter(false, false, false); } catch { }
                if (algorithm != null)
                    algorithm.Dispose();
                algorithm = null;
                cancellation.Dispose();
                cancellation = null;
                worker = null;
                ReleaseLease();
                report(new NativeDisciplineStatus
                {
                    State = "STOPPED",
                    ClockClass = "UNCALIBRATED",
                    Detail = "Native discipline is stopped and phase capture is disabled.",
                    LastAction = "Stopped safely",
                    Running = false
                });
            }
        }

        public void SetFakeHoldover(bool enabled)
        {
            fakeHoldover = enabled;
        }

        public void RequestCalibration()
        {
            if (!IsRunning)
                throw new InvalidOperationException(
                    "Start native discipline before requesting calibration.");
            if (options.TrackingOnly)
                throw new InvalidOperationException(
                    "Calibration is disabled while tracking_only is enabled.");
            Interlocked.Exchange(ref calibrationRequested, 1);
        }

        public void SaveNow()
        {
            if (!IsRunning)
                throw new InvalidOperationException(
                    "Start native discipline before saving its parameters.");
            Interlocked.Exchange(ref saveRequested, 1);
        }

        public void RequestCoarseAdjustment(int delta)
        {
            if (!IsRunning)
                throw new InvalidOperationException(
                    "Start native discipline before adjusting coarse control.");
            if (delta != -1 && delta != 1)
                throw new ArgumentOutOfRangeException("delta");
            Interlocked.Add(ref coarseAdjustmentRequested, delta);
        }

        private async Task RunAsync(CancellationToken token)
        {
            gnssMonitor = new NativeGnssSessionManager(client, 0u,
                options.GnssBaud, options.GnssRawObserver);
            // The service owns the ABI-14 discipline lease here, so UART
            // setup and an explicitly requested receiver profile are safe,
            // serialized mutations. Passive monitoring never reaches this
            // path without the lease.
            if (options.GnssReceiverReconfigure)
            {
                await gnssMonitor.DetectReceiverBaudAsync(true, token)
                    .ConfigureAwait(false);
                await gnssMonitor.ApplyConfigurationAndAwaitAcknowledgementAsync(
                    UbxProtocol.BuildOscillatordConfiguration(
                        options.GnssBaud, options.GnssPreferredTimeScale,
                        options.GnssCableDelayNanoseconds,
                        options.GnssRtcmEnabled),
                    options.GnssPersistConfiguration, true, token)
                    .ConfigureAwait(false);
                // CFG-VALSET can change UART1 from the detected rate to the
                // configured target after its acknowledgement.
                gnssMonitor.ConfigureUart(true);
            }
            else
                gnssMonitor.ConfigureUart(true);
            CancellationTokenSource gnssCancellation =
                CancellationTokenSource.CreateLinkedTokenSource(token);
            Task gnssWorker = Task.Run(() =>
                gnssMonitor.RunPassiveAsync(gnssCancellation.Token));
            try
            {
                if (options.InitializePhcFromGnss)
                    await AlignPhcFromGnssAsync(token).ConfigureAwait(false);
                while (!token.IsCancellationRequested)
                {
                    if (gnssWorker.IsFaulted)
                        await gnssWorker.ConfigureAwait(false);
                    TimeCardPhaseSample phase = client.GetPhaseSample();
                    if (phase.OscillatorCounter == lastOscillatorCounter)
                    {
                        await Task.Delay(20, token).ConfigureAwait(false);
                        continue;
                    }
                    bool referenceFresh = phase.ReferenceCounter !=
                        lastReferenceCounter;
                    lastReferenceCounter = phase.ReferenceCounter;
                    lastOscillatorCounter = phase.OscillatorCounter;
                    // Pair a new hardware phase event with the next
                    // unconsumed GNSS pulse. Polling an old TIM-TP before the
                    // counter changes would otherwise feed epoch n-1 into
                    // phase event n.
                    NativeGnssEpoch gnss = await WaitForGnssEpochAsync(
                        lastTimePulseSequence, token).ConfigureAwait(false);
                    Mro50Status oscillator = client.GetMro50Status();
                    int coarseDelta = Interlocked.Exchange(
                        ref coarseAdjustmentRequested, 0);
                    if (coarseDelta != 0)
                    {
                        long requested = (long)oscillator.CoarseAdjustment +
                            coarseDelta;
                        if (requested < capabilities.CoarseMinimum ||
                            requested > capabilities.CoarseMaximum)
                            throw new InvalidOperationException(
                                "The requested coarse adjustment is outside the driver limits.");
                        client.SetMro50CoarseAdjustment((uint)requested);
                        oscillator = client.GetMro50Status();
                    }
                    bool referenceValid = phase.IsReferenceValid &&
                        referenceFresh && phase.ReferenceError == 0;
                    bool oscillatorValid = phase.IsOscillatorValid &&
                        phase.OscillatorError == 0;
                    bool phaseValid = phase.IsPhaseValid && referenceValid &&
                        oscillatorValid;
                    if (ignoreNextPhaseSample && phaseValid)
                    {
                        ignoreNextPhaseSample = false;
                        await Task.Delay(20, token).ConfigureAwait(false);
                        continue;
                    }
                    bool newTimePulse = gnss.HasTimePulse &&
                        gnss.TimePulseSequence != lastTimePulseSequence;
                    bool gnssValid = newTimePulse && gnss.FixValid &&
                        !fakeHoldover;
                    bool surveyValid = surveyCompleted || options.BypassSurvey;
                    if (newTimePulse)
                        lastTimePulseSequence = gnss.TimePulseSequence;
                    bool calibrate = Interlocked.Exchange(
                        ref calibrationRequested, 0) != 0;
                    NativeDisciplineInput input = new NativeDisciplineInput
                    {
                        Temperature = ConvertTemperature(
                            oscillator.TemperatureRaw),
                        PhaseErrorNanoseconds = phaseValid ?
                            (options.OppositePhaseError ?
                                -phase.PhaseNanoseconds :
                                phase.PhaseNanoseconds) : 0,
                        FineSetpoint = oscillator.FineAdjustment,
                        CoarseSetpoint = checked((int)
                            oscillator.CoarseAdjustment),
                        // TIM-TP describes the next pulse; upstream uses qErr(n-1).
                        QuantizationErrorPicoseconds = newTimePulse ?
                            gnss.QuantizationErrorPicoseconds : 0,
                        Flags = (gnssValid ? 1u : 0u) |
                            (oscillator.IsLocked ? 2u : 0u) |
                            (surveyValid ? 4u : 0u) |
                            (calibrate ? 8u : 0u) |
                            (phaseValid ? 16u : 0u) |
                            (referenceValid ? 32u : 0u) |
                            (oscillatorValid ? 64u : 0u) |
                            ((phase.ReferenceError != 0 ||
                              phase.OscillatorError != 0) ? 128u : 0u)
                    };
                    NativeDisciplineOutput output = algorithm.Process(input);
                    string action = ApplyOutput(output, oscillator);
                    if ((DisciplineAction)output.Action ==
                        DisciplineAction.Calibrate)
                    {
                        await CalibrateAsync(token).ConfigureAwait(false);
                        action = "Calibration completed and saved";
                    }
                    if ((DisciplineAction)output.Action ==
                        DisciplineAction.SaveParameters)
                        SaveParameters();
                    if (Interlocked.Exchange(ref saveRequested, 0) != 0)
                    {
                        SaveParameters();
                        lastParameterSaveTimestamp = Stopwatch.GetTimestamp();
                    }
                    if (options.ParameterSaveInterval > TimeSpan.Zero &&
                        HasElapsed(lastParameterSaveTimestamp,
                            options.ParameterSaveInterval))
                    {
                        SaveParameters();
                        lastParameterSaveTimestamp = Stopwatch.GetTimestamp();
                    }
                    report(CreateStatus(output, input, action,
                        GnssReferenceDetail(gnss), gnss));
                }
            }
            finally
            {
                gnssCancellation.Cancel();
                try { await gnssWorker.ConfigureAwait(false); }
                catch (OperationCanceledException) when (
                    gnssCancellation.IsCancellationRequested)
                {
                }
                gnssCancellation.Dispose();
                gnssMonitor = null;
            }
        }

        private async Task AlignPhcFromGnssAsync(CancellationToken token)
        {
            PhcStartupAlignmentPlanner planner = new
                PhcStartupAlignmentPlanner(options.OppositePhaseError ? -1 : 1,
                    checked((long)options.StartupAlignmentSettling.TotalMilliseconds));
            long started = Stopwatch.GetTimestamp();
            while (!token.IsCancellationRequested)
            {
                if (HasElapsed(started, options.StartupAlignmentTimeout))
                    throw new TimeoutException(
                        "Timed out waiting for coherent UBX NAV-PVT, NAV-TIMELS, TIM-TP, and paired PPS data during PHC startup alignment.");

                NativeGnssSessionSnapshot gnss = gnssMonitor.Snapshot();
                TimeCardPhaseSample phase = client.GetPhaseSample();
                long now = StopwatchMilliseconds();
                long age = GnssAgeMilliseconds(gnss);
                bool hasPhc = false;
                DateTime phc = default(DateTime);
                if (planner.State == PhcStartupState.Verifying)
                {
                    try
                    {
                        phc = client.GetEstimatedClockTimeUtc();
                        hasPhc = true;
                    }
                    catch
                    {
                        hasPhc = false;
                    }
                }
                PhcStartupRecommendation recommendation = planner.Observe(
                    new PhcStartupObservation
                    {
                        Gnss = gnss,
                        HasPairedPhase = phase.IsPhaseValid &&
                            phase.IsReferenceValid && phase.IsOscillatorValid &&
                            phase.ReferenceError == 0 &&
                            phase.OscillatorError == 0,
                        PhaseErrorNanoseconds = phase.PhaseNanoseconds,
                        HasPhcUtc = hasPhc,
                        PhcUtc = phc,
                        MonotonicMilliseconds = now,
                        GnssAgeMilliseconds = age,
                        ReferenceCounter = phase.ReferenceCounter
                    });

                report(new NativeDisciplineStatus
                {
                    State = "STARTUP",
                    ClockClass = "UNCALIBRATED",
                    Detail = recommendation.Reason,
                    LastAction = "Native GNSS/PHC alignment",
                    Running = true
                });

                if (recommendation.Action ==
                    PhcStartupActionKind.SetPhcUtcAtNextPulse)
                {
                    TimeCardPhaseSample edge =
                        await WaitForReferenceEdgeAsync(
                            recommendation.SourceReferenceCounter, token)
                            .ConfigureAwait(false);
                    try
                    {
                        uint nanoseconds = checked((uint)
                            recommendation.TargetNanoseconds);
                        if (recommendation.PreservePhcNanoseconds)
                        {
                            DateTime phcAtEdge =
                                client.GetEstimatedClockTimeUtc();
                            long subsecondTicks = phcAtEdge.Ticks %
                                TimeSpan.TicksPerSecond;
                            nanoseconds = checked((uint)(subsecondTicks * 100L));
                            if (nanoseconds > 250000000u)
                                throw new InvalidOperationException(
                                    "The PHC was too far from the reference edge to preserve phase safely.");
                        }
                        client.SetClockUtc(recommendation.TargetUnixSeconds,
                            nanoseconds);
                        TimeCardPhaseSample afterSet =
                            client.GetPhaseSample();
                        if (afterSet.ReferenceCounter !=
                            edge.ReferenceCounter)
                            throw new InvalidOperationException(
                                "A second GNSS reference edge arrived while setting the PHC; the target was not applied.");
                        planner.Acknowledge(recommendation.AcknowledgementToken,
                            true, StopwatchMilliseconds(),
                            afterSet.ReferenceCounter, checked((int)nanoseconds));
                    }
                    catch
                    {
                        planner.Acknowledge(recommendation.AcknowledgementToken,
                            false, StopwatchMilliseconds(),
                            edge.ReferenceCounter, 0);
                        throw;
                    }
                    continue;
                }
                if (recommendation.Action ==
                    PhcStartupActionKind.AdjustPhcPhase)
                {
                    try
                    {
                        client.AdjustPhc(
                            recommendation.PhaseAdjustmentNanoseconds);
                        ignoreNextPhaseSample = true;
                        planner.Acknowledge(recommendation.AcknowledgementToken,
                            true, StopwatchMilliseconds(),
                            phase.ReferenceCounter, 0);
                    }
                    catch
                    {
                        planner.Acknowledge(recommendation.AcknowledgementToken,
                            false, StopwatchMilliseconds(),
                            phase.ReferenceCounter, 0);
                        throw;
                    }
                    continue;
                }
                if (recommendation.Action == PhcStartupActionKind.Complete)
                {
                    report(new NativeDisciplineStatus
                    {
                        State = "STARTUP COMPLETE",
                        ClockClass = "UNCALIBRATED",
                        Detail = recommendation.Reason,
                        LastAction = "PHC initialized from validated GNSS UTC",
                        Running = true
                    });
                    return;
                }
                if (recommendation.Action == PhcStartupActionKind.Fault)
                    throw new InvalidOperationException(recommendation.Reason);
                await Task.Delay(20, token).ConfigureAwait(false);
            }
            token.ThrowIfCancellationRequested();
        }

        private async Task<TimeCardPhaseSample> WaitForReferenceEdgeAsync(
            uint recommendationCounter, CancellationToken token)
        {
            long started = Stopwatch.GetTimestamp();
            while (!token.IsCancellationRequested)
            {
                TimeCardPhaseSample current = client.GetPhaseSample();
                uint delta = unchecked(current.ReferenceCounter -
                    recommendationCounter);
                if (delta > 1u)
                    throw new InvalidOperationException(
                        "More than one GNSS reference edge passed before the PHC set could be armed.");
                if (delta == 1u && current.IsReferenceValid &&
                    current.ReferenceError == 0)
                    return current;
                if (HasElapsed(started, TimeSpan.FromSeconds(2)))
                    throw new TimeoutException(
                        "No valid GNSS reference edge arrived while setting the PHC.");
                await Task.Delay(5, token).ConfigureAwait(false);
            }
            token.ThrowIfCancellationRequested();
            throw new OperationCanceledException(token);
        }

        private static long GnssAgeMilliseconds(
            NativeGnssSessionSnapshot snapshot)
        {
            GnssAssociatedEpoch epoch = snapshot == null ? null :
                snapshot.LatestCoherentEpoch;
            if (epoch == null || epoch.UpdatedMonotonicTicks == 0)
                return long.MaxValue;
            long ticks = Stopwatch.GetTimestamp() -
                epoch.UpdatedMonotonicTicks;
            if (ticks < 0)
                return long.MaxValue;
            return checked((long)(ticks * 1000.0 / Stopwatch.Frequency));
        }

        private static long StopwatchMilliseconds()
        {
            return checked((long)(Stopwatch.GetTimestamp() * 1000.0 /
                Stopwatch.Frequency));
        }

        private NativeGnssEpoch ReadGnssEpoch()
        {
            NativeGnssEpoch epoch = new NativeGnssEpoch();
            NativeGnssSessionSnapshot snapshot = gnssMonitor == null ?
                null : gnssMonitor.Snapshot();
            if (snapshot != null)
            {
                GnssAssociatedEpoch associated =
                    snapshot.LatestAssociatedEpoch;
                long associatedTicks = associated == null ? 0 :
                    associated.UpdatedMonotonicTicks;
                long ageTicks = Stopwatch.GetTimestamp() - associatedTicks;
                bool fresh = associatedTicks != 0 &&
                    ageTicks >= 0 && ageTicks /
                        (double)Stopwatch.Frequency <= 1.5;
                GnssTimePulseSample pulse = associated == null ? null :
                    associated.TimePulse;
                GnssNavigationSample navigation = associated == null ? null :
                    associated.Navigation;
                if (fresh && pulse != null)
                {
                    epoch.HasTimePulse = pulse.FlagsValid &&
                        pulse.TimingReferenceValid &&
                        pulse.IsOneHertzGpsPulse;
                    epoch.TimePulseSequence = ((ulong)pulse.Week << 32) |
                        pulse.ITowMilliseconds;
                    epoch.QuantizationErrorPicoseconds =
                        pulse.PreviousQuantizationErrorPicoseconds ?? 0;
                }
                if (fresh && navigation != null)
                {
                    epoch.HasNavigation = true;
                    epoch.FixValid = navigation.FixOk;
                    epoch.FixType = navigation.FixType;
                    epoch.Satellites = navigation.Satellites;
                    if (navigation.Utc != null)
                        epoch.TimeAccuracyNanoseconds =
                            navigation.Utc.AccuracyNanoseconds;
                }
                GnssLeapSecondSample leap = associated == null ? null :
                    associated.LeapSeconds;
                if (fresh && leap != null && leap.IsValid)
                {
                    epoch.LeapSeconds = leap.CurrentOffsetValid ?
                        leap.CurrentOffsetSeconds : 0;
                    epoch.LeapSecondChange = leap.EventValid ?
                        leap.ChangeSeconds : 0;
                }
                GnssSurveySample survey = snapshot.LatestSurvey;
                if (survey != null && survey.IsWellFormed &&
                    IsFresh(survey.UpdatedMonotonicTicks, 2.0))
                {
                    epoch.HasSurvey = true;
                    epoch.SurveyActive = survey.Active;
                    epoch.SurveyValid = survey.Completed;
                    epoch.SurveyDurationSeconds = survey.DurationSeconds;
                    epoch.SurveyPositionErrorMeters =
                        survey.MeanAccuracyMillimeters / 1000.0;
                }
                GnssRfSample rf = snapshot.LatestRf;
                if (rf != null && rf.IsWellFormed &&
                    IsFresh(rf.UpdatedMonotonicTicks, 5.0))
                {
                    epoch.AntennaStatus = rf.AntennaStatus;
                    epoch.AntennaPower = rf.AntennaPower;
                }
            }
            if (epoch.HasSurvey)
            {
                if (epoch.SurveyActive)
                    surveyCompleted = false;
                else if (epoch.SurveyValid)
                    surveyCompleted = true;
            }
            return epoch;
        }

        private async Task<NativeGnssEpoch> WaitForGnssEpochAsync(
            ulong consumedSequence, CancellationToken token)
        {
            long started = Stopwatch.GetTimestamp();
            NativeGnssEpoch latest = ReadGnssEpoch();
            while (!token.IsCancellationRequested &&
                (!latest.HasTimePulse ||
                 latest.TimePulseSequence == consumedSequence))
            {
                if (HasElapsed(started, TimeSpan.FromMilliseconds(800)))
                {
                    latest.HasTimePulse = false;
                    return latest;
                }
                await Task.Delay(5, token).ConfigureAwait(false);
                latest = ReadGnssEpoch();
            }
            token.ThrowIfCancellationRequested();
            return latest;
        }

        private static bool IsFresh(long updatedTicks, double seconds)
        {
            if (updatedTicks <= 0)
                return false;
            long age = Stopwatch.GetTimestamp() - updatedTicks;
            return age >= 0 && age / (double)Stopwatch.Frequency <= seconds;
        }

        private static NativeGnssEpoch DecodeGnssEpoch(
            IDictionary<ushort, byte[]> messages)
        {
            NativeGnssEpoch epoch = new NativeGnssEpoch();
            byte[] payload;
            if (messages.TryGetValue(0x0d01, out payload) &&
                payload.Length >= 16)
            {
                epoch.HasTimePulse = true;
                epoch.TimePulseSequence = 1;
                epoch.QuantizationErrorPicoseconds =
                    BitConverter.ToInt32(payload, 8);
            }
            if (messages.TryGetValue(0x0107, out payload) &&
                payload.Length >= 24)
            {
                byte fixType = payload[20];
                byte flags = payload[21];
                epoch.HasNavigation = true;
                epoch.FixValid = fixType >= 2 && (flags & 1u) != 0u &&
                    payload[23] > 3u;
            }
            if (messages.TryGetValue(0x0d04, out payload) &&
                payload.Length >= 26)
            {
                epoch.HasSurvey = true;
                epoch.SurveyActive = payload[25] != 0;
                epoch.SurveyDurationSeconds = BitConverter.ToUInt32(payload, 0);
                epoch.SurveyValid = payload[24] != 0 &&
                    !epoch.SurveyActive &&
                    epoch.SurveyDurationSeconds >= 1200u;
            }
            return epoch;
        }

        private string GnssReferenceDetail(NativeGnssEpoch epoch)
        {
            if (fakeHoldover)
                return "simulated holdover";
            if (!epoch.HasTimePulse)
                return "waiting for UBX-TIM-TP";
            if (!epoch.HasNavigation)
                return "waiting for UBX-NAV-PVT";
            if (!epoch.FixValid)
                return "GNSS navigation fix invalid";
            if (options.BypassSurvey)
                return "GNSS phase and fix valid · survey bypassed";
            if (!surveyCompleted)
                return epoch.HasSurvey && epoch.SurveyActive ?
                    "GNSS survey in progress" : "waiting for UBX-TIM-SVIN";
            return "GNSS phase, fix and survey valid";
        }

        private async Task RunGuardedAsync(CancellationToken token)
        {
            try
            {
                await RunAsync(token).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (token.IsCancellationRequested)
            {
            }
            catch (Exception ex)
            {
                try { SaveParameters(); } catch { }
                try { client.SetPhaseMeter(false, false, false); } catch { }
                ReleaseLease();
                report(new NativeDisciplineStatus
                {
                    State = "FAULT",
                    ClockClass = "UNCALIBRATED",
                    Detail = ex.Message,
                    LastAction = "Stopped safely; the service will retry",
                    Running = false
                });
            }
        }

        private string ApplyOutput(NativeDisciplineOutput output,
                                   Mro50Status current)
        {
            switch ((DisciplineAction)output.Action)
            {
                case DisciplineAction.None:
                    return "No adjustment";
                case DisciplineAction.PhaseJump:
                    long correction = -(long)output.PhaseJumpNanoseconds;
                    if (!capabilities.CanAdjustPhcPhase)
                        throw new InvalidOperationException(
                            "miniCOD requested a PHC phase correction that this driver does not support.");
                    client.AdjustPhc(correction);
                    ignoreNextPhaseSample = true;
                    return "PHC phase " + FormatSigned(correction) + " ns";
                case DisciplineAction.AdjustFine:
                    if (output.Setpoint < capabilities.FineMinimum ||
                        output.Setpoint > capabilities.FineMaximum)
                        throw new InvalidOperationException(
                            "miniCOD requested an out-of-range fine control value.");
                    if (output.Setpoint != current.FineAdjustment)
                        client.SetMro50FineAdjustment(output.Setpoint);
                    return "Fine control " + output.Setpoint.ToString(
                        CultureInfo.InvariantCulture);
                case DisciplineAction.AdjustCoarse:
                    if (output.Setpoint < capabilities.CoarseMinimum ||
                        output.Setpoint > capabilities.CoarseMaximum)
                        throw new InvalidOperationException(
                            "miniCOD requested an out-of-range coarse control value.");
                    if (output.Setpoint != current.CoarseAdjustment)
                        client.SetMro50CoarseAdjustment(output.Setpoint);
                    return "Coarse control " + output.Setpoint.ToString(
                        CultureInfo.InvariantCulture);
                case DisciplineAction.Calibrate:
                    return "Calibration requested";
                case DisciplineAction.SaveParameters:
                    return "Discipline parameters saved";
                default:
                    throw new InvalidOperationException(
                        "miniCOD returned an unknown action.");
            }
        }

        private async Task CalibrateAsync(CancellationToken token)
        {
            NativeDisciplineAlgorithm.CalibrationPlan plan =
                algorithm.GetCalibrationPlan();
            uint originalFine = client.GetMro50Status().FineAdjustment;
            List<float> samples = new List<float>(
                plan.Points.Length * plan.SamplesPerPoint);
            try
            {
                for (int pointIndex = 0; pointIndex < plan.Points.Length;
                    pointIndex++)
                {
                    ushort point = plan.Points[pointIndex];
                    client.SetMro50FineAdjustment(point);
                    report(new NativeDisciplineStatus
                    {
                        State = "CALIBRATION",
                        ClockClass = "CALIBRATING",
                        Detail = string.Format(CultureInfo.InvariantCulture,
                            "Settling at control point {0}/{1} ({2}).",
                            pointIndex + 1, plan.Points.Length, point),
                        LastAction = "Fine control " + point,
                        Running = true,
                        Calibrating = true
                    });
                    await Task.Delay(TimeSpan.FromSeconds(6), token)
                        .ConfigureAwait(false);
                    uint previousReference = 0;
                    uint previousOscillator = 0;
                    while (samples.Count <
                        (pointIndex + 1) * plan.SamplesPerPoint)
                    {
                        token.ThrowIfCancellationRequested();
                        NativeGnssEpoch reference = ReadGnssEpoch();
                        TimeCardPhaseSample sample = client.GetPhaseSample();
                        if (sample.ReferenceCounter != previousReference &&
                            sample.OscillatorCounter != previousOscillator &&
                            sample.IsPhaseValid && sample.ReferenceError == 0 &&
                            sample.OscillatorError == 0 &&
                            reference.HasTimePulse && reference.FixValid &&
                            (surveyCompleted || options.BypassSurvey) &&
                            !fakeHoldover)
                        {
                            previousReference = sample.ReferenceCounter;
                            previousOscillator = sample.OscillatorCounter;
                            samples.Add((float)(sample.PhaseNanoseconds +
                                reference.QuantizationErrorPicoseconds /
                                    1000.0));
                        }
                        else
                        {
                            await Task.Delay(100, token).ConfigureAwait(false);
                        }
                    }
                }
                algorithm.CompleteCalibration(samples.ToArray());
            }
            finally
            {
                /* Never leave the mRO parked at a calibration control point. */
                client.SetMro50FineAdjustment(originalFine);
            }
            SaveParameters();
        }

        private NativeDisciplineStatus CreateStatus(
            NativeDisciplineOutput output, NativeDisciplineInput input,
            string action, string referenceDetail, NativeGnssEpoch gnss)
        {
            return new NativeDisciplineStatus
            {
                State = StateName(output.State),
                ClockClass = ClockClassName(output.ClockClass),
                Detail = string.Format(CultureInfo.InvariantCulture,
                    "Convergence {0:F1}% ({1}/{2}) · {3} · qErr(n-1) {4:+0;-0;0} ps",
                    output.ConvergencePercent, output.ConvergenceCount,
                    output.ConvergenceThreshold,
                    referenceDetail, input.QuantizationErrorPicoseconds),
                LastAction = action,
                PhaseNanoseconds = input.PhaseErrorNanoseconds,
                Fine = input.FineSetpoint,
                Coarse = checked((uint)Math.Max(0, input.CoarseSetpoint)),
                TemperatureCelsius = input.Temperature,
                GnssValid = (input.Flags & 1u) != 0,
                OscillatorLocked = (input.Flags & 2u) != 0,
                ReadyForHoldover = output.ReadyForHoldover != 0u,
                ConvergencePercent = output.ConvergencePercent,
                ConvergenceCount = output.ConvergenceCount,
                ConvergenceThreshold = output.ConvergenceThreshold,
                Running = true,
                GnssFixType = gnss.FixType,
                GnssSatellites = gnss.Satellites,
                GnssLeapSeconds = gnss.LeapSeconds,
                GnssLeapSecondChange = gnss.LeapSecondChange,
                GnssAntennaPower = gnss.AntennaPower,
                GnssAntennaStatus = gnss.AntennaStatus,
                GnssSurveyPositionErrorMeters =
                    gnss.SurveyPositionErrorMeters,
                GnssTimeAccuracyNanoseconds =
                    gnss.TimeAccuracyNanoseconds
            };
        }

        private byte[] LoadParameters()
        {
            string path = ParameterPath();
            if (capabilities.HasDisciplineParameters)
            {
                try
                {
                    TimeCardDisciplineParameters card =
                        client.ReadDisciplineParameters();
                    if (card.IsPresent && card.IsValid &&
                        card.Data.Length == 512)
                    {
                        lastCardParameters = (byte[])card.Data.Clone();
                        return card.Data;
                    }
                }
                catch
                {
                }
            }
            try
            {
                byte[] host = File.Exists(path) ? File.ReadAllBytes(path) : null;
                if (host != null && host.Length == 512)
                {
                    lastHostParameters = (byte[])host.Clone();
                    return host;
                }
            }
            catch
            {
            }
            return null;
        }

        private void SaveParameters()
        {
            if (algorithm == null)
                return;
            byte[] parameters = algorithm.GetParameters();
            bool cardNeedsWrite = capabilities.HasDisciplineParameters &&
                !ByteArraysEqual(parameters, lastCardParameters);
            bool hostNeedsWrite = !ByteArraysEqual(parameters,
                lastHostParameters);
            if (!cardNeedsWrite && !hostNeedsWrite)
                return;
            if (cardNeedsWrite)
            {
                try
                {
                    TimeCardDisciplineParameters result =
                        client.WriteDisciplineParameters(parameters);
                    if (result.IsValid &&
                        ByteArraysEqual(parameters, result.Data))
                        lastCardParameters = (byte[])parameters.Clone();
                }
                catch
                {
                    /* Keep lastCardParameters unchanged so the next save retries. */
                }
            }
            if (hostNeedsWrite)
            {
                string path = ParameterPath();
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                string temporary = path + ".tmp";
                File.WriteAllBytes(temporary, parameters);
                if (File.Exists(path))
                    File.Replace(temporary, path, null);
                else
                    File.Move(temporary, path);
                lastHostParameters = (byte[])parameters.Clone();
            }
        }

        private string ParameterPath()
        {
            return Path.Combine(Environment.GetFolderPath(
                Environment.SpecialFolder.CommonApplicationData),
                "OCP Time Card", "Oscillatord", "Cards", cardKey,
                "discipline.bin");
        }

        private string ResolveCardKey()
        {
            string serial = null;
            try
            {
                TimeCardIdentity identity = client.GetIdentity();
                if (identity.IsValid && !string.IsNullOrEmpty(
                    identity.SerialNumber))
                    serial = identity.SerialNumber;
            }
            catch
            {
            }
            return StableCardKey(serial, client.DevicePath);
        }

        internal static string StableCardKey(string serial, string devicePath)
        {
            if (!string.IsNullOrWhiteSpace(serial))
            {
                StringBuilder normalized = new StringBuilder(12);
                foreach (char character in serial.Trim())
                {
                    if ((character >= '0' && character <= '9') ||
                        (character >= 'a' && character <= 'f') ||
                        (character >= 'A' && character <= 'F'))
                        normalized.Append(char.ToUpperInvariant(character));
                    else if (character != ':' && character != '-')
                    {
                        normalized.Clear();
                        break;
                    }
                }
                if (normalized.Length == 12)
                {
                    return string.Join("-", new[]
                    {
                        normalized.ToString(0, 2),
                        normalized.ToString(2, 2),
                        normalized.ToString(4, 2),
                        normalized.ToString(6, 2),
                        normalized.ToString(8, 2),
                        normalized.ToString(10, 2)
                    });
                }
            }
            string path = (devicePath ?? string.Empty).Trim();
            if (!path.StartsWith(@"\\?\", StringComparison.Ordinal))
                throw new InvalidOperationException(
                    "The card has no valid hardware serial or stable device-interface path; host calibration persistence was refused to prevent cross-card reuse.");
            byte[] canonical = Encoding.UTF8.GetBytes(path.ToUpperInvariant());
            byte[] digest;
            using (SHA256 algorithm = SHA256.Create())
                digest = algorithm.ComputeHash(canonical);
            return "path-" + BitConverter.ToString(digest, 0, 16)
                .Replace("-", string.Empty).ToLowerInvariant();
        }

        private void ReleaseLease()
        {
            if (!leaseAcquired)
                return;
            try { client.ReleaseDisciplineLease(); }
            catch { }
            leaseAcquired = false;
        }

        private static double ConvertTemperature(uint raw)
        {
            if (raw == 0 || raw >= 4095)
                return -999;
            double ratio = raw / 4095.0;
            double resistance = 47000.0 * ratio / (1.0 - ratio);
            return 4100.0 * 298.15 /
                (298.15 * Math.Log(0.00001 * resistance) + 4100.0) - 273.14;
        }

        private static string StateName(uint state)
        {
            string[] names = { "WARMUP", "INITIALIZING", "TRACKING",
                "HOLDOVER", "CALIBRATION" };
            return state < (uint)names.Length ? names[(int)state] : "UNKNOWN";
        }

        private static string ClockClassName(uint value)
        {
            string[] names = { "UNCALIBRATED", "CALIBRATING", "HOLDOVER",
                "LOCKED" };
            return value < (uint)names.Length ? names[(int)value] : "UNKNOWN";
        }

        private static string FormatSigned(long value)
        {
            return value > 0 ? "+" + value.ToString(
                CultureInfo.InvariantCulture) : value.ToString(
                CultureInfo.InvariantCulture);
        }

        private static bool ByteArraysEqual(byte[] left, byte[] right)
        {
            if (ReferenceEquals(left, right))
                return true;
            if (left == null || right == null || left.Length != right.Length)
                return false;
            for (int index = 0; index < left.Length; index++)
            {
                if (left[index] != right[index])
                    return false;
            }
            return true;
        }

        public void Dispose()
        {
            if (worker != null)
                StopAsync().GetAwaiter().GetResult();
        }

        private sealed class NativeGnssEpoch
        {
            public bool HasTimePulse;
            public ulong TimePulseSequence;
            public int QuantizationErrorPicoseconds;
            public bool HasNavigation;
            public bool FixValid;
            public bool HasSurvey;
            public bool SurveyValid;
            public bool SurveyActive;
            public uint SurveyDurationSeconds;
            public int FixType;
            public int Satellites;
            public int LeapSeconds;
            public int LeapSecondChange;
            public int AntennaPower;
            public int AntennaStatus;
            public double SurveyPositionErrorMeters;
            public uint TimeAccuracyNanoseconds;
        }

        private static bool HasElapsed(long started, TimeSpan duration)
        {
            long elapsed = Stopwatch.GetTimestamp() - started;
            double seconds = elapsed / (double)Stopwatch.Frequency;
            return seconds >= duration.TotalSeconds;
        }

        private sealed class GnssEpochMonitor
        {
            private readonly object gate = new object();
            private readonly TimeCardClient client;
            private readonly List<byte> buffer = new List<byte>();
            private NativeGnssEpoch latest = new NativeGnssEpoch();
            private long pulseUpdated;
            private long navigationUpdated;
            private long surveyUpdated;

            public GnssEpochMonitor(TimeCardClient activeClient)
            {
                client = activeClient;
            }

            public async Task RunAsync(CancellationToken token)
            {
                while (!token.IsCancellationRequested)
                {
                    UartReadResult result = client.ReadUart(0u, 256u, 20u);
                    if (result.Data.Length == 0)
                    {
                        await Task.Delay(5, token).ConfigureAwait(false);
                        continue;
                    }
                    Feed(result.Data);
                }
            }

            public NativeGnssEpoch Snapshot()
            {
                lock (gate)
                {
                    long now = Stopwatch.GetTimestamp();
                    bool pulseFresh = IsFresh(now, pulseUpdated, 1.5);
                    bool navigationFresh = IsFresh(now, navigationUpdated,
                        1.5);
                    bool surveyFresh = IsFresh(now, surveyUpdated, 5.0);
                    return new NativeGnssEpoch
                    {
                        HasTimePulse = latest.HasTimePulse && pulseFresh,
                        TimePulseSequence = latest.TimePulseSequence,
                        QuantizationErrorPicoseconds =
                            latest.QuantizationErrorPicoseconds,
                        HasNavigation = latest.HasNavigation &&
                            navigationFresh,
                        FixValid = latest.FixValid && navigationFresh,
                        HasSurvey = latest.HasSurvey && surveyFresh,
                        SurveyValid = latest.SurveyValid,
                        SurveyActive = latest.SurveyActive,
                        SurveyDurationSeconds = latest.SurveyDurationSeconds
                    };
                }
            }

            private void Feed(byte[] bytes)
            {
                buffer.AddRange(bytes);
                if (buffer.Count > 65536)
                    buffer.RemoveRange(0, buffer.Count - 32768);
                byte messageClass;
                byte messageId;
                byte[] payload;
                while (TryTakeUbxFrame(buffer, out messageClass,
                    out messageId, out payload))
                    Update(messageClass, messageId, payload);
            }

            private void Update(byte messageClass, byte messageId,
                byte[] payload)
            {
                long now = Stopwatch.GetTimestamp();
                lock (gate)
                {
                    if (messageClass == 0x0d && messageId == 0x01 &&
                        payload.Length >= 16)
                    {
                        latest.HasTimePulse = true;
                        latest.TimePulseSequence++;
                        latest.QuantizationErrorPicoseconds =
                            BitConverter.ToInt32(payload, 8);
                        pulseUpdated = now;
                    }
                    else if (messageClass == 0x01 && messageId == 0x07 &&
                        payload.Length >= 24)
                    {
                        latest.HasNavigation = true;
                        latest.FixValid = payload[20] >= 2 &&
                            (payload[21] & 1u) != 0u && payload[23] > 3u;
                        navigationUpdated = now;
                    }
                    else if (messageClass == 0x0d && messageId == 0x04 &&
                        payload.Length >= 26)
                    {
                        latest.HasSurvey = true;
                        latest.SurveyDurationSeconds =
                            BitConverter.ToUInt32(payload, 0);
                        latest.SurveyActive = payload[25] != 0;
                        latest.SurveyValid = payload[24] != 0 &&
                            !latest.SurveyActive &&
                            latest.SurveyDurationSeconds >= 1200u;
                        surveyUpdated = now;
                    }
                }
            }

            private static bool IsFresh(long now, long updated,
                double maximumSeconds)
            {
                if (updated == 0 || now < updated)
                    return false;
                return (now - updated) / (double)Stopwatch.Frequency <=
                    maximumSeconds;
            }

            private static bool TryTakeUbxFrame(List<byte> data,
                out byte messageClass, out byte messageId,
                out byte[] payload)
            {
                messageClass = 0;
                messageId = 0;
                payload = null;
                while (true)
                {
                    int start = -1;
                    for (int index = 0; index + 1 < data.Count; ++index)
                    {
                        if (data[index] == 0xb5 && data[index + 1] == 0x62)
                        {
                            start = index;
                            break;
                        }
                    }
                    if (start < 0)
                    {
                        if (data.Count > 0 && data[data.Count - 1] == 0xb5)
                            data.RemoveRange(0, data.Count - 1);
                        else
                            data.Clear();
                        return false;
                    }
                    if (start > 0)
                        data.RemoveRange(0, start);
                    if (data.Count < 8)
                        return false;
                    int length = data[4] | (data[5] << 8);
                    if (length > 4096)
                    {
                        data.RemoveAt(0);
                        continue;
                    }
                    int frameLength = length + 8;
                    if (data.Count < frameLength)
                        return false;
                    byte checksumA = 0;
                    byte checksumB = 0;
                    for (int index = 2; index < length + 6; ++index)
                    {
                        unchecked
                        {
                            checksumA += data[index];
                            checksumB += checksumA;
                        }
                    }
                    if (checksumA != data[length + 6] ||
                        checksumB != data[length + 7])
                    {
                        data.RemoveAt(0);
                        continue;
                    }
                    messageClass = data[2];
                    messageId = data[3];
                    payload = new byte[length];
                    data.CopyTo(6, payload, 0, length);
                    data.RemoveRange(0, frameLength);
                    return true;
                }
            }
        }
    }
}
