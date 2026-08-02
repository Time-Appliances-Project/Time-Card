using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace TimeCardControlCenter
{
    public sealed class TimeCardClient : IDisposable
    {
        private const string DevicePathPrefix = @"\\.\TimeCard";
        private const string LegacyDevicePath = DevicePathPrefix + "0";
        private const uint GenericRead = 0x80000000;
        private const uint GenericWrite = 0x40000000;
        private const uint FileShareRead = 0x00000001;
        private const uint FileShareWrite = 0x00000002;
        private const uint OpenExisting = 3;
        private const uint FileDeviceUnknown = 0x22;
        private const uint FileReadAccess = 0x0001;
        private const uint FileWriteAccess = 0x0002;
        private const int MaximumUartTransfer = 256;
        private const int MaximumI2cTransfer = 255;
        private const int I2cTransferHeaderSize = 20;
        private const int I2cTransferBufferSize = 276;
        private const int MaximumFlashTransfer = 256;
        private const int FlashTransferHeaderSize = 16;
        private const int FlashTransferBufferSize = 272;
        private const uint DigcfPresent = 0x00000002;
        private const uint DigcfDeviceInterface = 0x00000010;
        private const int ErrorInsufficientBuffer = 122;
        private static readonly Guid TimeCardDeviceInterface = new Guid(
            "8315a67a-3d76-4f6c-b557-b5a65d55545c");

        private static readonly uint IoctlSetTime = ControlCode(1, FileWriteAccess);
        private static readonly uint IoctlGetInfo = ControlCode(2, FileReadAccess);
        private static readonly uint IoctlGetCrossTimestamp = ControlCode(3, FileReadAccess);
        private static readonly uint IoctlUartConfigure = ControlCode(4, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlUartRead = ControlCode(5, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlUartWrite = ControlCode(6, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlHierarchy = ControlCode(7, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlSmaQuery = ControlCode(8, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlSmaSet = ControlCode(9, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlI2cGetStatus = ControlCode(10, FileReadAccess);
        private static readonly uint IoctlI2cProbe = ControlCode(11, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlI2cRead = ControlCode(12, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlClockSourceSet = ControlCode(13, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlNmeaQuery = ControlCode(14, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlNmeaSet = ControlCode(15, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlGetIdentity = ControlCode(16, FileReadAccess);
        private static readonly uint IoctlSignalQuery = ControlCode(17, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlSignalSet = ControlCode(18, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlFrequencyQuery = ControlCode(19, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlFrequencySet = ControlCode(20, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlFlashQuery = ControlCode(21, FileReadAccess);
        private static readonly uint IoctlFlashRead = ControlCode(22, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlFlashErase = ControlCode(23, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlFlashProgram = ControlCode(24, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlUartObserve = ControlCode(25, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlI2cMuxQuery = ControlCode(26, FileReadAccess);
        private static readonly uint IoctlI2cMuxSet = ControlCode(27, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlLedQuery = ControlCode(28, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlLedSet = ControlCode(29, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlSensorQuery = ControlCode(30, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlMro50Query = ControlCode(31, FileReadAccess);
        private static readonly uint IoctlMro50Control = ControlCode(32, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlGetCapabilities = ControlCode(33, FileReadAccess);
        private static readonly uint IoctlPhaseQuery = ControlCode(34, FileReadAccess);
        private static readonly uint IoctlPhaseControl = ControlCode(35, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlPhcAdjust = ControlCode(36, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlDisciplineRead = ControlCode(37, FileReadAccess);
        private static readonly uint IoctlDisciplineWrite = ControlCode(38, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlFpgaCapabilities = ControlCode(39, FileReadAccess);
        private static readonly uint IoctlClockTelemetry = ControlCode(40, FileReadAccess);
        private static readonly uint IoctlPpsQuery = ControlCode(41, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlPpsSet = ControlCode(42, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlTimecodeQuery = ControlCode(43, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlTimecodeSet = ControlCode(44, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlTodQuery = ControlCode(45, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlTodSet = ControlCode(46, FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlFpgaImageQuery = ControlCode(47, FileReadAccess);
        private static readonly uint IoctlDisciplineLease = ControlCode(48,
            FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlTimestampQuery = ControlCode(49,
            FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlTimestampSet = ControlCode(50,
            FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlTimestampRead = ControlCode(51,
            FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlClockAdjustQuery = ControlCode(52,
            FileReadAccess);
        private static readonly uint IoctlClockAdjustSet = ControlCode(53,
            FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlCoreInventoryQuery = ControlCode(54,
            FileReadAccess);
        private static readonly uint IoctlSignalEventRead = ControlCode(55,
            FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlFpgaContractQuery = ControlCode(56,
            FileReadAccess);
        private static readonly uint IoctlFpgaContractSet = ControlCode(57,
            FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlNmeaUtcQuery = ControlCode(58,
            FileReadAccess);
        private static readonly uint IoctlNmeaUtcSet = ControlCode(59,
            FileReadAccess | FileWriteAccess);
        private static readonly uint IoctlClockAdvancedQuery = ControlCode(60,
            FileReadAccess);
        private static readonly uint IoctlClockAdvancedSet = ControlCode(61,
            FileReadAccess | FileWriteAccess);

        private readonly object gate = new object();
        private readonly string devicePath;
        private SafeFileHandle handle;

        public TimeCardClient()
            : this(0u)
        {
        }

        public TimeCardClient(uint deviceIndex)
            : this(ResolveDevicePath(deviceIndex))
        {
        }

        public TimeCardClient(string selectedDevicePath)
        {
            if (string.IsNullOrWhiteSpace(selectedDevicePath))
                throw new ArgumentException(
                    "A Time Card device-interface path is required.",
                    "selectedDevicePath");
            devicePath = selectedDevicePath;
            handle = CreateFile(devicePath, GenericRead | GenericWrite,
                FileShareRead | FileShareWrite, IntPtr.Zero, OpenExisting, 0, IntPtr.Zero);
            if (handle.IsInvalid)
            {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                handle = null;
                throw new Win32Exception(error,
                    "Unable to open the OCP Time Card driver at " + devicePath + ".");
            }
        }

        public string DevicePath { get { return devicePath; } }

        public static IList<string> EnumerateDevicePaths()
        {
            List<string> paths = new List<string>();
            Guid interfaceClass = TimeCardDeviceInterface;
            IntPtr set = SetupDiGetClassDevs(ref interfaceClass, IntPtr.Zero,
                IntPtr.Zero, DigcfPresent | DigcfDeviceInterface);
            if (set == new IntPtr(-1))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Windows could not create the Time Card device inventory.");
            try
            {
                for (uint index = 0; ; ++index)
                {
                    SpDeviceInterfaceData data = new SpDeviceInterfaceData
                    {
                        Size = (uint)Marshal.SizeOf(
                            typeof(SpDeviceInterfaceData))
                    };
                    if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero,
                        ref interfaceClass, index, ref data))
                    {
                        int error = Marshal.GetLastWin32Error();
                        if (error == 259) // ERROR_NO_MORE_ITEMS
                            break;
                        throw new Win32Exception(error,
                            "Windows could not enumerate Time Card interfaces.");
                    }

                    uint required = 0;
                    SetupDiGetDeviceInterfaceDetail(set, ref data,
                        IntPtr.Zero, 0, ref required, IntPtr.Zero);
                    int detailError = Marshal.GetLastWin32Error();
                    if (required == 0 || detailError != ErrorInsufficientBuffer)
                        throw new Win32Exception(detailError,
                            "Windows could not size a Time Card interface path.");
                    IntPtr detail = Marshal.AllocHGlobal(checked((int)required));
                    try
                    {
                        Marshal.WriteInt32(detail, IntPtr.Size == 8 ? 8 : 6);
                        if (!SetupDiGetDeviceInterfaceDetail(set, ref data,
                            detail, required, ref required, IntPtr.Zero))
                            throw new Win32Exception(Marshal.GetLastWin32Error(),
                                "Windows could not read a Time Card interface path.");
                        // cbSize is 8 on x64 (6 on x86), but DevicePath always
                        // begins immediately after the 4-byte DWORD.
                        string path = Marshal.PtrToStringUni(IntPtr.Add(detail,
                            4));
                        if (!string.IsNullOrWhiteSpace(path))
                            paths.Add(path);
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(detail);
                    }
                }
            }
            finally
            {
                SetupDiDestroyDeviceInfoList(set);
            }
            return paths.OrderBy(value => value,
                StringComparer.OrdinalIgnoreCase).ToList();
        }

        public static IList<TimeCardDeviceDescriptor> EnumerateDevices()
        {
            List<TimeCardDeviceDescriptor> devices =
                new List<TimeCardDeviceDescriptor>();
            List<string> paths = EnumerateDevicePaths().ToList();
            bool legacyFallback = paths.Count == 0;
            if (legacyFallback)
                paths.Add(LegacyDevicePath);
            foreach (string path in paths)
            {
                string serial = string.Empty;
                string board = "Time Card";
                string error = string.Empty;
                uint abi = 0;
                bool accessible = false;
                try
                {
                    using (TimeCardClient probe = new TimeCardClient(path))
                    {
                        accessible = true;
                        try
                        {
                            TimeCardCapabilities capabilities =
                                probe.GetCapabilities();
                            board = capabilities.BoardName;
                            abi = capabilities.AbiVersion;
                        }
                        catch (Exception ex)
                        {
                            error = "Capability probe: " + ex.Message;
                        }
                        try
                        {
                            TimeCardIdentity identity = probe.GetIdentity();
                            if (identity != null && identity.IsValid)
                                serial = identity.SerialNumber;
                        }
                        catch (Exception ex)
                        {
                            error = AppendProbeError(error,
                                "Identity probe: " + ex.Message);
                        }
                    }
                }
                catch (Exception ex)
                {
                    Win32Exception win32 = ex as Win32Exception;
                    if (legacyFallback && win32 != null &&
                        (win32.NativeErrorCode == 2 ||
                         win32.NativeErrorCode == 3))
                        continue;
                    error = ex.Message;
                }
                devices.Add(new TimeCardDeviceDescriptor(path, serial, board,
                    abi, accessible, error));
            }
            return devices;
        }

        private static string AppendProbeError(string current, string next)
        {
            return string.IsNullOrWhiteSpace(current) ? next :
                current + " " + next;
        }

        private static string ResolveDevicePath(uint deviceIndex)
        {
            IList<string> interfaces = EnumerateDevicePaths();
            if (deviceIndex < (uint)interfaces.Count)
                return interfaces[(int)deviceIndex];
            if (interfaces.Count == 0 && deviceIndex == 0u)
                return LegacyDevicePath; // ABI 1-13 compatibility.
            throw new Win32Exception(2, string.Format(
                System.Globalization.CultureInfo.InvariantCulture,
                "Time Card index {0} is not present ({1} interface(s) found).",
                deviceIndex, interfaces.Count));
        }

        public TimeCardSnapshot GetSnapshot()
        {
            TimeCardInfoRaw info = GetOutput<TimeCardInfoRaw>(IoctlGetInfo);
            ulong tickBefore = GetTickCount64();
            TimeCardCrossTimestampRaw timestamp =
                GetOutput<TimeCardCrossTimestampRaw>(IoctlGetCrossTimestamp);
            ulong tickAfter = GetTickCount64();
            ulong sampleTick = tickAfter >= tickBefore ?
                tickBefore + ((tickAfter - tickBefore) / 2ul) : tickBefore;
            TimeCardHierarchyRaw hierarchy = SetHierarchyRaw(0, false);
            return new TimeCardSnapshot(info, timestamp, hierarchy,
                sampleTick);
        }

        public void SetClockFromSystem()
        {
            DateTime utc = DateTime.UtcNow;
            DateTime epoch = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);
            long ticks = (utc - epoch).Ticks;
            SetClockUtc(ticks / TimeSpan.TicksPerSecond,
                checked((uint)((ticks % TimeSpan.TicksPerSecond) * 100L)));
        }

        public void SetClockUtc(long unixSeconds, uint nanoseconds)
        {
            if (unixSeconds < 0)
                throw new ArgumentOutOfRangeException("unixSeconds");
            if (nanoseconds >= 1000000000u)
                throw new ArgumentOutOfRangeException("nanoseconds");
            TimeCardTimeRaw value = new TimeCardTimeRaw
            {
                Seconds = checked((ulong)unixSeconds),
                Nanoseconds = nanoseconds,
                Reserved = 0
            };
            SendInput(IoctlSetTime, value);
        }

        public DateTime GetEstimatedClockTimeUtc()
        {
            TimeCardCrossTimestampRaw timestamp =
                GetOutput<TimeCardCrossTimestampRaw>(IoctlGetCrossTimestamp);
            DateTime epoch = new DateTime(1970, 1, 1, 0, 0, 0,
                DateTimeKind.Utc);
            long cardTicks = checked(
                (long)timestamp.CardTime.Seconds * TimeSpan.TicksPerSecond +
                timestamp.CardTime.Nanoseconds / 100);
            DateTime cardAtSample = epoch.AddTicks(cardTicks);
            DateTime systemBefore = DateTime.FromFileTimeUtc(
                (long)timestamp.SystemTimeBefore100ns);
            DateTime systemAfter = DateTime.FromFileTimeUtc(
                (long)timestamp.SystemTimeAfter100ns);
            DateTime systemAtSample = systemBefore.AddTicks(
                (systemAfter.Ticks - systemBefore.Ticks) / 2);
            TimeSpan sampleAge = DateTime.UtcNow - systemAtSample;
            if (sampleAge < TimeSpan.FromSeconds(-2) ||
                sampleAge > TimeSpan.FromSeconds(10))
                throw new InvalidOperationException(
                    "The PHC cross-timestamp sample is too old to set Windows safely.");
            return cardAtSample.Add(sampleAge);
        }

        public DateTime SetSystemClockFromTimeCard()
        {
            DateTime utc = GetEstimatedClockTimeUtc();
            if (utc.Year < 2020 || utc.Year > 2100)
                throw new InvalidOperationException(
                    "The Time Card PHC does not contain a plausible UTC date. " +
                    "Synchronize the PHC from the system clock or establish GNSS time first.");

            NativeSystemTime systemTime = new NativeSystemTime
            {
                Year = (ushort)utc.Year,
                Month = (ushort)utc.Month,
                DayOfWeek = (ushort)utc.DayOfWeek,
                Day = (ushort)utc.Day,
                Hour = (ushort)utc.Hour,
                Minute = (ushort)utc.Minute,
                Second = (ushort)utc.Second,
                Milliseconds = (ushort)utc.Millisecond
            };
            if (!SetSystemTime(ref systemTime))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Windows rejected the Time Card UTC value.");
            return utc;
        }

        public uint SetClockSource(uint source)
        {
            TimeCardClockSourceRaw request = new TimeCardClockSourceRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardClockSourceRaw)),
                Source = source
            };
            byte[] output = Call(IoctlClockSourceSet, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardClockSourceRaw)));
            return BytesToStruct<TimeCardClockSourceRaw>(output).ActiveSource;
        }

        public NmeaOutputState GetNmeaOutput()
        {
            return new NmeaOutputState(
                GetOutput<TimeCardNmeaControlRaw>(IoctlNmeaQuery));
        }

        public NmeaOutputState SetNmeaOutput(bool enabled, uint baud,
                                              bool inverted)
        {
            return SetNmeaOutputCore(enabled, baud, inverted, false, false,
                0, 0, 0, 0);
        }

        public NmeaOutputState SetNmeaOutput(bool enabled, uint baud,
                                              bool inverted, bool clearError)
        {
            return SetNmeaOutputCore(enabled, baud, inverted, false,
                clearError, 0, 0, 0, 0);
        }

        public NmeaOutputState SetNmeaOutput(bool enabled, uint baud,
            bool inverted, int correctionSeconds, int localOffsetMinutes,
            uint gnss, uint messageDisableMask)
        {
            return SetNmeaOutputCore(enabled, baud, inverted, true, false,
                correctionSeconds, localOffsetMinutes, gnss,
                messageDisableMask);
        }

        public NmeaOutputState SetNmeaOutput(bool enabled, uint baud,
            bool inverted, int correctionSeconds, int localOffsetMinutes,
            uint gnss, uint messageDisableMask, bool clearError)
        {
            return SetNmeaOutputCore(enabled, baud, inverted, true,
                clearError, correctionSeconds, localOffsetMinutes, gnss,
                messageDisableMask);
        }

        private NmeaOutputState SetNmeaOutputCore(bool enabled, uint baud,
            bool inverted, bool advancedValid, bool clearError,
            int correctionSeconds, int localOffsetMinutes, uint gnss,
            uint messageDisableMask)
        {
            TimeCardNmeaControlRaw request = new TimeCardNmeaControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardNmeaControlRaw)),
                Flags = (enabled ? 2u : 0u) | (advancedValid ? 4u : 0u) |
                    (clearError ? 0x80000000u : 0u),
                Baud = baud,
                Polarity = inverted ? 1u : 0u,
                CorrectionSeconds = correctionSeconds,
                LocalOffsetMinutes = localOffsetMinutes,
                Gnss = gnss,
                MessageDisableMask = messageDisableMask
            };
            byte[] output = Call(IoctlNmeaSet, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardNmeaControlRaw)));
            return new NmeaOutputState(
                BytesToStruct<TimeCardNmeaControlRaw>(output));
        }

        public TimeCardIdentity GetIdentity()
        {
            return new TimeCardIdentity(
                GetOutput<TimeCardIdentityRaw>(IoctlGetIdentity));
        }

        public SignalGeneratorState GetSignalGenerator(uint generator)
        {
            return CallSignalGenerator(IoctlSignalQuery, generator, false,
                0, 0, 0, false, 0, 0, false, 0, 0);
        }

        public SignalGeneratorState SetSignalGenerator(uint generator,
            bool enabled, ulong periodNanoseconds, ulong pulseNanoseconds,
            ulong phaseNanoseconds, bool activeHigh)
        {
            return CallSignalGenerator(IoctlSignalSet, generator, enabled,
                periodNanoseconds, pulseNanoseconds, phaseNanoseconds,
                activeHigh, 0, 0, false, 0, 0);
        }

        public SignalGeneratorState SetSignalGenerator(uint generator,
            bool enabled, ulong periodNanoseconds, ulong pulseNanoseconds,
            ulong phaseNanoseconds, bool activeHigh, uint repeatCount,
            uint cableDelayNanoseconds)
        {
            return CallSignalGenerator(IoctlSignalSet, generator, enabled,
                periodNanoseconds, pulseNanoseconds, phaseNanoseconds,
                activeHigh, repeatCount, cableDelayNanoseconds, false, 0, 0);
        }

        public SignalGeneratorState SetSignalGeneratorAt(uint generator,
            bool enabled, ulong periodNanoseconds, ulong pulseNanoseconds,
            bool activeHigh, uint repeatCount, uint cableDelayNanoseconds,
            ulong startSeconds, uint startNanoseconds)
        {
            if (startSeconds > uint.MaxValue)
                throw new ArgumentOutOfRangeException("startSeconds");
            if (startNanoseconds >= 1000000000u)
                throw new ArgumentOutOfRangeException("startNanoseconds");
            return CallSignalGenerator(IoctlSignalSet, generator, enabled,
                periodNanoseconds, pulseNanoseconds, 0, activeHigh,
                repeatCount, cableDelayNanoseconds, true, startSeconds,
                startNanoseconds);
        }

        public SignalGeneratorState ClearSignalGeneratorStatus(uint generator)
        {
            if (generator == 0 || generator > 4)
                throw new ArgumentOutOfRangeException("generator");
            TimeCardSignalControlRaw request = new TimeCardSignalControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardSignalControlRaw)),
                Generator = generator,
                Flags = 0x40u
            };
            byte[] output = Call(IoctlSignalSet, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardSignalControlRaw)));
            return new SignalGeneratorState(
                BytesToStruct<TimeCardSignalControlRaw>(output));
        }

        private SignalGeneratorState CallSignalGenerator(uint code,
            uint generator, bool enabled, ulong periodNanoseconds,
            ulong pulseNanoseconds, ulong phaseNanoseconds, bool activeHigh,
            uint repeatCount, uint cableDelayNanoseconds, bool absoluteStart,
            ulong startSeconds, uint startNanoseconds)
        {
            if (generator == 0 || generator > 4)
                throw new ArgumentOutOfRangeException("generator");
            TimeCardSignalControlRaw request = new TimeCardSignalControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardSignalControlRaw)),
                Generator = generator,
                Flags = (enabled ? 2u : 0u) | (activeHigh ? 4u : 0u) |
                    (absoluteStart ? 0x20u : 0u),
                RepeatCount = repeatCount,
                CableDelayNanoseconds = cableDelayNanoseconds,
                PeriodNanoseconds = periodNanoseconds,
                PulseNanoseconds = pulseNanoseconds,
                PhaseNanoseconds = phaseNanoseconds,
                StartSeconds = startSeconds,
                StartNanoseconds = startNanoseconds
            };
            byte[] output = Call(code, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardSignalControlRaw)));
            return new SignalGeneratorState(
                BytesToStruct<TimeCardSignalControlRaw>(output));
        }

        public FpgaCapabilities GetFpgaCapabilities()
        {
            return new FpgaCapabilities(
                GetOutput<TimeCardFpgaCapabilitiesRaw>(IoctlFpgaCapabilities));
        }

        public FpgaImageInfo GetFpgaImageInfo()
        {
            return new FpgaImageInfo(
                GetOutput<TimeCardFpgaImageInfoRaw>(IoctlFpgaImageQuery));
        }

        public ClockTelemetryState GetClockTelemetry()
        {
            return new ClockTelemetryState(
                GetOutput<TimeCardClockTelemetryRaw>(IoctlClockTelemetry));
        }

        public TimestampChannelState GetTimestampChannel(uint channel)
        {
            return CallTimestampChannel(IoctlTimestampQuery, channel, false,
                0u, 0u, false, false);
        }

        public TimestampChannelState SetTimestampChannel(uint channel,
            bool enabled, uint polarity, uint cableDelayNanoseconds,
            bool clearError, bool clearQueue)
        {
            return CallTimestampChannel(IoctlTimestampSet, channel, enabled,
                polarity, cableDelayNanoseconds, clearError, clearQueue);
        }

        private TimestampChannelState CallTimestampChannel(uint ioctl,
            uint channel, bool enabled, uint polarity,
            uint cableDelayNanoseconds, bool clearError, bool clearQueue)
        {
            if (channel >= 6u)
                throw new ArgumentOutOfRangeException("channel");
            if (polarity > 1u)
                throw new ArgumentOutOfRangeException("polarity");
            if (cableDelayNanoseconds > 0x7fffffffu)
                throw new ArgumentOutOfRangeException("cableDelayNanoseconds");
            TimeCardTimestampControlRaw request =
                new TimeCardTimestampControlRaw
                {
                    Size = (uint)Marshal.SizeOf(
                        typeof(TimeCardTimestampControlRaw)),
                    Channel = channel,
                    Flags = (enabled ? 2u : 0u) |
                        (clearError ? 0x40000000u : 0u) |
                        (clearQueue ? 0x80000000u : 0u),
                    Polarity = polarity,
                    CableDelayNanoseconds = cableDelayNanoseconds
                };
            byte[] output = Call(ioctl, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardTimestampControlRaw)));
            return new TimestampChannelState(
                BytesToStruct<TimeCardTimestampControlRaw>(output));
        }

        public TimestampEventBatch ReadTimestampEvents(uint channel,
                                                        uint maximumEvents)
        {
            if (channel >= 6u)
                throw new ArgumentOutOfRangeException("channel");
            if (maximumEvents == 0u || maximumEvents > 16u)
                throw new ArgumentOutOfRangeException("maximumEvents");
            int size = Marshal.SizeOf(typeof(TimeCardTimestampBatchRaw));
            byte[] input = new byte[size];
            Buffer.BlockCopy(BitConverter.GetBytes((uint)size), 0, input, 0, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(channel), 0, input, 4, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(maximumEvents), 0, input, 8, 4);
            byte[] output = Call(IoctlTimestampRead, input, size);
            return new TimestampEventBatch(
                BytesToStruct<TimeCardTimestampBatchRaw>(output));
        }

        public ClockAdjustmentState GetClockAdjustment()
        {
            return new ClockAdjustmentState(
                GetOutput<TimeCardClockAdjustmentRaw>(IoctlClockAdjustQuery));
        }

        public ClockAdjustmentState SetClockAdjustment(int offsetNanoseconds,
            uint offsetIntervalNanoseconds, long driftPpbQ16,
            uint driftIntervalNanoseconds, uint inSyncThresholdNanoseconds,
            uint applyFlags)
        {
            if ((applyFlags & ~0x00070000u) != 0u || applyFlags == 0u)
                throw new ArgumentOutOfRangeException("applyFlags");
            if (offsetNanoseconds < -1000000000 ||
                offsetNanoseconds > 1000000000)
                throw new ArgumentOutOfRangeException("offsetNanoseconds");
            if (driftPpbQ16 < -(1000000L << 16) ||
                driftPpbQ16 > (1000000L << 16))
                throw new ArgumentOutOfRangeException("driftPpbQ16");
            if ((applyFlags & 0x00010000u) != 0u &&
                offsetIntervalNanoseconds == 0u)
                throw new ArgumentOutOfRangeException(
                    "offsetIntervalNanoseconds");
            if ((applyFlags & 0x00020000u) != 0u &&
                driftIntervalNanoseconds == 0u)
                throw new ArgumentOutOfRangeException(
                    "driftIntervalNanoseconds");
            if (inSyncThresholdNanoseconds > 1000000000u)
                throw new ArgumentOutOfRangeException(
                    "inSyncThresholdNanoseconds");
            TimeCardClockAdjustmentRaw request =
                new TimeCardClockAdjustmentRaw
                {
                    Size = (uint)Marshal.SizeOf(
                        typeof(TimeCardClockAdjustmentRaw)),
                    Flags = applyFlags,
                    OffsetNanoseconds = offsetNanoseconds,
                    OffsetIntervalNanoseconds = offsetIntervalNanoseconds,
                    DriftPpbQ16 = driftPpbQ16,
                    DriftIntervalNanoseconds = driftIntervalNanoseconds,
                    InSyncThresholdNanoseconds = inSyncThresholdNanoseconds,
                    Reserved = new uint[7]
                };
            byte[] output = Call(IoctlClockAdjustSet,
                StructToBytes(request), Marshal.SizeOf(
                    typeof(TimeCardClockAdjustmentRaw)));
            return new ClockAdjustmentState(
                BytesToStruct<TimeCardClockAdjustmentRaw>(output));
        }

        public ClockAdvancedState GetClockAdvanced()
        {
            return new ClockAdvancedState(
                GetOutput<TimeCardClockAdvancedControlRaw>(
                    IoctlClockAdvancedQuery));
        }

        public ClockAdvancedState SetClockAdvanced(ClockAdvancedState value,
                                                    uint applyFlags)
        {
            if (value == null)
                throw new ArgumentNullException("value");
            if ((applyFlags & ~0x3fu) != 0u || applyFlags == 0u)
                throw new ArgumentOutOfRangeException("applyFlags");
            TimeCardClockAdvancedControlRaw request =
                new TimeCardClockAdvancedControlRaw
                {
                    Size = (uint)Marshal.SizeOf(
                        typeof(TimeCardClockAdvancedControlRaw)),
                    Control = value.Control,
                    ApplyFlags = applyFlags,
                    OffsetRateLimiter = value.OffsetRateLimiter,
                    DriftRateLimiterQ16 = value.DriftRateLimiterQ16,
                    AgingConfiguration = value.AgingConfiguration,
                    HoldoverConfiguration = value.HoldoverConfiguration,
                    OffsetOutlierFilter = value.OffsetOutlierFilter,
                    DriftOutlierFilter = value.DriftOutlierFilter,
                    ServoOffsetP = value.ServoOffsetP,
                    ServoOffsetI = value.ServoOffsetI,
                    ServoDriftP = value.ServoDriftP,
                    ServoDriftI = value.ServoDriftI,
                    Reserved = new uint[3]
                };
            byte[] output = Call(IoctlClockAdvancedSet,
                StructToBytes(request), Marshal.SizeOf(
                    typeof(TimeCardClockAdvancedControlRaw)));
            return new ClockAdvancedState(
                BytesToStruct<TimeCardClockAdvancedControlRaw>(output));
        }

        public ClockAdvancedState SetClockAdvanced(uint control,
            uint applyFlags, uint offsetRateLimiter,
            uint driftRateLimiterQ16, uint agingConfiguration,
            uint holdoverConfiguration, uint offsetOutlierFilter,
            uint driftOutlierFilter, uint servoOffsetP, uint servoOffsetI,
            uint servoDriftP, uint servoDriftI)
        {
            if ((applyFlags & ~0x3fu) != 0u || applyFlags == 0u)
                throw new ArgumentOutOfRangeException("applyFlags");
            if (servoOffsetP > 0xffffu || servoOffsetI > 0xffffu ||
                servoDriftP > 0xffffu || servoDriftI > 0xffffu)
                throw new ArgumentOutOfRangeException("servoOffsetP");
            TimeCardClockAdvancedControlRaw request =
                new TimeCardClockAdvancedControlRaw
                {
                    Size = (uint)Marshal.SizeOf(
                        typeof(TimeCardClockAdvancedControlRaw)),
                    Control = control,
                    ApplyFlags = applyFlags,
                    OffsetRateLimiter = offsetRateLimiter,
                    DriftRateLimiterQ16 = driftRateLimiterQ16,
                    AgingConfiguration = agingConfiguration,
                    HoldoverConfiguration = holdoverConfiguration,
                    OffsetOutlierFilter = offsetOutlierFilter,
                    DriftOutlierFilter = driftOutlierFilter,
                    ServoOffsetP = servoOffsetP,
                    ServoOffsetI = servoOffsetI,
                    ServoDriftP = servoDriftP,
                    ServoDriftI = servoDriftI,
                    Reserved = new uint[3]
                };
            byte[] output = Call(IoctlClockAdvancedSet,
                StructToBytes(request), Marshal.SizeOf(
                    typeof(TimeCardClockAdvancedControlRaw)));
            return new ClockAdvancedState(
                BytesToStruct<TimeCardClockAdvancedControlRaw>(output));
        }

        public CoreInventory GetCoreInventory()
        {
            return new CoreInventory(
                GetOutput<TimeCardCoreInventoryRaw>(IoctlCoreInventoryQuery));
        }

        public SignalCompletionBatch ReadSignalCompletionEvents(
            uint generator, uint maximumEvents)
        {
            if (generator == 0u || generator > 4u)
                throw new ArgumentOutOfRangeException("generator");
            if (maximumEvents == 0u || maximumEvents > 16u)
                throw new ArgumentOutOfRangeException("maximumEvents");
            int size = Marshal.SizeOf(typeof(TimeCardSignalEventBatchRaw));
            byte[] input = new byte[size];
            Buffer.BlockCopy(BitConverter.GetBytes((uint)size), 0, input, 0, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(generator), 0, input, 4, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(maximumEvents), 0, input, 8, 4);
            byte[] output = Call(IoctlSignalEventRead, input, size);
            return new SignalCompletionBatch(
                BytesToStruct<TimeCardSignalEventBatchRaw>(output));
        }

        public FpgaImageContract GetFpgaImageContract()
        {
            return new FpgaImageContract(
                GetOutput<TimeCardFpgaImageContractRaw>(
                    IoctlFpgaContractQuery));
        }

        public FpgaImageContract SetFpgaImageContract(uint rawImageVersion,
            uint capabilityFlags, uint boardProfile, uint layout)
        {
            if (rawImageVersion == 0u)
                throw new ArgumentOutOfRangeException("rawImageVersion");
            if ((capabilityFlags & ~0xfffu) != 0u)
                throw new ArgumentOutOfRangeException("capabilityFlags");
            if ((capabilityFlags & 0x10u) != 0u &&
                (capabilityFlags & 0x08u) == 0u)
                throw new ArgumentException(
                    "UTC writes require UTC read capability.",
                    "capabilityFlags");
            TimeCardFpgaImageContractRaw request =
                new TimeCardFpgaImageContractRaw
                {
                    Size = (uint)Marshal.SizeOf(
                        typeof(TimeCardFpgaImageContractRaw)),
                    AbiVersion = 15u,
                    RawImageVersion = rawImageVersion,
                    CapabilityFlags = capabilityFlags,
                    BoardProfile = boardProfile,
                    Layout = layout,
                    Acknowledgement = 0x54434d46u,
                    Reserved = new uint[7]
                };
            byte[] output = Call(IoctlFpgaContractSet,
                StructToBytes(request), Marshal.SizeOf(
                    typeof(TimeCardFpgaImageContractRaw)));
            return new FpgaImageContract(
                BytesToStruct<TimeCardFpgaImageContractRaw>(output));
        }

        public NmeaUtcState GetNmeaUtc()
        {
            return new NmeaUtcState(
                GetOutput<TimeCardNmeaUtcControlRaw>(IoctlNmeaUtcQuery));
        }

        public NmeaUtcState SetNmeaUtc(uint utcOffsetSeconds, bool leap61,
            bool leap59, bool offsetValid)
        {
            if (utcOffsetSeconds > 0xffffu)
                throw new ArgumentOutOfRangeException("utcOffsetSeconds");
            TimeCardNmeaUtcControlRaw request =
                new TimeCardNmeaUtcControlRaw
                {
                    Size = (uint)Marshal.SizeOf(
                        typeof(TimeCardNmeaUtcControlRaw)),
                    Flags = (leap61 ? 8u : 0u) | (leap59 ? 0x10u : 0u) |
                        (offsetValid ? 0x20u : 0u),
                    UtcOffsetSeconds = utcOffsetSeconds,
                    Reserved = new uint[10]
                };
            byte[] output = Call(IoctlNmeaUtcSet, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardNmeaUtcControlRaw)));
            return new NmeaUtcState(
                BytesToStruct<TimeCardNmeaUtcControlRaw>(output));
        }

        public PpsEngineState GetPpsEngine(uint core)
        {
            return CallPpsEngine(IoctlPpsQuery, core, false, false, 0, 0,
                false);
        }

        public PpsEngineState SetPpsEngine(uint core, bool enabled,
            bool activeHigh, uint pulseWidthMilliseconds,
            int cableDelayNanoseconds, bool clearErrors)
        {
            return CallPpsEngine(IoctlPpsSet, core, enabled, activeHigh,
                pulseWidthMilliseconds, cableDelayNanoseconds, clearErrors);
        }

        private PpsEngineState CallPpsEngine(uint code, uint core,
            bool enabled, bool activeHigh, uint pulseWidthMilliseconds,
            int cableDelayNanoseconds, bool clearErrors)
        {
            if (core < 1 || core > 2)
                throw new ArgumentOutOfRangeException("core");
            TimeCardPpsControlRaw request = new TimeCardPpsControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardPpsControlRaw)),
                Core = core,
                Flags = (enabled ? 2u : 0u) |
                    (clearErrors ? 0x80000000u : 0u),
                Polarity = activeHigh ? 1u : 0u,
                PulseWidthMilliseconds = pulseWidthMilliseconds,
                CableDelayNanoseconds = cableDelayNanoseconds
            };
            byte[] output = Call(code, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardPpsControlRaw)));
            return new PpsEngineState(
                BytesToStruct<TimeCardPpsControlRaw>(output));
        }

        public TimecodeEngineState GetTimecodeEngine(uint format, uint role)
        {
            return CallTimecodeEngine(IoctlTimecodeQuery, format, role,
                false, 0, 0, 0, 0, 0, false, 0u, 0u);
        }

        public TimecodeEngineState SetTimecodeEngine(uint format, uint role,
            bool enabled, uint mode, uint codeValue, int correctionSeconds,
            int delayNanoseconds, uint controlBits, bool clearErrors)
        {
            return CallTimecodeEngine(IoctlTimecodeSet, format, role,
                enabled, mode, codeValue, correctionSeconds,
                delayNanoseconds, controlBits, clearErrors, 0u, 0u);
        }

        public TimecodeEngineState SetTimecodeEngine(uint format, uint role,
            bool enabled, uint mode, uint codeValue, int correctionSeconds,
            int delayNanoseconds, uint controlBits, bool clearErrors,
            bool amplitudeModulation, uint manualYear)
        {
            if (manualYear != 0u &&
                (manualYear < 1970u || manualYear > 2069u))
                throw new ArgumentOutOfRangeException("manualYear");
            return CallTimecodeEngine(IoctlTimecodeSet, format, role,
                enabled, mode, codeValue, correctionSeconds,
                delayNanoseconds, controlBits, clearErrors,
                amplitudeModulation ? 1u : 0u, manualYear);
        }

        private TimecodeEngineState CallTimecodeEngine(uint ioctl,
            uint format, uint role, bool enabled, uint mode, uint codeValue,
            int correctionSeconds, int delayNanoseconds, uint controlBits,
            bool clearErrors, uint amplitudeModulation, uint manualYear)
        {
            if (format < 1 || format > 2)
                throw new ArgumentOutOfRangeException("format");
            if (role < 1 || role > 2)
                throw new ArgumentOutOfRangeException("role");
            TimeCardTimecodeControlRaw request = new TimeCardTimecodeControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardTimecodeControlRaw)),
                Format = format,
                Role = role,
                Flags = (enabled ? 2u : 0u) |
                    (clearErrors ? 0x80000000u : 0u),
                Mode = mode,
                Code = codeValue,
                CorrectionSeconds = correctionSeconds,
                DelayNanoseconds = delayNanoseconds,
                ControlBits = controlBits,
                AmplitudeModulation = amplitudeModulation,
                ManualYear = manualYear
            };
            byte[] output = Call(ioctl, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardTimecodeControlRaw)));
            return new TimecodeEngineState(
                BytesToStruct<TimeCardTimecodeControlRaw>(output));
        }

        public TodParserState GetTodParser()
        {
            return CallTodParser(IoctlTodQuery, false, 0, 0, 0, false, 0,
                0, false);
        }

        public TodParserState SetTodParser(bool enabled, uint protocol,
            uint gnss, uint baud, bool inverted, int correctionSeconds,
            uint messageDisableMask, bool clearErrors)
        {
            return CallTodParser(IoctlTodSet, enabled, protocol, gnss, baud,
                inverted, correctionSeconds, messageDisableMask,
                clearErrors);
        }

        private TodParserState CallTodParser(uint ioctl, bool enabled,
            uint protocol, uint gnss, uint baud, bool inverted,
            int correctionSeconds, uint messageDisableMask, bool clearErrors)
        {
            TimeCardTodControlRaw request = new TimeCardTodControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardTodControlRaw)),
                Flags = (enabled ? 2u : 0u) |
                    (clearErrors ? 0x80000000u : 0u),
                Protocol = protocol,
                Gnss = gnss,
                Baud = baud,
                Polarity = inverted ? 1u : 0u,
                CorrectionSeconds = correctionSeconds,
                MessageDisableMask = messageDisableMask
            };
            byte[] output = Call(ioctl, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardTodControlRaw)));
            return new TodParserState(
                BytesToStruct<TimeCardTodControlRaw>(output));
        }

        public FrequencyCounterState GetFrequencyCounter(uint counter)
        {
            return CallFrequencyCounter(IoctlFrequencyQuery, counter, 0);
        }

        public FrequencyCounterState SetFrequencyCounter(uint counter,
                                                          uint integrationSeconds)
        {
            if (integrationSeconds > 255)
                throw new ArgumentOutOfRangeException("integrationSeconds");
            return CallFrequencyCounter(IoctlFrequencySet, counter,
                integrationSeconds);
        }

        private FrequencyCounterState CallFrequencyCounter(uint code,
            uint counter, uint integrationSeconds)
        {
            if (counter == 0 || counter > 4)
                throw new ArgumentOutOfRangeException("counter");
            TimeCardFrequencyControlRaw request = new TimeCardFrequencyControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardFrequencyControlRaw)),
                Counter = counter,
                IntegrationSeconds = integrationSeconds
            };
            byte[] output = Call(code, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardFrequencyControlRaw)));
            return new FrequencyCounterState(
                BytesToStruct<TimeCardFrequencyControlRaw>(output));
        }

        public FlashDeviceStatus GetFlashStatus()
        {
            return new FlashDeviceStatus(
                GetOutput<TimeCardFlashStatusRaw>(IoctlFlashQuery));
        }

        public byte[] ReadFlash(uint offset, uint length)
        {
            if (length == 0 || length > MaximumFlashTransfer)
                throw new ArgumentOutOfRangeException("length");
            TimeCardFlashRangeRaw request = new TimeCardFlashRangeRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardFlashRangeRaw)),
                Offset = offset,
                Length = length
            };
            byte[] output = Call(IoctlFlashRead, StructToBytes(request),
                FlashTransferBufferSize);
            if (output.Length < FlashTransferHeaderSize)
                throw new InvalidOperationException("The driver returned a truncated flash response.");
            uint returnedOffset = BitConverter.ToUInt32(output, 4);
            uint returnedLength = BitConverter.ToUInt32(output, 8);
            if (returnedOffset != offset || returnedLength != length ||
                output.Length < FlashTransferHeaderSize + returnedLength)
                throw new InvalidOperationException("The driver returned an invalid flash range.");
            byte[] data = new byte[returnedLength];
            Buffer.BlockCopy(output, FlashTransferHeaderSize, data, 0,
                (int)returnedLength);
            return data;
        }

        public void EraseFlashSector(uint offset, uint eraseSize)
        {
            if (eraseSize == 0)
                throw new ArgumentOutOfRangeException("eraseSize");
            TimeCardFlashRangeRaw request = new TimeCardFlashRangeRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardFlashRangeRaw)),
                Offset = offset,
                Length = eraseSize
            };
            Call(IoctlFlashErase, StructToBytes(request), 16);
        }

        public void ProgramFlashPage(uint offset, byte[] data)
        {
            if (data == null)
                throw new ArgumentNullException("data");
            if (data.Length == 0 || data.Length > MaximumFlashTransfer)
                throw new ArgumentOutOfRangeException("data");
            byte[] input = new byte[FlashTransferHeaderSize + data.Length];
            Buffer.BlockCopy(BitConverter.GetBytes((uint)input.Length), 0, input, 0, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(offset), 0, input, 4, 4);
            Buffer.BlockCopy(BitConverter.GetBytes((uint)data.Length), 0, input, 8, 4);
            Buffer.BlockCopy(data, 0, input, FlashTransferHeaderSize, data.Length);
            Call(IoctlFlashProgram, input, 16);
        }

        public UartObservation ObserveUart(uint port, uint timeoutMilliseconds)
        {
            if (port > 3)
                throw new ArgumentOutOfRangeException("port");
            TimeCardUartObserveRaw request = new TimeCardUartObserveRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardUartObserveRaw)),
                Port = port,
                TimeoutMilliseconds = Math.Min(timeoutMilliseconds, 5000u)
            };
            byte[] output = Call(IoctlUartObserve, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardUartObserveRaw)));
            return new UartObservation(BytesToStruct<TimeCardUartObserveRaw>(output));
        }

        public void ConfigureUart(uint port, uint baud)
        {
            if (port > 3)
                throw new ArgumentOutOfRangeException("port");
            if (baud == 0)
                throw new ArgumentOutOfRangeException("baud");
            SendInput(IoctlUartConfigure, new TimeCardUartConfigRaw { Port = port, Baud = baud });
        }

        public UartReadResult ReadUart(uint port, uint maximumBytes, uint timeoutMilliseconds)
        {
            if (port > 3)
                throw new ArgumentOutOfRangeException("port");
            maximumBytes = Math.Min(maximumBytes, MaximumUartTransfer);
            timeoutMilliseconds = Math.Min(timeoutMilliseconds, 5000);
            TimeCardUartReadRequestRaw request = new TimeCardUartReadRequestRaw
            {
                Port = port,
                MaximumBytes = maximumBytes,
                TimeoutMilliseconds = timeoutMilliseconds,
                Reserved = 0
            };
            byte[] output;
            try
            {
                output = Call(IoctlUartRead, StructToBytes(request), 16 + MaximumUartTransfer);
            }
            catch (Win32Exception ex)
            {
                // The KMDF driver maps STATUS_IO_TIMEOUT to ERROR_SEM_TIMEOUT
                // when no byte arrives. An idle UART is normal for a monitor.
                if (ex.NativeErrorCode == 121 || ex.NativeErrorCode == 1460)
                    return new UartReadResult(new byte[0], 0);
                throw;
            }
            uint length = BitConverter.ToUInt32(output, 4);
            uint lineStatus = BitConverter.ToUInt32(output, 12);
            if (length > MaximumUartTransfer)
                throw new InvalidOperationException("The driver returned an invalid UART length.");
            byte[] data = new byte[length];
            Buffer.BlockCopy(output, 16, data, 0, (int)length);
            return new UartReadResult(data, lineStatus);
        }

        public UartWriteResult WriteUart(uint port, byte[] data, uint timeoutMilliseconds)
        {
            if (port > 3)
                throw new ArgumentOutOfRangeException("port");
            if (data == null)
                throw new ArgumentNullException("data");
            if (data.Length > MaximumUartTransfer)
                throw new ArgumentOutOfRangeException("data", "UART writes are limited to 256 bytes.");

            byte[] input = new byte[16 + data.Length];
            Buffer.BlockCopy(BitConverter.GetBytes(port), 0, input, 0, 4);
            Buffer.BlockCopy(BitConverter.GetBytes((uint)data.Length), 0, input, 4, 4);
            Buffer.BlockCopy(BitConverter.GetBytes(Math.Min(timeoutMilliseconds, 5000)), 0, input, 8, 4);
            Buffer.BlockCopy(data, 0, input, 16, data.Length);
            byte[] output = Call(IoctlUartWrite, input, 8);
            return new UartWriteResult(BitConverter.ToUInt32(output, 0),
                BitConverter.ToUInt32(output, 4));
        }

        internal string ExecuteSa53Command(string command, uint timeoutMilliseconds)
        {
            if (string.IsNullOrWhiteSpace(command))
                throw new ArgumentException("An SA53 command is required.", "command");
            if (command.Length > 220)
                throw new ArgumentOutOfRangeException("command", "SA53 commands are limited to 220 characters.");
            foreach (char value in command)
            {
                if (value < 0x20 || value > 0x7e || value == '{' || value == '}' ||
                    value == '[' || value == ']')
                    throw new ArgumentException("The SA53 command contains an unsupported character.", "command");
            }

            timeoutMilliseconds = Math.Max(100u, Math.Min(timeoutMilliseconds, 5000u));
            lock (gate)
            {
                // MAC-SA53 C3 defaults to 57,600 baud, 8N1, no flow control.
                ConfigureUart(2, 57600);

                // Discard stale announcements or bytes left by a previous console session.
                for (int pass = 0; pass < 8; pass++)
                {
                    UartReadResult stale = ReadUart(2, MaximumUartTransfer, 10);
                    if (stale.Data.Length == 0)
                        break;
                }

                byte[] request = Encoding.ASCII.GetBytes("{" + command + "}");
                UartWriteResult written = WriteUart(2, request, timeoutMilliseconds);
                if (written.BytesTransferred != request.Length)
                    throw new InvalidOperationException("The complete SA53 command was not transmitted.");

                Stopwatch timer = Stopwatch.StartNew();
                StringBuilder received = new StringBuilder();
                while (timer.ElapsedMilliseconds < timeoutMilliseconds && received.Length < 8192)
                {
                    uint remaining = (uint)Math.Max(1L,
                        (long)timeoutMilliseconds - timer.ElapsedMilliseconds);
                    UartReadResult result = ReadUart(2, MaximumUartTransfer,
                        Math.Min(remaining, 100u));
                    if (result.Data.Length == 0)
                        continue;
                    received.Append(Encoding.ASCII.GetString(result.Data));

                    int scan = 0;
                    while (scan < received.Length)
                    {
                        int start = received.ToString().IndexOf('[', scan);
                        if (start < 0)
                            break;
                        int end = received.ToString().IndexOf(']', start + 1);
                        if (end < 0)
                            break;
                        string frame = received.ToString(start + 1, end - start - 1);
                        scan = end + 1;
                        if (frame.StartsWith(">", StringComparison.Ordinal))
                            continue;
                        if (frame.StartsWith("!", StringComparison.Ordinal))
                            throw new InvalidOperationException("The SA53 rejected '" + command +
                                "': " + StripSa53Checksum(frame.Substring(1)) + ".");
                        if (frame.StartsWith("=", StringComparison.Ordinal))
                            return StripSa53Checksum(frame.Substring(1));
                    }
                }

                throw new TimeoutException("The SA53 did not answer '" + command +
                    "' within " + timeoutMilliseconds + " ms.");
            }
        }

        private static string StripSa53Checksum(string value)
        {
            int checksum = value.LastIndexOf('|');
            return checksum < 0 ? value.Trim() : value.Substring(0, checksum).Trim();
        }

        internal byte[] ExecuteUbxPoll(uint port, uint baud, byte messageClass,
            byte messageId, byte[] payload, uint timeoutMilliseconds)
        {
            return ExecuteUbxExchange(port, baud, messageClass, messageId,
                payload, messageClass, messageId, false, timeoutMilliseconds);
        }

        internal void ExecuteUbxSet(uint port, uint baud, byte messageClass,
            byte messageId, byte[] payload, uint timeoutMilliseconds)
        {
            ExecuteUbxExchange(port, baud, messageClass, messageId,
                payload, 0, 0, true, timeoutMilliseconds);
        }

        internal IDictionary<ushort, byte[]> CaptureUbxMessages(uint port,
            uint baud, uint durationMilliseconds)
        {
            return CaptureUbxMessagesCore(port, baud, true,
                durationMilliseconds);
        }

        internal IDictionary<ushort, byte[]> CaptureUbxMessagesPreserveBaud(
            uint port, uint durationMilliseconds)
        {
            return CaptureUbxMessagesCore(port, 0u, false,
                durationMilliseconds);
        }

        private IDictionary<ushort, byte[]> CaptureUbxMessagesCore(uint port,
            uint baud, bool configureBaud, uint durationMilliseconds)
        {
            if (port > 1)
                throw new ArgumentOutOfRangeException("port",
                    "GNSS receivers use UART 0 or UART 1.");
            if (configureBaud && baud == 0)
                throw new ArgumentOutOfRangeException("baud");
            durationMilliseconds = Math.Max(250u,
                Math.Min(durationMilliseconds, 5000u));

            lock (gate)
            {
                if (configureBaud)
                    ConfigureUart(port, baud);
                Stopwatch timer = Stopwatch.StartNew();
                List<byte> received = new List<byte>();
                Dictionary<ushort, byte[]> messages =
                    new Dictionary<ushort, byte[]>();
                while (timer.ElapsedMilliseconds < durationMilliseconds &&
                    received.Count < 65536)
                {
                    uint remaining = (uint)Math.Max(1L,
                        (long)durationMilliseconds - timer.ElapsedMilliseconds);
                    UartReadResult result = ReadUart(port, MaximumUartTransfer,
                        Math.Min(remaining, 1u));
                    if (result.Data.Length != 0)
                        received.AddRange(result.Data);

                    byte frameClass;
                    byte frameId;
                    byte[] framePayload;
                    while (TryTakeUbxFrame(received, out frameClass, out frameId,
                        out framePayload))
                    {
                        messages[(ushort)((frameClass << 8) | frameId)] =
                            framePayload;
                    }
                }
                return messages;
            }
        }

        private byte[] ExecuteUbxExchange(uint port, uint baud,
            byte messageClass, byte messageId, byte[] payload,
            byte responseClass, byte responseId, bool expectAcknowledgement,
            uint timeoutMilliseconds)
        {
            if (port > 1)
                throw new ArgumentOutOfRangeException("port", "GNSS receivers use UART 0 or UART 1.");
            if (baud == 0)
                throw new ArgumentOutOfRangeException("baud");
            if (payload == null)
                payload = new byte[0];
            if (payload.Length > MaximumUartTransfer - 8)
                throw new ArgumentOutOfRangeException("payload", "UBX requests are limited to 248 payload bytes.");
            timeoutMilliseconds = Math.Max(100u, Math.Min(timeoutMilliseconds, 5000u));

            lock (gate)
            {
                ConfigureUart(port, baud);
                for (int pass = 0; pass < 12; pass++)
                {
                    UartReadResult stale = ReadUart(port, MaximumUartTransfer, 5);
                    if (stale.Data.Length == 0)
                        break;
                }

                byte[] request = BuildUbxFrame(messageClass, messageId, payload);
                UartWriteResult written = WriteUart(port, request, timeoutMilliseconds);
                if (written.BytesTransferred != request.Length)
                    throw new InvalidOperationException("The complete UBX request was not transmitted.");

                Stopwatch timer = Stopwatch.StartNew();
                List<byte> received = new List<byte>();
                while (timer.ElapsedMilliseconds < timeoutMilliseconds && received.Count < 16384)
                {
                    uint remaining = (uint)Math.Max(1L,
                        (long)timeoutMilliseconds - timer.ElapsedMilliseconds);
                    UartReadResult result = ReadUart(port, MaximumUartTransfer,
                        Math.Min(remaining, 1u));
                    if (result.Data.Length != 0)
                        received.AddRange(result.Data);

                    byte frameClass;
                    byte frameId;
                    byte[] framePayload;
                    while (TryTakeUbxFrame(received, out frameClass, out frameId,
                        out framePayload))
                    {
                        if (frameClass == 0x05 && framePayload.Length >= 2 &&
                            framePayload[0] == messageClass && framePayload[1] == messageId)
                        {
                            if (frameId == 0x00)
                                throw new InvalidOperationException(string.Format(
                                    "The u-blox receiver rejected UBX-{0:X2}-{1:X2}.",
                                    messageClass, messageId));
                            if (frameId == 0x01 && expectAcknowledgement)
                                return new byte[0];
                        }
                        if (!expectAcknowledgement && frameClass == responseClass &&
                            frameId == responseId)
                            return framePayload;
                    }
                }

                throw new TimeoutException(string.Format(
                    "The u-blox receiver did not answer UBX-{0:X2}-{1:X2} within {2} ms.",
                    messageClass, messageId, timeoutMilliseconds));
            }
        }

        private static byte[] BuildUbxFrame(byte messageClass, byte messageId,
            byte[] payload)
        {
            byte[] frame = new byte[payload.Length + 8];
            frame[0] = 0xB5;
            frame[1] = 0x62;
            frame[2] = messageClass;
            frame[3] = messageId;
            frame[4] = (byte)(payload.Length & 0xff);
            frame[5] = (byte)((payload.Length >> 8) & 0xff);
            Buffer.BlockCopy(payload, 0, frame, 6, payload.Length);
            byte checksumA = 0;
            byte checksumB = 0;
            for (int index = 2; index < payload.Length + 6; index++)
            {
                unchecked
                {
                    checksumA += frame[index];
                    checksumB += checksumA;
                }
            }
            frame[frame.Length - 2] = checksumA;
            frame[frame.Length - 1] = checksumB;
            return frame;
        }

        private static bool TryTakeUbxFrame(List<byte> data, out byte messageClass,
            out byte messageId, out byte[] payload)
        {
            messageClass = 0;
            messageId = 0;
            payload = null;
            while (true)
            {
                int start = -1;
                for (int index = 0; index + 1 < data.Count; index++)
                {
                    if (data[index] == 0xB5 && data[index + 1] == 0x62)
                    {
                        start = index;
                        break;
                    }
                }
                if (start < 0)
                {
                    if (data.Count > 0 && data[data.Count - 1] == 0xB5)
                        data.RemoveRange(0, data.Count - 1);
                    else
                        data.Clear();
                    return false;
                }
                if (start > 0)
                    data.RemoveRange(0, start);
                if (data.Count < 8)
                    return false;

                int payloadLength = data[4] | (data[5] << 8);
                if (payloadLength > 4096)
                {
                    data.RemoveAt(0);
                    continue;
                }
                int frameLength = payloadLength + 8;
                if (data.Count < frameLength)
                    return false;

                byte checksumA = 0;
                byte checksumB = 0;
                for (int index = 2; index < payloadLength + 6; index++)
                {
                    unchecked
                    {
                        checksumA += data[index];
                        checksumB += checksumA;
                    }
                }
                if (checksumA != data[payloadLength + 6] ||
                    checksumB != data[payloadLength + 7])
                {
                    data.RemoveAt(0);
                    continue;
                }

                messageClass = data[2];
                messageId = data[3];
                payload = new byte[payloadLength];
                data.CopyTo(6, payload, 0, payloadLength);
                data.RemoveRange(0, frameLength);
                return true;
            }
        }

        public void ControlHierarchy(uint action, bool persist)
        {
            SetHierarchyRaw(action, persist);
        }

        public SmaConnectorState GetSmaConnector(uint connector)
        {
            return CallSma(IoctlSmaQuery, connector, SmaDirection.Input, 0);
        }

        public SmaConnectorState SetSmaConnector(uint connector,
                                                 SmaDirection direction,
                                                 uint function)
        {
            return CallSma(IoctlSmaSet, connector, direction, function);
        }

        private SmaConnectorState CallSma(uint code, uint connector,
                                          SmaDirection direction,
                                          uint function)
        {
            if (connector == 0 || connector > 4)
                throw new ArgumentOutOfRangeException("connector");
            TimeCardSmaControlRaw request = new TimeCardSmaControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardSmaControlRaw)),
                Connector = connector,
                Direction = (uint)direction,
                Function = function
            };
            byte[] output = Call(code, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardSmaControlRaw)));
            return new SmaConnectorState(BytesToStruct<TimeCardSmaControlRaw>(output));
        }

        public I2cControllerStatus GetI2cStatus()
        {
            return new I2cControllerStatus(
                GetOutput<TimeCardI2cStatusRaw>(IoctlI2cGetStatus));
        }

        public I2cProbeResult ProbeI2c(uint address)
        {
            if (address < 0x08 || address > 0x77)
                throw new ArgumentOutOfRangeException("address");
            TimeCardI2cProbeRaw request = new TimeCardI2cProbeRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardI2cProbeRaw)),
                Address = address
            };
            byte[] output = Call(IoctlI2cProbe, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardI2cProbeRaw)));
            return new I2cProbeResult(BytesToStruct<TimeCardI2cProbeRaw>(output));
        }

        public I2cReadResult ReadI2c(uint address, uint subaddress,
                                     uint subaddressLength, uint length,
                                     uint timeoutMilliseconds)
        {
            if (address < 0x08 || address > 0x77)
                throw new ArgumentOutOfRangeException("address");
            if (subaddressLength > 2)
                throw new ArgumentOutOfRangeException("subaddressLength");
            if (length == 0 || length > MaximumI2cTransfer)
                throw new ArgumentOutOfRangeException("length");

            TimeCardI2cReadRequestRaw request = new TimeCardI2cReadRequestRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardI2cReadRequestRaw)),
                Address = address,
                SubaddressLength = subaddressLength,
                Subaddress = subaddress,
                Length = length,
                TimeoutMilliseconds = Math.Min(timeoutMilliseconds, 250u)
            };
            byte[] output = Call(IoctlI2cRead, StructToBytes(request),
                I2cTransferBufferSize);
            if (output.Length < I2cTransferHeaderSize)
                throw new InvalidOperationException("The driver returned a truncated I2C response.");
            uint returnedAddress = BitConverter.ToUInt32(output, 4);
            uint returnedLength = BitConverter.ToUInt32(output, 8);
            uint controllerStatus = BitConverter.ToUInt32(output, 12);
            uint interruptStatus = BitConverter.ToUInt32(output, 16);
            if (returnedLength > MaximumI2cTransfer ||
                output.Length < I2cTransferHeaderSize + returnedLength)
                throw new InvalidOperationException("The driver returned an invalid I2C length.");
            byte[] data = new byte[returnedLength];
            Buffer.BlockCopy(output, I2cTransferHeaderSize, data, 0,
                (int)returnedLength);
            return new I2cReadResult(returnedAddress, data,
                controllerStatus, interruptStatus);
        }

        public I2cMuxState GetI2cMux()
        {
            return new I2cMuxState(
                GetOutput<TimeCardI2cMuxControlRaw>(IoctlI2cMuxQuery));
        }

        public I2cMuxState SetI2cMux(uint channelMask)
        {
            if ((channelMask & ~0x0fu) != 0)
                throw new ArgumentOutOfRangeException("channelMask");
            TimeCardI2cMuxControlRaw request = new TimeCardI2cMuxControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardI2cMuxControlRaw)),
                ChannelMask = channelMask
            };
            byte[] output = Call(IoctlI2cMuxSet, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardI2cMuxControlRaw)));
            return new I2cMuxState(
                BytesToStruct<TimeCardI2cMuxControlRaw>(output));
        }

        public BoardLedState GetBoardLed(uint led)
        {
            if (led >= 6)
                throw new ArgumentOutOfRangeException("led");
            TimeCardLedControlRaw request = new TimeCardLedControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardLedControlRaw)),
                Led = led
            };
            byte[] output = Call(IoctlLedQuery, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardLedControlRaw)));
            return new BoardLedState(BytesToStruct<TimeCardLedControlRaw>(output));
        }

        public BoardLedState SetBoardLed(uint led, byte red, byte green,
                                         byte blue, byte globalCurrent)
        {
            return SetBoardLed(led, red, green, blue, globalCurrent, false);
        }

        public BoardLedState SetBoardLed(uint led, byte red, byte green,
                                         byte blue, byte globalCurrent,
                                         bool dcTest)
        {
            return SetBoardLed(led, red, green, blue, globalCurrent,
                dcTest, false);
        }

        public BoardLedState SetBoardLed(uint led, byte red, byte green,
                                         byte blue, byte globalCurrent,
                                         bool dcTest, bool resetTest)
        {
            if (led >= 6)
                throw new ArgumentOutOfRangeException("led");
            if (globalCurrent == 0 || globalCurrent > 128)
                throw new ArgumentOutOfRangeException("globalCurrent");
            TimeCardLedControlRaw request = new TimeCardLedControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardLedControlRaw)),
                Led = led,
                Red = red,
                Green = green,
                Blue = blue,
                GlobalCurrent = globalCurrent,
                Flags = (dcTest ? 8u : 0u) | (resetTest ? 16u : 0u)
            };
            byte[] output = Call(IoctlLedSet, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardLedControlRaw)));
            return new BoardLedState(BytesToStruct<TimeCardLedControlRaw>(output));
        }

        public SensorTelemetrySnapshot GetSensorTelemetry()
        {
            return new SensorTelemetrySnapshot(
                GetOutput<TimeCardSensorTelemetryRaw>(IoctlSensorQuery));
        }

        public Mro50Status GetMro50Status()
        {
            return new Mro50Status(
                GetOutput<TimeCardMro50StatusRaw>(IoctlMro50Query));
        }

        public Mro50Status SetMro50FineAdjustment(uint value)
        {
            if (value > 4800u)
                throw new ArgumentOutOfRangeException("value");
            return ControlMro50(1, value);
        }

        public Mro50Status SetMro50CoarseAdjustment(uint value)
        {
            if (value > 0x003fffffu)
                throw new ArgumentOutOfRangeException("value");
            return ControlMro50(2, value);
        }

        public Mro50Status SaveMro50CoarseAdjustment()
        {
            return ControlMro50(3, 0);
        }

        public Mro50Status SetMro50SerialRoute(bool enabled)
        {
            return ControlMro50(4, enabled ? 1u : 0u);
        }

        public TimeCardCapabilities GetCapabilities()
        {
            return new TimeCardCapabilities(
                GetOutput<TimeCardCapabilitiesRaw>(IoctlGetCapabilities));
        }

        public DisciplineLeaseState GetDisciplineLease()
        {
            return ControlDisciplineLease(0u);
        }

        public DisciplineLeaseState AcquireDisciplineLease()
        {
            DisciplineLeaseState state = ControlDisciplineLease(1u);
            if (!state.IsOwner)
                throw new InvalidOperationException(
                    "The driver did not grant oscillator discipline ownership.");
            return state;
        }

        public DisciplineLeaseState ReleaseDisciplineLease()
        {
            return ControlDisciplineLease(2u);
        }

        private DisciplineLeaseState ControlDisciplineLease(uint action)
        {
            TimeCardDisciplineLeaseRaw request =
                new TimeCardDisciplineLeaseRaw
                {
                    Size = (uint)Marshal.SizeOf(
                        typeof(TimeCardDisciplineLeaseRaw)),
                    Action = action,
                    Reserved = new ulong[2]
                };
            byte[] output = Call(IoctlDisciplineLease,
                StructToBytes(request), Marshal.SizeOf(
                    typeof(TimeCardDisciplineLeaseRaw)));
            return new DisciplineLeaseState(
                BytesToStruct<TimeCardDisciplineLeaseRaw>(output));
        }

        public TimeCardPhaseSample GetPhaseSample()
        {
            return new TimeCardPhaseSample(
                GetOutput<TimeCardPhaseSampleRaw>(IoctlPhaseQuery));
        }

        public bool SetPhaseMeter(bool enabled, bool referenceFalling,
                                  bool oscillatorFalling)
        {
            TimeCardPhaseControlRaw request = new TimeCardPhaseControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardPhaseControlRaw)),
                Action = enabled ? 1u : 0u,
                ReferencePolarity = referenceFalling ? 1u : 0u,
                OscillatorPolarity = oscillatorFalling ? 1u : 0u
            };
            byte[] output = Call(IoctlPhaseControl, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardPhaseControlRaw)));
            return BytesToStruct<TimeCardPhaseControlRaw>(output).Enabled != 0u;
        }

        public DateTime AdjustPhc(long offsetNanoseconds)
        {
            if (offsetNanoseconds < -499999999L ||
                offsetNanoseconds > 499999999L)
                throw new ArgumentOutOfRangeException("offsetNanoseconds");
            TimeCardPhcAdjustRaw request = new TimeCardPhcAdjustRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardPhcAdjustRaw)),
                OffsetNanoseconds = offsetNanoseconds
            };
            byte[] output = Call(IoctlPhcAdjust, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardPhcAdjustRaw)));
            TimeCardPhcAdjustRaw result =
                BytesToStruct<TimeCardPhcAdjustRaw>(output);
            DateTime epoch = new DateTime(1970, 1, 1, 0, 0, 0,
                DateTimeKind.Utc);
            return epoch.AddSeconds(result.ResultingTime.Seconds)
                .AddTicks(result.ResultingTime.Nanoseconds / 100);
        }

        public TimeCardDisciplineParameters ReadDisciplineParameters()
        {
            return new TimeCardDisciplineParameters(
                GetOutput<TimeCardDisciplineBlobRaw>(IoctlDisciplineRead));
        }

        public TimeCardDisciplineParameters WriteDisciplineParameters(
            byte[] data)
        {
            if (data == null || data.Length != 512)
                throw new ArgumentException(
                    "The ART discipline EEPROM image must be exactly 512 bytes.",
                    "data");
            TimeCardDisciplineBlobRaw request = new TimeCardDisciplineBlobRaw
            {
                Size = (uint)Marshal.SizeOf(
                    typeof(TimeCardDisciplineBlobRaw)),
                Length = 512,
                Data = (byte[])data.Clone()
            };
            byte[] output = Call(IoctlDisciplineWrite,
                StructToBytes(request), Marshal.SizeOf(
                    typeof(TimeCardDisciplineBlobRaw)));
            return new TimeCardDisciplineParameters(
                BytesToStruct<TimeCardDisciplineBlobRaw>(output));
        }

        private Mro50Status ControlMro50(uint action, uint value)
        {
            TimeCardMro50ControlRaw request = new TimeCardMro50ControlRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardMro50ControlRaw)),
                Action = action,
                Value = value
            };
            byte[] output = Call(IoctlMro50Control, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardMro50StatusRaw)));
            return new Mro50Status(
                BytesToStruct<TimeCardMro50StatusRaw>(output));
        }

        private TimeCardHierarchyRaw SetHierarchyRaw(uint action, bool persist)
        {
            TimeCardHierarchyRaw request = new TimeCardHierarchyRaw
            {
                Size = (uint)Marshal.SizeOf(typeof(TimeCardHierarchyRaw)),
                Action = action,
                Persist = persist ? 1u : 0u
            };
            byte[] output = Call(IoctlHierarchy, StructToBytes(request),
                Marshal.SizeOf(typeof(TimeCardHierarchyRaw)));
            return BytesToStruct<TimeCardHierarchyRaw>(output);
        }

        public void Dispose()
        {
            lock (gate)
            {
                if (handle != null)
                {
                    handle.Dispose();
                    handle = null;
                }
            }
        }

        private static uint ControlCode(uint index, uint access)
        {
            return (FileDeviceUnknown << 16) | (access << 14) | ((0x800 + index) << 2);
        }

        private T GetOutput<T>(uint code) where T : struct
        {
            byte[] output = Call(code, null, Marshal.SizeOf(typeof(T)));
            return BytesToStruct<T>(output);
        }

        private void SendInput<T>(uint code, T input) where T : struct
        {
            Call(code, StructToBytes(input), 0);
        }

        private byte[] Call(uint code, byte[] input, int outputSize)
        {
            lock (gate)
            {
                if (handle == null || handle.IsClosed || handle.IsInvalid)
                    throw new ObjectDisposedException("TimeCardClient");
                byte[] output = outputSize == 0 ? null : new byte[outputSize];
                int returned;
                bool success = DeviceIoControl(handle, code, input,
                    input == null ? 0 : input.Length, output, outputSize,
                    out returned, IntPtr.Zero);
                if (!success)
                {
                    int error = Marshal.GetLastWin32Error();
                    string systemMessage = new Win32Exception(error).Message;
                    throw new Win32Exception(error, string.Format(
                        "Time Card IOCTL 0x{0:X8} failed with Win32 error {1}: {2}",
                        code, error, systemMessage));
                }
                if (output != null && returned < outputSize)
                    Array.Resize(ref output, returned);
                return output ?? new byte[0];
            }
        }

        private static byte[] StructToBytes<T>(T value) where T : struct
        {
            int size = Marshal.SizeOf(typeof(T));
            byte[] bytes = new byte[size];
            IntPtr pointer = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(value, pointer, false);
                Marshal.Copy(pointer, bytes, 0, size);
                return bytes;
            }
            finally
            {
                Marshal.FreeHGlobal(pointer);
            }
        }

        private static T BytesToStruct<T>(byte[] bytes) where T : struct
        {
            int size = Marshal.SizeOf(typeof(T));
            if (bytes == null || bytes.Length < size)
                throw new InvalidOperationException("The driver returned a truncated response.");
            IntPtr pointer = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.Copy(bytes, 0, pointer, size);
                return (T)Marshal.PtrToStructure(pointer, typeof(T));
            }
            finally
            {
                Marshal.FreeHGlobal(pointer);
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct NativeSystemTime
        {
            public ushort Year;
            public ushort Month;
            public ushort DayOfWeek;
            public ushort Day;
            public ushort Hour;
            public ushort Minute;
            public ushort Second;
            public ushort Milliseconds;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct SpDeviceInterfaceData
        {
            public uint Size;
            public Guid InterfaceClassGuid;
            public uint Flags;
            public UIntPtr Reserved;
        }

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode,
            SetLastError = true)]
        private static extern IntPtr SetupDiGetClassDevs(
            ref Guid classGuid, IntPtr enumerator, IntPtr parentWindow,
            uint flags);

        [DllImport("setupapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiEnumDeviceInterfaces(
            IntPtr deviceInfoSet, IntPtr deviceInfoData,
            ref Guid interfaceClassGuid, uint memberIndex,
            ref SpDeviceInterfaceData deviceInterfaceData);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode,
            SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiGetDeviceInterfaceDetail(
            IntPtr deviceInfoSet,
            ref SpDeviceInterfaceData deviceInterfaceData,
            IntPtr deviceInterfaceDetailData,
            uint deviceInterfaceDetailDataSize,
            ref uint requiredSize,
            IntPtr deviceInfoData);

        [DllImport("setupapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiDestroyDeviceInfoList(
            IntPtr deviceInfoSet);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFile(string fileName, uint desiredAccess,
            uint shareMode, IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DeviceIoControl(SafeFileHandle device, uint controlCode,
            byte[] inputBuffer, int inputBufferSize, byte[] outputBuffer,
            int outputBufferSize, out int bytesReturned, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetSystemTime(ref NativeSystemTime systemTime);

        [DllImport("kernel32.dll")]
        private static extern ulong GetTickCount64();
    }
}
