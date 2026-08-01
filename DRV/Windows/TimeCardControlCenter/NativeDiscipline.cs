using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
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
            StringBuilder error = new StringBuilder(1024);
            IntPtr value = NativeMethods.Create(factoryCoarse, savedParameters,
                savedParameters == null ? 0u : (uint)savedParameters.Length,
                error, (uint)error.Capacity);
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
        public bool Running { get; set; }
        public bool Calibrating { get; set; }
    }

    internal sealed class NativeDisciplineEngine : IDisposable
    {
        private readonly TimeCardClient client;
        private readonly TimeCardCapabilities capabilities;
        private readonly Action<NativeDisciplineStatus> report;
        private readonly bool bypassSurvey;
        private NativeDisciplineAlgorithm algorithm;
        private CancellationTokenSource cancellation;
        private Task worker;
        private volatile bool fakeHoldover;
        private volatile bool calibrationRequested;
        private uint lastReferenceCounter;
        private uint lastOscillatorCounter;
        private int lastQuantizationErrorPicoseconds;
        private bool hasQuantizationError;
        private bool surveyCompleted;
        private byte[] lastPersistedParameters;

        public NativeDisciplineEngine(TimeCardClient activeClient,
            TimeCardCapabilities activeCapabilities,
            Action<NativeDisciplineStatus> statusReport,
            bool allowSurveyBypass)
        {
            client = activeClient;
            capabilities = activeCapabilities;
            report = statusReport;
            bypassSurvey = allowSurveyBypass;
        }

        public bool IsRunning { get { return worker != null; } }
        public bool FakeHoldover { get { return fakeHoldover; } }

        public void Start()
        {
            if (IsRunning)
                return;
            if (!capabilities.HasDirectMro50 ||
                !capabilities.HasPairedPhaseMeter)
                throw new NotSupportedException(
                    "Native miniCOD software discipline requires the Orolia ART mRO-50 and paired PPS phase meter. SA53 cards use their hardware discipline on the Atomic Clock workspace.");
            Mro50Status oscillator = client.GetMro50Status();
            if (!oscillator.IsFineValid || !oscillator.IsCoarseValid)
                throw new InvalidOperationException(
                    "The mRO-50 fine and coarse controls are not readable; discipline was not started.");
            byte[] saved = LoadParameters();
            algorithm = NativeDisciplineAlgorithm.Create(
                oscillator.CoarseAdjustment, saved);
            try
            {
                client.SetPhaseMeter(true, false, false);
                cancellation = new CancellationTokenSource();
                worker = Task.Run(() => RunGuardedAsync(cancellation.Token));
            }
            catch
            {
                try { client.SetPhaseMeter(false, false, false); } catch { }
                if (cancellation != null)
                    cancellation.Dispose();
                cancellation = null;
                algorithm.Dispose();
                algorithm = null;
                worker = null;
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
            calibrationRequested = true;
        }

        private async Task RunAsync(CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                NativeGnssEpoch gnss = ReadGnssEpoch();
                TimeCardPhaseSample phase = client.GetPhaseSample();
                if (phase.OscillatorCounter == lastOscillatorCounter)
                {
                    await Task.Delay(100, token).ConfigureAwait(false);
                    continue;
                }
                bool referenceFresh = phase.ReferenceCounter !=
                    lastReferenceCounter;
                lastReferenceCounter = phase.ReferenceCounter;
                lastOscillatorCounter = phase.OscillatorCounter;
                Mro50Status oscillator = client.GetMro50Status();
                bool valid = phase.IsPhaseValid && referenceFresh &&
                    phase.ReferenceError == 0 && phase.OscillatorError == 0 &&
                    gnss.HasTimePulse && gnss.FixValid &&
                    (surveyCompleted || bypassSurvey) &&
                    !fakeHoldover;
                int quantizationError = hasQuantizationError ?
                    lastQuantizationErrorPicoseconds : 0;
                if (gnss.HasTimePulse)
                {
                    lastQuantizationErrorPicoseconds =
                        Math.Abs((long)gnss.QuantizationErrorPicoseconds) <= 5000L ?
                        gnss.QuantizationErrorPicoseconds : 0;
                    hasQuantizationError = true;
                }
                NativeDisciplineInput input = new NativeDisciplineInput
                {
                    Temperature = ConvertTemperature(oscillator.TemperatureRaw),
                    PhaseErrorNanoseconds = valid ? phase.PhaseNanoseconds : 0,
                    FineSetpoint = oscillator.FineAdjustment,
                    CoarseSetpoint = checked((int)oscillator.CoarseAdjustment),
                    // TIM-TP describes the next pulse; upstream uses qErr(n-1).
                    QuantizationErrorPicoseconds = quantizationError,
                    Flags = (valid ? 1u | 4u : 0u) |
                        (oscillator.IsLocked ? 2u : 0u) |
                        (calibrationRequested ? 8u : 0u)
                };
                calibrationRequested = false;
                NativeDisciplineOutput output = algorithm.Process(input);
                string action = ApplyOutput(output, oscillator);
                if ((DisciplineAction)output.Action == DisciplineAction.Calibrate)
                {
                    await CalibrateAsync(token).ConfigureAwait(false);
                    action = "Calibration completed and saved";
                }
                if ((DisciplineAction)output.Action ==
                    DisciplineAction.SaveParameters)
                    SaveParameters();
                report(CreateStatus(output, input, action,
                    GnssReferenceDetail(gnss)));
            }
        }

        private NativeGnssEpoch ReadGnssEpoch()
        {
            IDictionary<ushort, byte[]> messages =
                client.CaptureUbxMessagesPreserveBaud(0u, 1200u);
            NativeGnssEpoch epoch = DecodeGnssEpoch(messages);
            if (epoch.HasSurvey)
            {
                if (epoch.SurveyActive)
                    surveyCompleted = false;
                else if (epoch.SurveyValid)
                    surveyCompleted = true;
            }
            return epoch;
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
                epoch.QuantizationErrorPicoseconds =
                    BitConverter.ToInt32(payload, 8);
            }
            if (messages.TryGetValue(0x0107, out payload) &&
                payload.Length >= 24)
            {
                byte fixType = payload[20];
                byte flags = payload[21];
                epoch.HasNavigation = true;
                epoch.FixValid = fixType >= 2 && (flags & 1u) != 0u;
            }
            if (messages.TryGetValue(0x0d04, out payload) &&
                payload.Length >= 26)
            {
                epoch.HasSurvey = true;
                epoch.SurveyActive = payload[25] != 0;
                epoch.SurveyValid = payload[24] != 0;
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
            if (bypassSurvey)
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
                report(new NativeDisciplineStatus
                {
                    State = "FAULT",
                    ClockClass = "UNCALIBRATED",
                    Detail = ex.Message,
                    LastAction = "Stopped adjusting; press Stop to release phase capture",
                    Running = true
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
                            (surveyCompleted || bypassSurvey) && !fakeHoldover)
                        {
                            previousReference = sample.ReferenceCounter;
                            previousOscillator = sample.OscillatorCounter;
                            samples.Add((float)sample.PhaseNanoseconds);
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
            string action, string referenceDetail)
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
                Running = true
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
                        lastPersistedParameters = (byte[])card.Data.Clone();
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
                    lastPersistedParameters = (byte[])host.Clone();
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
            if (ByteArraysEqual(parameters, lastPersistedParameters))
                return;
            bool cardSaved = false;
            if (capabilities.HasDisciplineParameters)
            {
                try
                {
                    TimeCardDisciplineParameters result =
                        client.WriteDisciplineParameters(parameters);
                    cardSaved = result.IsValid &&
                        ByteArraysEqual(parameters, result.Data);
                }
                catch
                {
                    cardSaved = false;
                }
            }
            string path = ParameterPath();
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temporary = path + ".tmp";
            File.WriteAllBytes(temporary, parameters);
            if (File.Exists(path))
                File.Replace(temporary, path, null);
            else
                File.Move(temporary, path);
            if (cardSaved || File.Exists(path))
                lastPersistedParameters = (byte[])parameters.Clone();
        }

        private string ParameterPath()
        {
            return Path.Combine(Environment.GetFolderPath(
                Environment.SpecialFolder.CommonApplicationData),
                "OCP Time Card", "discipline-profile-" +
                capabilities.BoardProfile.ToString(CultureInfo.InvariantCulture) +
                ".bin");
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
            public int QuantizationErrorPicoseconds;
            public bool HasNavigation;
            public bool FixValid;
            public bool HasSurvey;
            public bool SurveyValid;
            public bool SurveyActive;
        }
    }
}
