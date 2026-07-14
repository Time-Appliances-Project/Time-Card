using System;
using System.Runtime.InteropServices;

namespace TimeCardControlCenter
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardTimeRaw
    {
        public ulong Seconds;
        public uint Nanoseconds;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardCrossTimestampRaw
    {
        public TimeCardTimeRaw CardTime;
        public ulong SystemTimeBefore100ns;
        public ulong SystemTimeAfter100ns;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardInfoRaw
    {
        public uint AbiVersion;
        public uint DriverVersion;
        public uint Layout;
        public uint InterruptMessages;
        public uint BarLength;
        public uint ClockOffset;
        public uint ClockVersion;
        public uint ClockStatus;
        public uint ClockSelect;
        public uint TodVersion;
        public uint TodStatus;
        public uint UtcStatus;
        public uint Leap;
        public uint GnssStatus;
        public uint Satellites;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardUartConfigRaw
    {
        public uint Port;
        public uint Baud;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardUartReadRequestRaw
    {
        public uint Port;
        public uint MaximumBytes;
        public uint TimeoutMilliseconds;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardHierarchyRaw
    {
        public uint Size;
        public uint Action;
        public uint Persist;
        public uint RuntimeEnabled;
        public uint Persisted;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardSmaControlRaw
    {
        public uint Size;
        public uint Connector;
        public uint Direction;
        public uint Function;
        public uint Flags;
        public uint InputMap;
        public uint OutputMap;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardI2cStatusRaw
    {
        public uint Size;
        public uint Flags;
        public uint Offset;
        public uint Control;
        public uint Status;
        public uint InterruptStatus;
        public uint InterruptEnable;
        public uint TxFifoOccupancy;
        public uint RxFifoOccupancy;
        public uint KnownDeviceMask;
        public uint Reserved0;
        public uint Reserved1;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardI2cProbeRaw
    {
        public uint Size;
        public uint Address;
        public uint Present;
        public uint ControllerStatus;
        public uint InterruptStatus;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardI2cReadRequestRaw
    {
        public uint Size;
        public uint Address;
        public uint SubaddressLength;
        public uint Subaddress;
        public uint Length;
        public uint TimeoutMilliseconds;
        public uint Reserved0;
        public uint Reserved1;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardClockSourceRaw
    {
        public uint Size;
        public uint Source;
        public uint ActiveSource;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
        public uint Reserved4;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardNmeaControlRaw
    {
        public uint Size;
        public uint Flags;
        public uint Baud;
        public uint BaudSelector;
        public uint Polarity;
        public uint Control;
        public uint Status;
        public uint Version;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardIdentityRaw
    {
        public uint Size;
        public uint Flags;
        public byte Serial0;
        public byte Serial1;
        public byte Serial2;
        public byte Serial3;
        public byte Serial4;
        public byte Serial5;
        public byte Reserved0;
        public byte Reserved1;
    }

    public enum SmaDirection : uint
    {
        Input = 0,
        Output = 1,
        Disabled = 2
    }

    public sealed class SmaConnectorState
    {
        internal SmaConnectorState(TimeCardSmaControlRaw value)
        {
            Connector = value.Connector;
            Direction = (SmaDirection)value.Direction;
            Function = value.Function;
            IsPresent = (value.Flags & 1u) != 0;
            IsDirectionFixed = (value.Flags & 2u) != 0;
            IsDisabled = (value.Flags & 4u) != 0;
            InputMap = value.InputMap;
            OutputMap = value.OutputMap;
        }

        public uint Connector { get; private set; }
        public SmaDirection Direction { get; private set; }
        public uint Function { get; private set; }
        public bool IsPresent { get; private set; }
        public bool IsDirectionFixed { get; private set; }
        public bool IsDisabled { get; private set; }
        public uint InputMap { get; private set; }
        public uint OutputMap { get; private set; }
    }

    public sealed class I2cControllerStatus
    {
        internal I2cControllerStatus(TimeCardI2cStatusRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            IsBusBusy = (value.Flags & 4u) != 0;
            IsReceiveFifoEmpty = (value.Flags & 8u) != 0;
            IsTransmitFifoEmpty = (value.Flags & 16u) != 0;
            Offset = value.Offset;
            Control = value.Control;
            Status = value.Status;
            InterruptStatus = value.InterruptStatus;
            InterruptEnable = value.InterruptEnable;
            TxFifoOccupancy = value.TxFifoOccupancy;
            RxFifoOccupancy = value.RxFifoOccupancy;
            KnownDeviceMask = value.KnownDeviceMask;
        }

        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool IsBusBusy { get; private set; }
        public bool IsReceiveFifoEmpty { get; private set; }
        public bool IsTransmitFifoEmpty { get; private set; }
        public uint Offset { get; private set; }
        public uint Control { get; private set; }
        public uint Status { get; private set; }
        public uint InterruptStatus { get; private set; }
        public uint InterruptEnable { get; private set; }
        public uint TxFifoOccupancy { get; private set; }
        public uint RxFifoOccupancy { get; private set; }
        public uint KnownDeviceMask { get; private set; }
    }

    public sealed class I2cProbeResult
    {
        internal I2cProbeResult(TimeCardI2cProbeRaw value)
        {
            Address = value.Address;
            IsPresent = value.Present != 0;
            ControllerStatus = value.ControllerStatus;
            InterruptStatus = value.InterruptStatus;
        }

        public uint Address { get; private set; }
        public bool IsPresent { get; private set; }
        public uint ControllerStatus { get; private set; }
        public uint InterruptStatus { get; private set; }
    }

    public sealed class I2cReadResult
    {
        public I2cReadResult(uint address, byte[] data, uint controllerStatus,
                             uint interruptStatus)
        {
            Address = address;
            Data = data;
            ControllerStatus = controllerStatus;
            InterruptStatus = interruptStatus;
        }

        public uint Address { get; private set; }
        public byte[] Data { get; private set; }
        public uint ControllerStatus { get; private set; }
        public uint InterruptStatus { get; private set; }
    }

    public sealed class NmeaOutputState
    {
        internal NmeaOutputState(TimeCardNmeaControlRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            Baud = value.Baud;
            BaudSelector = value.BaudSelector;
            IsInverted = value.Polarity != 0;
            Control = value.Control;
            Status = value.Status;
            Version = value.Version;
        }

        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public uint Baud { get; private set; }
        public uint BaudSelector { get; private set; }
        public bool IsInverted { get; private set; }
        public uint Control { get; private set; }
        public uint Status { get; private set; }
        public uint Version { get; private set; }
    }

    public sealed class TimeCardIdentity
    {
        internal TimeCardIdentity(TimeCardIdentityRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            SerialBytes = new[] { value.Serial0, value.Serial1, value.Serial2,
                value.Serial3, value.Serial4, value.Serial5 };
            SerialNumber = BitConverter.ToString(SerialBytes).Replace('-', ':');
        }

        public bool IsPresent { get; private set; }
        public bool IsValid { get; private set; }
        public byte[] SerialBytes { get; private set; }
        public string SerialNumber { get; private set; }
    }

    public sealed class TimeCardSnapshot
    {
        internal TimeCardSnapshot(TimeCardInfoRaw info, TimeCardCrossTimestampRaw timestamp,
                                  TimeCardHierarchyRaw hierarchy)
        {
            AbiVersion = info.AbiVersion;
            DriverVersion = string.Format("{0}.{1}", info.DriverVersion >> 16, info.DriverVersion & 0xffff);
            Layout = info.Layout == 2 ? "MSI-X" : info.Layout == 1 ? "MSI" : "Unknown";
            InterruptMessages = info.InterruptMessages;
            BarLength = info.BarLength;
            ClockOffset = info.ClockOffset;
            ClockVersion = string.Format("{0}.{1}.{2}", info.ClockVersion >> 24,
                (info.ClockVersion >> 16) & 0xff, info.ClockVersion & 0xffff);
            ClockVersionRaw = info.ClockVersion;
            ClockStatus = info.ClockStatus;
            ClockSource = info.ClockSelect >> 16;
            TodVersion = info.TodVersion;
            TodStatus = info.TodStatus;
            UtcStatus = info.UtcStatus;
            Leap = info.Leap;
            GnssStatus = info.GnssStatus;
            Satellites = info.Satellites;
            SeenSatellites = (int)(info.Satellites & 0xff);
            LockedSatellites = (int)((info.Satellites >> 8) & 0xff);
            SatelliteDataValid = (info.Satellites & (1u << 16)) != 0;
            GnssFixOk = (info.GnssStatus & (1u << 16)) != 0;
            GnssFixCode = (int)((info.GnssStatus >> 17) & 0xff);
            string[] fixNames = { "No fix", "Dead reckoning", "2-D fix", "3-D fix",
                "GPS + dead reckoning", "Unknown" };
            GnssFix = GnssFixCode >= 0 && GnssFixCode < fixNames.Length - 1
                ? fixNames[GnssFixCode] : fixNames[fixNames.Length - 1];
            HierarchyRuntimeEnabled = hierarchy.RuntimeEnabled != 0;
            HierarchyPersisted = hierarchy.Persisted != 0;

            DateTime epoch = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);
            CardTimeUtc = epoch.AddSeconds(timestamp.CardTime.Seconds)
                .AddTicks(timestamp.CardTime.Nanoseconds / 100);
            SystemTimeBeforeUtc = DateTime.FromFileTimeUtc((long)timestamp.SystemTimeBefore100ns);
            SystemTimeAfterUtc = DateTime.FromFileTimeUtc((long)timestamp.SystemTimeAfter100ns);
            SystemTimeUtc = SystemTimeBeforeUtc.AddTicks(
                (SystemTimeAfterUtc.Ticks - SystemTimeBeforeUtc.Ticks) / 2);
            OffsetNanoseconds = (CardTimeUtc.Ticks - SystemTimeUtc.Ticks) * 100L;
            SamplingWindowNanoseconds =
                (long)(timestamp.SystemTimeAfter100ns - timestamp.SystemTimeBefore100ns) * 100L;
        }

        public uint AbiVersion { get; private set; }
        public string DriverVersion { get; private set; }
        public string Layout { get; private set; }
        public uint InterruptMessages { get; private set; }
        public uint BarLength { get; private set; }
        public uint ClockOffset { get; private set; }
        public string ClockVersion { get; private set; }
        public uint ClockVersionRaw { get; private set; }
        public uint ClockStatus { get; private set; }
        public bool IsClockSynchronized { get { return (ClockStatus & 1) != 0; } }
        public uint ClockSource { get; private set; }
        public uint TodVersion { get; private set; }
        public uint TodStatus { get; private set; }
        public uint UtcStatus { get; private set; }
        public uint Leap { get; private set; }
        public uint GnssStatus { get; private set; }
        public uint Satellites { get; private set; }
        public int SeenSatellites { get; private set; }
        public int LockedSatellites { get; private set; }
        public bool SatelliteDataValid { get; private set; }
        public bool GnssFixOk { get; private set; }
        public int GnssFixCode { get; private set; }
        public string GnssFix { get; private set; }
        public bool HierarchyRuntimeEnabled { get; private set; }
        public bool HierarchyPersisted { get; private set; }
        public DateTime CardTimeUtc { get; private set; }
        public DateTime SystemTimeBeforeUtc { get; private set; }
        public DateTime SystemTimeAfterUtc { get; private set; }
        public DateTime SystemTimeUtc { get; private set; }
        public long OffsetNanoseconds { get; private set; }
        public long SamplingWindowNanoseconds { get; private set; }
    }

    public sealed class UartReadResult
    {
        public UartReadResult(byte[] data, uint lineStatus)
        {
            Data = data;
            LineStatus = lineStatus;
        }

        public byte[] Data { get; private set; }
        public uint LineStatus { get; private set; }
    }

    public sealed class UartWriteResult
    {
        public UartWriteResult(uint bytesTransferred, uint lineStatus)
        {
            BytesTransferred = bytesTransferred;
            LineStatus = lineStatus;
        }

        public uint BytesTransferred { get; private set; }
        public uint LineStatus { get; private set; }
    }
}
