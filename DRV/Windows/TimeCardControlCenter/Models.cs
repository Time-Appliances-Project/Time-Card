using System;
using System.Collections.Generic;
using System.Globalization;
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
    internal struct TimeCardI2cMuxControlRaw
    {
        public uint Size;
        public uint ChannelMask;
        public uint Present;
        public uint ControllerStatus;
        public uint InterruptStatus;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardLedControlRaw
    {
        public uint Size;
        public uint Led;
        public uint Flags;
        public uint Red;
        public uint Green;
        public uint Blue;
        public uint GlobalCurrent;
        public uint MuxChannelMask;
        public uint ControllerStatus;
        public uint InterruptStatus;
        public uint OpenOutputMask;
        public uint ShortOutputMask;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardBme280ReadingRaw
    {
        public uint Size;
        public uint Flags;
        public uint ChipId;
        public uint Status;
        public int RawTemperature;
        public uint RawPressure;
        public uint RawHumidity;
        public uint DigT1;
        public int DigT2;
        public int DigT3;
        public uint DigP1;
        public int DigP2;
        public int DigP3;
        public int DigP4;
        public int DigP5;
        public int DigP6;
        public int DigP7;
        public int DigP8;
        public int DigP9;
        public uint DigH1;
        public int DigH2;
        public uint DigH3;
        public int DigH4;
        public int DigH5;
        public int DigH6;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardIna219ReadingRaw
    {
        public uint Size;
        public uint Flags;
        public uint Address;
        public uint BusMillivolts;
        public int ShuntMicrovolts;
        public int CurrentMilliamps;
        public int PowerMilliwatts;
        public uint Configuration;
        public uint RawBus;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardLm75bReadingRaw
    {
        public uint Size;
        public uint Flags;
        public uint Address;
        public int RawTemperature;
        public int TemperatureMilliCelsius;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardSht3xReadingRaw
    {
        public uint Size;
        public uint Flags;
        public uint Address;
        public uint Status;
        public uint RawTemperature;
        public uint RawHumidity;
        public int TemperatureMilliCelsius;
        public uint HumidityMilliPercent;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardIcp10100ReadingRaw
    {
        public uint Size;
        public uint Flags;
        public uint Address;
        public uint ProductId;
        public uint RawPressure;
        public uint RawTemperature;
        public int TemperatureMilliCelsius;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
        public int[] Otp;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardBno055ReadingRaw
    {
        public uint Size;
        public uint Flags;
        public uint ChipId;
        public uint OperationMode;
        public uint PowerMode;
        public uint UnitSelection;
        public uint SystemStatus;
        public uint SystemError;
        public uint Calibration;
        public uint SelfTest;
        public uint SystemClockStatus;
        public int Temperature;
        public int AccelerationX;
        public int AccelerationY;
        public int AccelerationZ;
        public int MagneticX;
        public int MagneticY;
        public int MagneticZ;
        public int GyroscopeX;
        public int GyroscopeY;
        public int GyroscopeZ;
        public int Heading;
        public int Roll;
        public int Pitch;
        public int QuaternionW;
        public int QuaternionX;
        public int QuaternionY;
        public int QuaternionZ;
        public int LinearAccelerationX;
        public int LinearAccelerationY;
        public int LinearAccelerationZ;
        public int GravityX;
        public int GravityY;
        public int GravityZ;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardSensorTelemetryRaw
    {
        public uint Size;
        public uint Flags;
        public uint MuxChannelMask;
        public uint ControllerStatus;
        public uint InterruptStatus;
        public TimeCardBme280ReadingRaw Environment;
        public TimeCardIna219ReadingRaw Rail12V;
        public TimeCardIna219ReadingRaw Rail5V;
        public TimeCardIna219ReadingRaw Rail3V3;
        public TimeCardBno055ReadingRaw Imu;
        public uint BoardProfile;
        public uint Capabilities;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
        public TimeCardLm75bReadingRaw[] BoardTemperature;
        public TimeCardSht3xReadingRaw Humidity;
        public TimeCardIcp10100ReadingRaw Pressure;
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
    internal struct TimeCardClockAdjustmentRaw
    {
        public uint Size;
        public uint Flags;
        public uint Version;
        public uint Control;
        public uint Select;
        public int OffsetNanoseconds;
        public uint OffsetIntervalNanoseconds;
        public long DriftPpbQ16;
        public uint DriftIntervalNanoseconds;
        public uint InSyncThresholdNanoseconds;
        public uint AppliedFlags;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 7)]
        public uint[] Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardClockAdvancedControlRaw
    {
        public uint Size;
        public uint Flags;
        public uint Version;
        public uint Control;
        public uint Status;
        public uint ApplyFlags;
        public uint OffsetRateLimiter;
        public uint DriftRateLimiterQ16;
        public uint AgingConfiguration;
        public uint HoldoverConfiguration;
        public uint OffsetOutlierFilter;
        public uint DriftOutlierFilter;
        public uint DynamicControl;
        public uint ServoOffsetP;
        public uint ServoOffsetI;
        public uint ServoDriftP;
        public uint ServoDriftI;
        public uint StatusOffset;
        public uint StatusDrift;
        public uint StatusOffsetFraction;
        public uint StatusDriftFraction;
        public uint StatusHoldover;
        public uint StatusHoldoverFraction;
        public uint StatusHoldoverSamples;
        public uint StatusOffsetOutliers;
        public uint StatusDriftOutliers;
        public uint StatusAgingLow;
        public uint StatusAgingHigh;
        public uint StatusAgingSamples;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
        public uint[] Reserved;
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
        public int CorrectionSeconds;
        public int LocalOffsetMinutes;
        public uint Gnss;
        public uint MessageDisableMask;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardNmeaUtcControlRaw
    {
        public uint Size;
        public uint Flags;
        public uint Version;
        public uint RawUtcInfo;
        public uint UtcOffsetSeconds;
        public uint HandshakeControl;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 10)]
        public uint[] Reserved;
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

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardSignalControlRaw
    {
        public uint Size;
        public uint Generator;
        public uint Flags;
        public uint Status;
        public uint Version;
        public uint RepeatCount;
        public uint StartNanoseconds;
        public uint CableDelayNanoseconds;
        public ulong PeriodNanoseconds;
        public ulong PulseNanoseconds;
        public ulong PhaseNanoseconds;
        public ulong StartSeconds;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardSignalEventRaw
    {
        public ulong SystemInterruptTime100ns;
        public ulong Sequence;
        public uint Generator;
        public uint Flags;
        public uint Status;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardSignalEventBatchRaw
    {
        public uint Size;
        public uint Generator;
        public uint MaximumEvents;
        public uint Count;
        public uint DroppedEvents;
        public uint Flags;
        public uint Reserved0;
        public uint Reserved1;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public TimeCardSignalEventRaw[] Events;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardTimestampEventRaw
    {
        public TimeCardTimeRaw Time;
        public uint TimestampCount;
        public uint EventCount;
        public uint Error;
        public uint DataWidth;
        public uint Flags;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
        public uint[] Data;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardTimestampControlRaw
    {
        public uint Size;
        public uint Channel;
        public uint Flags;
        public uint Status;
        public uint Version;
        public uint Polarity;
        public uint CableDelayNanoseconds;
        public uint Interrupt;
        public uint InterruptMask;
        public uint EventCount;
        public uint TimestampCount;
        public uint QueueDepth;
        public uint DroppedEvents;
        public uint DataWidth;
        public TimeCardTimeRaw Time;
        public uint Data;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardTimestampBatchRaw
    {
        public uint Size;
        public uint Channel;
        public uint MaximumEvents;
        public uint Count;
        public uint DroppedEvents;
        public uint Flags;
        public uint Reserved0;
        public uint Reserved1;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
        public TimeCardTimestampEventRaw[] Events;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardFpgaCapabilitiesRaw
    {
        public uint Size;
        public uint AbiVersion;
        public uint CoreMask;
        public uint FeatureFlags;
        public uint KnownGaps;
        public uint Layout;
        public uint BoardProfile;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
        public uint Reserved4;
        public uint Reserved5;
        public uint Reserved6;
        public uint Reserved7;
        public uint Reserved8;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardCoreDescriptorRaw
    {
        public uint Type;
        public uint Instance;
        public uint RegisterOffset;
        public uint RegisterSpan;
        public uint InterruptMessage;
        public uint Version;
        public uint Flags;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardCoreInventoryRaw
    {
        public uint Size;
        public uint AbiVersion;
        public uint Count;
        public uint Flags;
        public uint BoardProfile;
        public uint Layout;
        public uint RawImageVersion;
        public uint Reserved;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)]
        public TimeCardCoreDescriptorRaw[] Cores;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardFpgaImageContractRaw
    {
        public uint Size;
        public uint AbiVersion;
        public uint RawImageVersion;
        public uint CapabilityFlags;
        public uint EffectiveFlags;
        public uint StatusFlags;
        public uint BoardProfile;
        public uint Layout;
        public uint Acknowledgement;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 7)]
        public uint[] Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardFpgaImageInfoRaw
    {
        public uint Size;
        public uint AbiVersion;
        public uint Flags;
        public uint RawVersion;
        public uint ImageTag;
        public uint ImageVersion;
        public uint Layout;
        public uint BoardProfile;
        public uint RegisterOffset;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
        public uint Reserved4;
        public uint Reserved5;
        public uint Reserved6;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardClockTelemetryRaw
    {
        public uint Size;
        public uint Flags;
        public uint Version;
        public uint Control;
        public uint Status;
        public uint Select;
        public uint CoreMask;
        public uint KnownGaps;
        public uint InSyncThreshold;
        public uint ServoOffsetP;
        public uint ServoOffsetI;
        public uint ServoDriftP;
        public uint ServoDriftI;
        public int StatusOffsetNanoseconds;
        public int StatusDriftPpb;
        public uint StatusOffsetFraction;
        public uint StatusDriftFraction;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardPpsControlRaw
    {
        public uint Size;
        public uint Core;
        public uint Flags;
        public uint Control;
        public uint Status;
        public uint Version;
        public uint Polarity;
        public uint PulseWidthMilliseconds;
        public int CableDelayNanoseconds;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
        public uint Reserved4;
        public uint Reserved5;
        public uint Reserved6;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardTimecodeControlRaw
    {
        public uint Size;
        public uint Format;
        public uint Role;
        public uint Flags;
        public uint Control;
        public uint Status;
        public uint Version;
        public uint Mode;
        public uint Code;
        public int CorrectionSeconds;
        public int DelayNanoseconds;
        public uint ControlBits;
        public uint BitPosition;
        public uint AmplitudeModulation;
        public uint ManualYear;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
        public uint Reserved4;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardTodControlRaw
    {
        public uint Size;
        public uint Flags;
        public uint Control;
        public uint Status;
        public uint Version;
        public uint Protocol;
        public uint Gnss;
        public uint Baud;
        public uint BaudSelector;
        public uint Polarity;
        public int CorrectionSeconds;
        public uint MessageDisableMask;
        public uint UtcStatus;
        public int TimeToLeapSeconds;
        public uint GnssStatus;
        public uint Satellites;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardFrequencyControlRaw
    {
        public uint Size;
        public uint Counter;
        public uint Flags;
        public uint IntegrationSeconds;
        public uint FrequencyHz;
        public uint Control;
        public uint Status;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardFlashStatusRaw
    {
        public uint Size;
        public uint Flags;
        public uint Offset;
        public uint JedecId;
        public uint CapacityBytes;
        public uint FirmwareOffset;
        public uint EraseSize;
        public uint PageSize;
        public uint ControllerStatus;
        public uint FlashStatus;
        public uint FifoDepth;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
        public uint Reserved4;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardFlashRangeRaw
    {
        public uint Size;
        public uint Offset;
        public uint Length;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardUartObserveRaw
    {
        public uint Size;
        public uint Port;
        public uint TimeoutMilliseconds;
        public uint Flags;
        public uint LineStatus;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardMro50StatusRaw
    {
        public uint Size;
        public uint Flags;
        public uint Control;
        public uint FineAdjustment;
        public uint CoarseAdjustment;
        public uint Temperature;
        public uint BoardConfig;
        public uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardMro50ControlRaw
    {
        public uint Size;
        public uint Action;
        public uint Value;
        public uint Reserved;
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
            LastStartInitialControl = value.Reserved0 & 0xffu;
            LastStartInitialStatus = (value.Reserved0 >> 8) & 0xffu;
            LastStartFinalControl = (value.Reserved0 >> 16) & 0xffu;
            LastStartFinalStatus = (value.Reserved0 >> 24) & 0xffu;
            LastStartInterruptStatus = value.Reserved1 & 0xffffu;
            LastStartInitialTxFifoOccupancy =
                (value.Reserved1 >> 16) & 0xffu;
            LastStartFinalTxFifoOccupancy =
                (value.Reserved1 >> 24) & 0xffu;
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
        public uint LastStartInitialControl { get; private set; }
        public uint LastStartInitialStatus { get; private set; }
        public uint LastStartFinalControl { get; private set; }
        public uint LastStartFinalStatus { get; private set; }
        public uint LastStartInterruptStatus { get; private set; }
        public uint LastStartInitialTxFifoOccupancy { get; private set; }
        public uint LastStartFinalTxFifoOccupancy { get; private set; }
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

    public sealed class I2cMuxState
    {
        internal I2cMuxState(TimeCardI2cMuxControlRaw value)
        {
            ChannelMask = value.ChannelMask;
            IsPresent = value.Present != 0;
            ControllerStatus = value.ControllerStatus;
            InterruptStatus = value.InterruptStatus;
        }

        public uint ChannelMask { get; private set; }
        public bool IsPresent { get; private set; }
        public uint ControllerStatus { get; private set; }
        public uint InterruptStatus { get; private set; }
    }

    public sealed class BoardLedState
    {
        internal BoardLedState(TimeCardLedControlRaw value)
        {
            Led = value.Led;
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            HasFaultDiagnostics = (value.Flags & 4u) != 0;
            IsDcTestActive = (value.Flags & 8u) != 0;
            WasResetTested = (value.Flags & 16u) != 0;
            IsSdbHigh = (value.Flags & 32u) != 0;
            Red = (byte)value.Red;
            Green = (byte)value.Green;
            Blue = (byte)value.Blue;
            GlobalCurrent = (byte)value.GlobalCurrent;
            MuxChannelMask = value.MuxChannelMask;
            ControllerStatus = value.ControllerStatus;
            InterruptStatus = value.InterruptStatus;
            OpenOutputMask = value.OpenOutputMask;
            ShortOutputMask = value.ShortOutputMask;
        }

        public uint Led { get; private set; }
        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool HasFaultDiagnostics { get; private set; }
        public bool IsDcTestActive { get; private set; }
        public bool WasResetTested { get; private set; }
        public bool IsSdbHigh { get; private set; }
        public byte Red { get; private set; }
        public byte Green { get; private set; }
        public byte Blue { get; private set; }
        public byte GlobalCurrent { get; private set; }
        public uint MuxChannelMask { get; private set; }
        public uint ControllerStatus { get; private set; }
        public uint InterruptStatus { get; private set; }
        public uint OpenOutputMask { get; private set; }
        public uint ShortOutputMask { get; private set; }
    }

    public sealed class SensorTelemetrySnapshot
    {
        internal SensorTelemetrySnapshot(TimeCardSensorTelemetryRaw value)
        {
            IsAvailable = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            MuxChannelMask = value.MuxChannelMask;
            ControllerStatus = value.ControllerStatus;
            InterruptStatus = value.InterruptStatus;
            Environment = new EnvironmentSensorReading(value.Environment);
            Rail12V = new PowerRailReading("+12 V", value.Rail12V);
            Rail5V = new PowerRailReading("+5 V", value.Rail5V);
            Rail3V3 = new PowerRailReading("+3.3 V", value.Rail3V3);
            Imu = new ImuSensorReading(value.Imu);
            BoardProfile = value.BoardProfile;
            Capabilities = value.Capabilities;
            TimeCardLm75bReadingRaw[] temperatures = value.BoardTemperature ??
                new TimeCardLm75bReadingRaw[3];
            BoardTemperatures = new[]
            {
                new BoardTemperatureReading("Sensor 1", temperatures[0]),
                new BoardTemperatureReading("Sensor 2", temperatures[1]),
                new BoardTemperatureReading("Sensor 3", temperatures[2])
            };
            HumiditySensor = new Sht3xSensorReading(value.Humidity);
            PressureSensor = new Icp10100SensorReading(value.Pressure);
        }

        public bool IsAvailable { get; private set; }
        public bool IsValid { get; private set; }
        public uint MuxChannelMask { get; private set; }
        public uint ControllerStatus { get; private set; }
        public uint InterruptStatus { get; private set; }
        public EnvironmentSensorReading Environment { get; private set; }
        public PowerRailReading Rail12V { get; private set; }
        public PowerRailReading Rail5V { get; private set; }
        public PowerRailReading Rail3V3 { get; private set; }
        public ImuSensorReading Imu { get; private set; }
        public uint BoardProfile { get; private set; }
        public uint Capabilities { get; private set; }
        public bool IsCelestica { get { return BoardProfile == 3u; } }
        public BoardTemperatureReading[] BoardTemperatures { get; private set; }
        public Sht3xSensorReading HumiditySensor { get; private set; }
        public Icp10100SensorReading PressureSensor { get; private set; }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardCapabilitiesRaw
    {
        public uint Size;
        public uint AbiVersion;
        public ulong Flags;
        public uint BoardProfile;
        public uint OscillatorType;
        public uint ReferencePpsIndex;
        public uint OscillatorPpsIndex;
        public uint FineMinimum;
        public uint FineMaximum;
        public uint CoarseMinimum;
        public uint CoarseMaximum;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardPhaseSampleRaw
    {
        public uint Size;
        public uint Flags;
        public uint ReferenceCounter;
        public uint OscillatorCounter;
        public TimeCardTimeRaw ReferenceTime;
        public TimeCardTimeRaw OscillatorTime;
        public long PhaseNanoseconds;
        public uint ReferenceError;
        public uint OscillatorError;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardPhaseControlRaw
    {
        public uint Size;
        public uint Action;
        public uint ReferencePolarity;
        public uint OscillatorPolarity;
        public uint Enabled;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardPhcAdjustRaw
    {
        public uint Size;
        public uint Flags;
        public long OffsetNanoseconds;
        public TimeCardTimeRaw ResultingTime;
        public uint Reserved0;
        public uint Reserved1;
        public uint Reserved2;
        public uint Reserved3;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardDisciplineBlobRaw
    {
        public uint Size;
        public uint Flags;
        public uint Length;
        public uint Reserved;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 512)]
        public byte[] Data;
    }

    public sealed class BoardTemperatureReading
    {
        internal BoardTemperatureReading(string name,
                                         TimeCardLm75bReadingRaw value)
        {
            Name = name;
            IsPresent = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            Address = value.Address;
            RawTemperature = value.RawTemperature;
            TemperatureCelsius = value.TemperatureMilliCelsius / 1000.0;
        }

        public string Name { get; private set; }
        public bool IsPresent { get; private set; }
        public bool IsValid { get; private set; }
        public uint Address { get; private set; }
        public int RawTemperature { get; private set; }
        public double TemperatureCelsius { get; private set; }
    }

    public sealed class Sht3xSensorReading
    {
        internal Sht3xSensorReading(TimeCardSht3xReadingRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            HasValidCrc = (value.Flags & 512u) != 0;
            Address = value.Address;
            Status = value.Status;
            RawTemperature = value.RawTemperature;
            RawHumidity = value.RawHumidity;
            TemperatureCelsius = value.TemperatureMilliCelsius / 1000.0;
            HumidityPercent = value.HumidityMilliPercent / 1000.0;
            if (IsValid && HumidityPercent > 0.0)
            {
                double gamma = Math.Log(HumidityPercent / 100.0) +
                    17.62 * TemperatureCelsius /
                    (243.12 + TemperatureCelsius);
                DewPointCelsius = 243.12 * gamma / (17.62 - gamma);
            }
        }

        public bool IsPresent { get; private set; }
        public bool IsValid { get; private set; }
        public bool HasValidCrc { get; private set; }
        public uint Address { get; private set; }
        public uint Status { get; private set; }
        public uint RawTemperature { get; private set; }
        public uint RawHumidity { get; private set; }
        public double TemperatureCelsius { get; private set; }
        public double HumidityPercent { get; private set; }
        public double DewPointCelsius { get; private set; }
    }

    public sealed class Icp10100SensorReading
    {
        internal Icp10100SensorReading(TimeCardIcp10100ReadingRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            HasValidCrc = (value.Flags & 512u) != 0;
            Address = value.Address;
            ProductId = value.ProductId;
            RawPressure = value.RawPressure;
            RawTemperature = value.RawTemperature;
            TemperatureCelsius = value.TemperatureMilliCelsius / 1000.0;
            Otp = value.Otp ?? new int[4];
            CalculateCompensatedPressure();
        }

        private void CalculateCompensatedPressure()
        {
            double pressure;
            if (IsValid && CelesticaSensorMath.TryCompensateIcp10100(
                    RawPressure, RawTemperature, Otp, out pressure))
            {
                PressurePascals = pressure;
                HasCompensatedPressure = true;
            }
        }

        public bool IsPresent { get; private set; }
        public bool IsValid { get; private set; }
        public bool HasValidCrc { get; private set; }
        public bool HasCompensatedPressure { get; private set; }
        public uint Address { get; private set; }
        public uint ProductId { get; private set; }
        public uint RawPressure { get; private set; }
        public uint RawTemperature { get; private set; }
        public double TemperatureCelsius { get; private set; }
        public double PressurePascals { get; private set; }
        public double PressureHectopascals { get { return PressurePascals / 100.0; } }
        public int[] Otp { get; private set; }
    }

    public sealed class EnvironmentSensorReading
    {
        internal EnvironmentSensorReading(TimeCardBme280ReadingRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            IsConfigured = (value.Flags & 4u) != 0;
            IsConversionReady = (value.Flags & 8u) != 0;
            HasHumidity = (value.Flags & 64u) != 0;
            ChipId = value.ChipId;
            Status = value.Status;
            if (!IsValid || value.DigT1 == 0 || value.DigP1 == 0)
                return;

            double var1 = (value.RawTemperature / 16384.0 -
                value.DigT1 / 1024.0) * value.DigT2;
            double var2 = value.RawTemperature / 131072.0 -
                value.DigT1 / 8192.0;
            var2 = var2 * var2 * value.DigT3;
            double tFine = var1 + var2;
            TemperatureCelsius = Math.Max(-40.0,
                Math.Min(85.0, tFine / 5120.0));

            var1 = tFine / 2.0 - 64000.0;
            var2 = var1 * var1 * value.DigP6 / 32768.0;
            var2 += var1 * value.DigP5 * 2.0;
            var2 = var2 / 4.0 + value.DigP4 * 65536.0;
            double var3 = value.DigP3 * var1 * var1 / 524288.0;
            var1 = (var3 + value.DigP2 * var1) / 524288.0;
            var1 = (1.0 + var1 / 32768.0) * value.DigP1;
            if (var1 > 0.0)
            {
                double pressure = 1048576.0 - value.RawPressure;
                pressure = (pressure - var2 / 4096.0) * 6250.0 / var1;
                var1 = value.DigP9 * pressure * pressure / 2147483648.0;
                var2 = pressure * value.DigP8 / 32768.0;
                pressure += (var1 + var2 + value.DigP7) / 16.0;
                PressureHectopascals = Math.Max(30000.0,
                    Math.Min(110000.0, pressure)) / 100.0;
            }

            if (HasHumidity)
            {
                var1 = tFine - 76800.0;
                var2 = value.DigH4 * 64.0 + value.DigH5 / 16384.0 * var1;
                var3 = value.RawHumidity - var2;
                double var4 = value.DigH2 / 65536.0;
                double var5 = 1.0 + value.DigH3 / 67108864.0 * var1;
                double var6 = 1.0 + value.DigH6 / 67108864.0 * var1 * var5;
                var6 = var3 * var4 * (var5 * var6);
                HumidityPercent = Math.Max(0.0, Math.Min(100.0,
                    var6 * (1.0 - value.DigH1 * var6 / 524288.0)));
                if (HumidityPercent > 0.0)
                {
                    double gamma = Math.Log(HumidityPercent / 100.0) +
                        17.62 * TemperatureCelsius /
                        (243.12 + TemperatureCelsius);
                    DewPointCelsius = 243.12 * gamma / (17.62 - gamma);
                }
            }
        }

        public bool IsPresent { get; private set; }
        public bool IsValid { get; private set; }
        public bool IsConfigured { get; private set; }
        public bool IsConversionReady { get; private set; }
        public bool HasHumidity { get; private set; }
        public uint ChipId { get; private set; }
        public uint Status { get; private set; }
        public double TemperatureCelsius { get; private set; }
        public double HumidityPercent { get; private set; }
        public double PressureHectopascals { get; private set; }
        public double DewPointCelsius { get; private set; }
    }

    public sealed class PowerRailReading
    {
        internal PowerRailReading(string name, TimeCardIna219ReadingRaw value)
        {
            Name = name;
            IsPresent = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            IsConversionReady = (value.Flags & 8u) != 0;
            HasOverflow = (value.Flags & 16u) != 0;
            Address = value.Address;
            VoltageVolts = value.BusMillivolts / 1000.0;
            CurrentAmps = value.CurrentMilliamps / 1000.0;
            PowerWatts = value.PowerMilliwatts / 1000.0;
            ShuntMillivolts = value.ShuntMicrovolts / 1000.0;
            Configuration = value.Configuration;
        }

        public string Name { get; private set; }
        public bool IsPresent { get; private set; }
        public bool IsValid { get; private set; }
        public bool IsConversionReady { get; private set; }
        public bool HasOverflow { get; private set; }
        public uint Address { get; private set; }
        public double VoltageVolts { get; private set; }
        public double CurrentAmps { get; private set; }
        public double PowerWatts { get; private set; }
        public double ShuntMillivolts { get; private set; }
        public uint Configuration { get; private set; }
    }

    public sealed class SensorVector3
    {
        internal SensorVector3(double x, double y, double z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public double X { get; private set; }
        public double Y { get; private set; }
        public double Z { get; private set; }
    }

    public sealed class ImuSensorReading
    {
        internal ImuSensorReading(TimeCardBno055ReadingRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            IsConfigured = (value.Flags & 4u) != 0;
            UsesExternalClock = (value.Flags & 32u) != 0;
            ChipId = value.ChipId;
            OperationMode = value.OperationMode;
            PowerMode = value.PowerMode;
            UnitSelection = value.UnitSelection;
            SystemStatus = value.SystemStatus;
            SystemError = value.SystemError;
            SelfTest = value.SelfTest;
            SystemClockStatus = value.SystemClockStatus;
            SystemCalibration = (int)((value.Calibration >> 6) & 3u);
            GyroscopeCalibration = (int)((value.Calibration >> 4) & 3u);
            AccelerometerCalibration = (int)((value.Calibration >> 2) & 3u);
            MagnetometerCalibration = (int)(value.Calibration & 3u);

            double accelerationScale = (value.UnitSelection & 0x02u) != 0
                ? 9.80665 / 1000.0 : 1.0 / 100.0;
            double gyroScale = (value.UnitSelection & 0x04u) != 0
                ? 180.0 / (Math.PI * 900.0) : 1.0 / 16.0;
            double eulerScale = (value.UnitSelection & 0x08u) != 0
                ? 180.0 / (Math.PI * 900.0) : 1.0 / 16.0;
            HasTemperature = ChipId == 0x80u
                ? (value.Flags & 128u) != 0
                : IsValid;
            TemperatureCelsius = ChipId == 0x80u &&
                (value.Flags & 256u) != 0
                ? value.Temperature / 128.0
                : (value.UnitSelection & 0x20u) != 0
                    ? (value.Temperature * 2.0 - 32.0) * 5.0 / 9.0
                    : value.Temperature;
            Acceleration = new SensorVector3(
                value.AccelerationX * accelerationScale,
                value.AccelerationY * accelerationScale,
                value.AccelerationZ * accelerationScale);
            MagneticField = new SensorVector3(
                value.MagneticX / 16.0, value.MagneticY / 16.0,
                value.MagneticZ / 16.0);
            Gyroscope = new SensorVector3(
                value.GyroscopeX * gyroScale, value.GyroscopeY * gyroScale,
                value.GyroscopeZ * gyroScale);
            HeadingDegrees = value.Heading * eulerScale;
            RollDegrees = value.Roll * eulerScale;
            PitchDegrees = value.Pitch * eulerScale;
            QuaternionW = value.QuaternionW / 16384.0;
            QuaternionX = value.QuaternionX / 16384.0;
            QuaternionY = value.QuaternionY / 16384.0;
            QuaternionZ = value.QuaternionZ / 16384.0;
            if (ChipId == 0x80u)
            {
                double sinRoll = 2.0 * (QuaternionW * QuaternionX +
                    QuaternionY * QuaternionZ);
                double cosRoll = 1.0 - 2.0 *
                    (QuaternionX * QuaternionX + QuaternionY * QuaternionY);
                double sinPitch = 2.0 * (QuaternionW * QuaternionY -
                    QuaternionZ * QuaternionX);
                double sinYaw = 2.0 * (QuaternionW * QuaternionZ +
                    QuaternionX * QuaternionY);
                double cosYaw = 1.0 - 2.0 *
                    (QuaternionY * QuaternionY + QuaternionZ * QuaternionZ);

                RollDegrees = Math.Atan2(sinRoll, cosRoll) * 180.0 / Math.PI;
                PitchDegrees = Math.Asin(Math.Max(-1.0,
                    Math.Min(1.0, sinPitch))) * 180.0 / Math.PI;
                HeadingDegrees = Math.Atan2(sinYaw, cosYaw) *
                    180.0 / Math.PI;
                if (HeadingDegrees < 0.0)
                    HeadingDegrees += 360.0;
            }
            LinearAcceleration = new SensorVector3(
                value.LinearAccelerationX * accelerationScale,
                value.LinearAccelerationY * accelerationScale,
                value.LinearAccelerationZ * accelerationScale);
            Gravity = new SensorVector3(
                value.GravityX * accelerationScale,
                value.GravityY * accelerationScale,
                value.GravityZ * accelerationScale);
        }

        public bool IsPresent { get; private set; }
        public bool IsValid { get; private set; }
        public bool IsConfigured { get; private set; }
        public bool UsesExternalClock { get; private set; }
        public uint ChipId { get; private set; }
        public uint OperationMode { get; private set; }
        public uint PowerMode { get; private set; }
        public uint UnitSelection { get; private set; }
        public uint SystemStatus { get; private set; }
        public uint SystemError { get; private set; }
        public uint SelfTest { get; private set; }
        public uint SystemClockStatus { get; private set; }
        public int SystemCalibration { get; private set; }
        public int GyroscopeCalibration { get; private set; }
        public int AccelerometerCalibration { get; private set; }
        public int MagnetometerCalibration { get; private set; }
        public bool HasTemperature { get; private set; }
        public double TemperatureCelsius { get; private set; }
        public SensorVector3 Acceleration { get; private set; }
        public SensorVector3 MagneticField { get; private set; }
        public SensorVector3 Gyroscope { get; private set; }
        public double HeadingDegrees { get; private set; }
        public double RollDegrees { get; private set; }
        public double PitchDegrees { get; private set; }
        public double QuaternionW { get; private set; }
        public double QuaternionX { get; private set; }
        public double QuaternionY { get; private set; }
        public double QuaternionZ { get; private set; }
        public SensorVector3 LinearAcceleration { get; private set; }
        public SensorVector3 Gravity { get; private set; }
    }

    public sealed class NmeaOutputState
    {
        internal NmeaOutputState(TimeCardNmeaControlRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            HasError = (value.Flags & 8u) != 0;
            Baud = value.Baud;
            BaudSelector = value.BaudSelector;
            IsInverted = value.Polarity != 0;
            Control = value.Control;
            Status = value.Status;
            Version = value.Version;
            HasAdvancedConfiguration = (value.Flags & 4u) != 0;
            CorrectionSeconds = value.CorrectionSeconds;
            LocalOffsetMinutes = value.LocalOffsetMinutes;
            Gnss = value.Gnss;
            MessageDisableMask = value.MessageDisableMask;
        }

        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool HasError { get; private set; }
        public uint Baud { get; private set; }
        public uint BaudSelector { get; private set; }
        public bool IsInverted { get; private set; }
        public uint Control { get; private set; }
        public uint Status { get; private set; }
        public uint Version { get; private set; }
        public bool HasAdvancedConfiguration { get; private set; }
        public int CorrectionSeconds { get; private set; }
        public int LocalOffsetMinutes { get; private set; }
        public uint Gnss { get; private set; }
        public uint MessageDisableMask { get; private set; }
        public bool SupportsPolarity { get { return VersionAtLeast(1u, 2u); } }
        public bool SupportsGnss { get { return VersionAtLeast(1u, 3u); } }
        public bool SupportsRmc { get { return VersionAtLeast(1u, 4u); } }
        public bool SupportsUtc { get { return VersionAtLeast(1u, 6u); } }

        private bool VersionAtLeast(uint major, uint minor)
        {
            uint actualMajor = Version >> 24;
            uint actualMinor = (Version >> 16) & 0xffu;
            return Version != 0u && Version != uint.MaxValue &&
                (actualMajor > major ||
                 (actualMajor == major && actualMinor >= minor));
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TimeCardDisciplineLeaseRaw
    {
        public uint Size;
        public uint Action;
        public uint Flags;
        public uint Reserved0;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2)]
        public ulong[] Reserved;
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

    public sealed class SignalGeneratorState
    {
        internal SignalGeneratorState(TimeCardSignalControlRaw value)
        {
            Generator = value.Generator;
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            IsActiveHigh = (value.Flags & 4u) != 0;
            HasError = (value.Flags & 8u) != 0;
            HasTimeJump = (value.Flags & 16u) != 0;
            HasCompletionInterrupt = (value.Flags & 0x80u) != 0;
            HasCompletionPending = (value.Flags & 0x100u) != 0;
            HasCompletionOverflow = (value.Flags & 0x200u) != 0;
            Status = value.Status;
            Version = value.Version;
            RepeatCount = value.RepeatCount;
            PeriodNanoseconds = value.PeriodNanoseconds;
            PulseNanoseconds = value.PulseNanoseconds;
            PhaseNanoseconds = value.PhaseNanoseconds;
            StartSeconds = value.StartSeconds;
            StartNanoseconds = value.StartNanoseconds;
            CableDelayNanoseconds = value.CableDelayNanoseconds;
        }

        public uint Generator { get; private set; }
        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool IsActiveHigh { get; private set; }
        public bool IsInverted
        {
            get { return IsActiveHigh; }
        }
        public bool HasError { get; private set; }
        public bool HasTimeJump { get; private set; }
        public bool HasCompletionInterrupt { get; private set; }
        public bool HasCompletionPending { get; private set; }
        public bool HasCompletionOverflow { get; private set; }
        public uint Status { get; private set; }
        public uint Version { get; private set; }
        public uint RepeatCount { get; private set; }
        public ulong PeriodNanoseconds { get; private set; }
        public ulong PulseNanoseconds { get; private set; }
        public ulong PhaseNanoseconds { get; private set; }
        public ulong StartSeconds { get; private set; }
        public uint StartNanoseconds { get; private set; }
        public uint CableDelayNanoseconds { get; private set; }
        public double FrequencyHz
        {
            get { return PeriodNanoseconds == 0 ? 0 : 1000000000.0 / PeriodNanoseconds; }
        }
        public double DutyPercent
        {
            get { return PeriodNanoseconds == 0 ? 0 : PulseNanoseconds * 100.0 / PeriodNanoseconds; }
        }
    }

    public sealed class SignalCompletionEvent
    {
        internal SignalCompletionEvent(TimeCardSignalEventRaw value)
        {
            SystemInterruptTime100ns = value.SystemInterruptTime100ns;
            Sequence = value.Sequence;
            Generator = value.Generator;
            Flags = value.Flags;
            Status = value.Status;
        }

        public ulong SystemInterruptTime100ns { get; private set; }
        public ulong Sequence { get; private set; }
        public uint Generator { get; private set; }
        public uint Flags { get; private set; }
        public uint Status { get; private set; }
        public bool IsCompleted { get { return (Flags & 1u) != 0; } }
        public bool HasError { get { return (Flags & 2u) != 0; } }
        public bool HasTimeJump { get { return (Flags & 4u) != 0; } }
    }

    public sealed class SignalCompletionBatch
    {
        internal SignalCompletionBatch(TimeCardSignalEventBatchRaw value)
        {
            Generator = value.Generator;
            DroppedEvents = value.DroppedEvents;
            HasOverflow = (value.Flags & 8u) != 0;
            List<SignalCompletionEvent> events =
                new List<SignalCompletionEvent>();
            uint count = Math.Min(value.Count, 16u);
            if (value.Events != null)
            {
                count = Math.Min(count, (uint)value.Events.Length);
                for (uint index = 0; index < count; ++index)
                    events.Add(new SignalCompletionEvent(value.Events[index]));
            }
            Events = events.AsReadOnly();
        }

        public uint Generator { get; private set; }
        public uint DroppedEvents { get; private set; }
        public bool HasOverflow { get; private set; }
        public IList<SignalCompletionEvent> Events { get; private set; }
    }

    public sealed class TimestampEvent
    {
        internal TimestampEvent(TimeCardTimestampEventRaw value)
        {
            Seconds = value.Time.Seconds;
            Nanoseconds = value.Time.Nanoseconds;
            TimestampCount = value.TimestampCount;
            EventCount = value.EventCount;
            Error = value.Error;
            DataWidth = value.DataWidth;
            Flags = value.Flags;
            Data = value.Data == null ? new uint[0] :
                (uint[])value.Data.Clone();
        }

        public ulong Seconds { get; private set; }
        public uint Nanoseconds { get; private set; }
        public uint TimestampCount { get; private set; }
        public uint EventCount { get; private set; }
        public uint Error { get; private set; }
        public uint DataWidth { get; private set; }
        public uint Flags { get; private set; }
        public uint[] Data { get; private set; }
        public bool IsValid { get { return (Flags & 8u) != 0; } }
        public bool HasDropError { get { return (Flags & 4u) != 0; } }
        public bool IsDataValid { get { return (Flags & 16u) != 0; } }
    }

    public sealed class TimestampChannelState
    {
        internal TimestampChannelState(TimeCardTimestampControlRaw value)
        {
            Channel = value.Channel;
            Flags = value.Flags;
            Status = value.Status;
            Version = value.Version;
            Polarity = value.Polarity;
            CableDelayNanoseconds = value.CableDelayNanoseconds;
            Interrupt = value.Interrupt;
            InterruptMask = value.InterruptMask;
            EventCount = value.EventCount;
            TimestampCount = value.TimestampCount;
            QueueDepth = value.QueueDepth;
            DroppedEvents = value.DroppedEvents;
            DataWidth = value.DataWidth;
            Seconds = value.Time.Seconds;
            Nanoseconds = value.Time.Nanoseconds;
            Data = value.Data;
        }

        public uint Channel { get; private set; }
        public uint Flags { get; private set; }
        public uint Status { get; private set; }
        public uint Version { get; private set; }
        public uint Polarity { get; private set; }
        public uint CableDelayNanoseconds { get; private set; }
        public uint Interrupt { get; private set; }
        public uint InterruptMask { get; private set; }
        public uint EventCount { get; private set; }
        public uint TimestampCount { get; private set; }
        public uint QueueDepth { get; private set; }
        public uint DroppedEvents { get; private set; }
        public uint DataWidth { get; private set; }
        public ulong Seconds { get; private set; }
        public uint Nanoseconds { get; private set; }
        public uint Data { get; private set; }
        public bool IsPresent { get { return (Flags & 1u) != 0; } }
        public bool IsEnabled { get { return (Flags & 2u) != 0; } }
        public bool HasDropError { get { return (Flags & 4u) != 0; } }
        public bool HasValidEvent { get { return (Flags & 8u) != 0; } }
        public bool HasInterrupt { get { return (Flags & 0x20u) != 0; } }
        public bool HasQueueOverflow { get { return (Flags & 0x40u) != 0; } }
        public bool IsCableDelayWritable { get { return (Flags & 0x100u) != 0; } }
        public bool HasCounters { get { return (Flags & 0x200u) != 0; } }
        public bool HasDataSurface { get { return (Flags & 0x400u) != 0; } }
    }

    public sealed class TimestampEventBatch
    {
        internal TimestampEventBatch(TimeCardTimestampBatchRaw value)
        {
            Channel = value.Channel;
            DroppedEvents = value.DroppedEvents;
            HasOverflow = (value.Flags & 0x40u) != 0;
            List<TimestampEvent> events = new List<TimestampEvent>();
            uint count = Math.Min(value.Count, 16u);
            if (value.Events != null)
            {
                count = Math.Min(count, (uint)value.Events.Length);
                for (uint index = 0; index < count; ++index)
                    events.Add(new TimestampEvent(value.Events[index]));
            }
            Events = events.AsReadOnly();
        }

        public uint Channel { get; private set; }
        public uint DroppedEvents { get; private set; }
        public bool HasOverflow { get; private set; }
        public IList<TimestampEvent> Events { get; private set; }
    }

    public sealed class ClockAdjustmentState
    {
        internal ClockAdjustmentState(TimeCardClockAdjustmentRaw value)
        {
            Flags = value.Flags;
            Version = value.Version;
            Control = value.Control;
            Select = value.Select;
            OffsetNanoseconds = value.OffsetNanoseconds;
            OffsetIntervalNanoseconds = value.OffsetIntervalNanoseconds;
            DriftPpbQ16 = value.DriftPpbQ16;
            DriftIntervalNanoseconds = value.DriftIntervalNanoseconds;
            InSyncThresholdNanoseconds = value.InSyncThresholdNanoseconds;
            AppliedFlags = value.AppliedFlags;
        }

        public uint Flags { get; private set; }
        public uint Version { get; private set; }
        public uint Control { get; private set; }
        public uint Select { get; private set; }
        public int OffsetNanoseconds { get; private set; }
        public uint OffsetIntervalNanoseconds { get; private set; }
        public long DriftPpbQ16 { get; private set; }
        public double DriftPpb { get { return DriftPpbQ16 / 65536.0; } }
        public uint DriftIntervalNanoseconds { get; private set; }
        public uint InSyncThresholdNanoseconds { get; private set; }
        public uint AppliedFlags { get; private set; }
        public bool IsPresent { get { return (Flags & 1u) != 0; } }
        public bool HasFractionalDrift { get { return (Flags & 2u) != 0; } }
    }

    public sealed class ClockAdvancedState
    {
        internal ClockAdvancedState(TimeCardClockAdvancedControlRaw value)
        {
            Flags = value.Flags; Version = value.Version;
            Control = value.Control; Status = value.Status;
            ApplyFlags = value.ApplyFlags;
            OffsetRateLimiter = value.OffsetRateLimiter;
            DriftRateLimiterQ16 = value.DriftRateLimiterQ16;
            AgingConfiguration = value.AgingConfiguration;
            HoldoverConfiguration = value.HoldoverConfiguration;
            OffsetOutlierFilter = value.OffsetOutlierFilter;
            DriftOutlierFilter = value.DriftOutlierFilter;
            DynamicControl = value.DynamicControl;
            ServoOffsetP = value.ServoOffsetP; ServoOffsetI = value.ServoOffsetI;
            ServoDriftP = value.ServoDriftP; ServoDriftI = value.ServoDriftI;
            StatusOffset = value.StatusOffset; StatusDrift = value.StatusDrift;
            StatusOffsetFraction = value.StatusOffsetFraction;
            StatusDriftFraction = value.StatusDriftFraction;
            StatusHoldover = value.StatusHoldover;
            StatusHoldoverFraction = value.StatusHoldoverFraction;
            StatusHoldoverSamples = value.StatusHoldoverSamples;
            StatusOffsetOutliers = value.StatusOffsetOutliers;
            StatusDriftOutliers = value.StatusDriftOutliers;
            StatusAgingLow = value.StatusAgingLow;
            StatusAgingHigh = value.StatusAgingHigh;
            StatusAgingSamples = value.StatusAgingSamples;
        }

        public uint Flags { get; private set; }
        public uint Version { get; private set; }
        public uint Control { get; private set; }
        public uint Status { get; private set; }
        public uint ApplyFlags { get; private set; }
        public uint OffsetRateLimiter { get; private set; }
        public uint DriftRateLimiterQ16 { get; private set; }
        public uint AgingConfiguration { get; private set; }
        public uint HoldoverConfiguration { get; private set; }
        public uint OffsetOutlierFilter { get; private set; }
        public uint DriftOutlierFilter { get; private set; }
        public uint DynamicControl { get; private set; }
        public uint ServoOffsetP { get; private set; }
        public uint ServoOffsetI { get; private set; }
        public uint ServoDriftP { get; private set; }
        public uint ServoDriftI { get; private set; }
        public uint StatusOffset { get; private set; }
        public uint StatusDrift { get; private set; }
        public uint StatusOffsetFraction { get; private set; }
        public uint StatusDriftFraction { get; private set; }
        public uint StatusHoldover { get; private set; }
        public uint StatusHoldoverFraction { get; private set; }
        public uint StatusHoldoverSamples { get; private set; }
        public uint StatusOffsetOutliers { get; private set; }
        public uint StatusDriftOutliers { get; private set; }
        public uint StatusAgingLow { get; private set; }
        public uint StatusAgingHigh { get; private set; }
        public uint StatusAgingSamples { get; private set; }
        public bool HasRateLimiters { get { return (Flags & 2u) != 0; } }
        public bool HasHoldover { get { return (Flags & 4u) != 0; } }
        public bool HasOutlierFilters { get { return (Flags & 8u) != 0; } }
        public bool HasServoFactors { get { return (Flags & 0x10u) != 0; } }
        public bool HasServoLog { get { return (Flags & 0x20u) != 0; } }
        public bool HasAging { get { return (Flags & 0x40u) != 0; } }
        public bool HasDynamicControl { get { return (Flags & 0x80u) != 0; } }
        public bool HasRevert { get { return (Flags & 0x100u) != 0; } }
        public bool IsHoldoverReady { get { return (Flags & 0x200u) != 0; } }
        public bool IsAgingReady { get { return (Flags & 0x400u) != 0; } }
    }

    public sealed class CoreDescriptor
    {
        internal CoreDescriptor(TimeCardCoreDescriptorRaw value)
        {
            Type = value.Type; Instance = value.Instance;
            RegisterOffset = value.RegisterOffset;
            RegisterSpan = value.RegisterSpan;
            InterruptMessage = value.InterruptMessage;
            Version = value.Version; Flags = value.Flags;
        }

        public uint Type { get; private set; }
        public uint Instance { get; private set; }
        public uint RegisterOffset { get; private set; }
        public uint RegisterSpan { get; private set; }
        public uint InterruptMessage { get; private set; }
        public uint Version { get; private set; }
        public uint Flags { get; private set; }
        public string Name
        {
            get
            {
                string[] names = { "Unknown", "Adjustable Clock", "Image Identity",
                    "Signal Timestamper", "PPS Master", "PPS Slave", "ToD Slave",
                    "ToD Master", "IRIG Master", "IRIG Slave", "DCF Master",
                    "DCF Slave", "Signal Generator", "Frequency Input" };
                return Type < names.Length ? names[Type] : "Core " + Type.ToString(
                    CultureInfo.InvariantCulture);
            }
        }
        public bool HasInterrupt { get { return InterruptMessage != uint.MaxValue; } }
    }

    public sealed class CoreInventory
    {
        internal CoreInventory(TimeCardCoreInventoryRaw value)
        {
            AbiVersion = value.AbiVersion; Flags = value.Flags;
            BoardProfile = value.BoardProfile; Layout = value.Layout;
            RawImageVersion = value.RawImageVersion;
            List<CoreDescriptor> cores = new List<CoreDescriptor>();
            uint count = Math.Min(value.Count, 32u);
            if (value.Cores != null)
            {
                count = Math.Min(count, (uint)value.Cores.Length);
                for (uint index = 0; index < count; ++index)
                    cores.Add(new CoreDescriptor(value.Cores[index]));
            }
            Cores = cores.AsReadOnly();
        }

        public uint AbiVersion { get; private set; }
        public uint Flags { get; private set; }
        public uint BoardProfile { get; private set; }
        public uint Layout { get; private set; }
        public uint RawImageVersion { get; private set; }
        public IList<CoreDescriptor> Cores { get; private set; }
        public bool IsStaticProfile { get { return (Flags & 1u) != 0; } }
        public bool HasConfigurationSlave { get { return (Flags & 2u) == 0; } }
    }

    public sealed class FpgaImageContract
    {
        internal FpgaImageContract(TimeCardFpgaImageContractRaw value)
        {
            AbiVersion = value.AbiVersion;
            RawImageVersion = value.RawImageVersion;
            CapabilityFlags = value.CapabilityFlags;
            EffectiveFlags = value.EffectiveFlags;
            StatusFlags = value.StatusFlags;
            BoardProfile = value.BoardProfile;
            Layout = value.Layout;
        }

        public uint AbiVersion { get; private set; }
        public uint RawImageVersion { get; private set; }
        public uint CapabilityFlags { get; private set; }
        public uint EffectiveFlags { get; private set; }
        public uint StatusFlags { get; private set; }
        public uint BoardProfile { get; private set; }
        public uint Layout { get; private set; }
        public bool HasImage { get { return (StatusFlags & 1u) != 0; } }
        public bool IsExactMatch { get { return (StatusFlags & 2u) != 0; } }
        public bool IsActive { get { return (StatusFlags & 4u) != 0; } }
        public bool Allows(uint flag) { return (EffectiveFlags & flag) == flag; }
    }

    public sealed class NmeaUtcState
    {
        internal NmeaUtcState(TimeCardNmeaUtcControlRaw value)
        {
            Flags = value.Flags; Version = value.Version;
            RawUtcInfo = value.RawUtcInfo;
            UtcOffsetSeconds = value.UtcOffsetSeconds;
            HandshakeControl = value.HandshakeControl;
        }

        public uint Flags { get; private set; }
        public uint Version { get; private set; }
        public uint RawUtcInfo { get; private set; }
        public uint UtcOffsetSeconds { get; private set; }
        public uint HandshakeControl { get; private set; }
        public bool IsPresent { get { return (Flags & 1u) != 0; } }
        public bool CanRead { get { return (Flags & 2u) != 0; } }
        public bool CanWrite { get { return (Flags & 4u) != 0; } }
        public bool Leap61 { get { return (Flags & 8u) != 0; } }
        public bool Leap59 { get { return (Flags & 0x10u) != 0; } }
        public bool IsOffsetValid { get { return (Flags & 0x20u) != 0; } }
    }

    public sealed class FpgaCapabilities
    {
        internal FpgaCapabilities(TimeCardFpgaCapabilitiesRaw value)
        {
            AbiVersion = value.AbiVersion;
            CoreMask = value.CoreMask;
            FeatureFlags = value.FeatureFlags;
            KnownGaps = value.KnownGaps;
            Layout = value.Layout;
            BoardProfile = value.BoardProfile;
        }

        public uint AbiVersion { get; private set; }
        public uint CoreMask { get; private set; }
        public uint FeatureFlags { get; private set; }
        public uint KnownGaps { get; private set; }
        public uint Layout { get; private set; }
        public uint BoardProfile { get; private set; }
    }

    public sealed class FpgaImageInfo
    {
        internal FpgaImageInfo(TimeCardFpgaImageInfoRaw value)
        {
            AbiVersion = value.AbiVersion;
            Flags = value.Flags;
            RawVersion = value.RawVersion;
            ImageTag = value.ImageTag;
            ImageVersion = value.ImageVersion;
            Layout = value.Layout;
            BoardProfile = value.BoardProfile;
            RegisterOffset = value.RegisterOffset;
        }

        public uint AbiVersion { get; private set; }
        public uint Flags { get; private set; }
        public uint RawVersion { get; private set; }
        public uint ImageTag { get; private set; }
        public uint ImageVersion { get; private set; }
        public uint Layout { get; private set; }
        public uint BoardProfile { get; private set; }
        public uint RegisterOffset { get; private set; }
        public bool IsPresent { get { return (Flags & 1u) != 0; } }
        public bool IsLoader { get { return (Flags & 2u) != 0; } }
        public bool IsFpgaFirmware { get { return (Flags & 4u) != 0; } }
        public string ImageKind
        {
            get
            {
                string kind = IsFpgaFirmware ? "FPGA" : "SOM";
                return IsLoader ? kind + " loader" : kind + " firmware";
            }
        }
        public string VersionText
        {
            get
            {
                return string.Format(CultureInfo.InvariantCulture, "{0}.{1}",
                    ImageVersion >> 8, ImageVersion & 0xffu);
            }
        }
    }

    public sealed class ClockTelemetryState
    {
        internal ClockTelemetryState(TimeCardClockTelemetryRaw value)
        {
            Flags = value.Flags;
            Version = value.Version;
            Control = value.Control;
            Status = value.Status;
            Select = value.Select;
            CoreMask = value.CoreMask;
            KnownGaps = value.KnownGaps;
            InSyncThreshold = value.InSyncThreshold;
            ServoOffsetP = value.ServoOffsetP;
            ServoOffsetI = value.ServoOffsetI;
            ServoDriftP = value.ServoDriftP;
            ServoDriftI = value.ServoDriftI;
            StatusOffsetNanoseconds = value.StatusOffsetNanoseconds;
            StatusDriftPpb = value.StatusDriftPpb;
            StatusOffsetFraction = value.StatusOffsetFraction;
            StatusDriftFraction = value.StatusDriftFraction;
        }

        public uint Flags { get; private set; }
        public uint Version { get; private set; }
        public uint Control { get; private set; }
        public uint Status { get; private set; }
        public uint Select { get; private set; }
        public uint CoreMask { get; private set; }
        public uint KnownGaps { get; private set; }
        public uint InSyncThreshold { get; private set; }
        public uint ServoOffsetP { get; private set; }
        public uint ServoOffsetI { get; private set; }
        public uint ServoDriftP { get; private set; }
        public uint ServoDriftI { get; private set; }
        public int StatusOffsetNanoseconds { get; private set; }
        public int StatusDriftPpb { get; private set; }
        public uint StatusOffsetFraction { get; private set; }
        public uint StatusDriftFraction { get; private set; }
    }

    public sealed class PpsEngineState
    {
        internal PpsEngineState(TimeCardPpsControlRaw value)
        {
            Core = value.Core;
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            HasError = (value.Flags & 4u) != 0;
            HasFilterError = (value.Flags & 8u) != 0;
            HasSupervisionError = (value.Flags & 16u) != 0;
            IsPulseWidthWritable = (value.Flags & 32u) != 0;
            Control = value.Control;
            Status = value.Status;
            Version = value.Version;
            IsActiveHigh = value.Polarity != 0;
            PulseWidthMilliseconds = value.PulseWidthMilliseconds;
            CableDelayNanoseconds = value.CableDelayNanoseconds;
        }

        public uint Core { get; private set; }
        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool HasError { get; private set; }
        public bool HasFilterError { get; private set; }
        public bool HasSupervisionError { get; private set; }
        public bool IsPulseWidthWritable { get; private set; }
        public uint Control { get; private set; }
        public uint Status { get; private set; }
        public uint Version { get; private set; }
        public bool IsActiveHigh { get; private set; }
        public uint PulseWidthMilliseconds { get; private set; }
        public int CableDelayNanoseconds { get; private set; }
    }

    public sealed class TimecodeEngineState
    {
        internal TimecodeEngineState(TimeCardTimecodeControlRaw value)
        {
            Format = value.Format;
            Role = value.Role;
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            HasError = (value.Flags & 4u) != 0;
            IsDelayWritable = (value.Flags & 8u) != 0;
            IsControlBitsWritable = (value.Flags & 16u) != 0;
            IsAmplitudeModulationWritable = (value.Flags & 32u) != 0;
            IsManualYearWritable = (value.Flags & 64u) != 0;
            Control = value.Control;
            Status = value.Status;
            Version = value.Version;
            Mode = value.Mode;
            Code = value.Code;
            CorrectionSeconds = value.CorrectionSeconds;
            DelayNanoseconds = value.DelayNanoseconds;
            ControlBits = value.ControlBits;
            BitPosition = value.BitPosition;
            AmplitudeModulation = value.AmplitudeModulation;
            ManualYear = value.ManualYear;
        }

        public uint Format { get; private set; }
        public uint Role { get; private set; }
        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool HasError { get; private set; }
        public bool IsDelayWritable { get; private set; }
        public bool IsControlBitsWritable { get; private set; }
        public bool IsAmplitudeModulationWritable { get; private set; }
        public bool IsManualYearWritable { get; private set; }
        public uint Control { get; private set; }
        public uint Status { get; private set; }
        public uint Version { get; private set; }
        public uint Mode { get; private set; }
        public uint Code { get; private set; }
        public int CorrectionSeconds { get; private set; }
        public int DelayNanoseconds { get; private set; }
        public uint ControlBits { get; private set; }
        public uint BitPosition { get; private set; }
        public uint AmplitudeModulation { get; private set; }
        public uint ManualYear { get; private set; }
    }

    public sealed class TodParserState
    {
        internal TodParserState(TimeCardTodControlRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            HasParseError = (value.Flags & 4u) != 0;
            HasChecksumError = (value.Flags & 8u) != 0;
            HasUartError = (value.Flags & 16u) != 0;
            HasUtcTelemetry = (value.Flags & 32u) != 0;
            HasGnssTelemetry = (value.Flags & 64u) != 0;
            Control = value.Control;
            Status = value.Status;
            Version = value.Version;
            Protocol = value.Protocol;
            Gnss = value.Gnss;
            Baud = value.Baud;
            BaudSelector = value.BaudSelector;
            IsInverted = value.Polarity != 0;
            CorrectionSeconds = value.CorrectionSeconds;
            MessageDisableMask = value.MessageDisableMask;
            UtcStatus = value.UtcStatus;
            TimeToLeapSeconds = value.TimeToLeapSeconds;
            GnssStatus = value.GnssStatus;
            Satellites = value.Satellites;
        }

        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool HasParseError { get; private set; }
        public bool HasChecksumError { get; private set; }
        public bool HasUartError { get; private set; }
        public bool HasUtcTelemetry { get; private set; }
        public bool HasGnssTelemetry { get; private set; }
        public uint Control { get; private set; }
        public uint Status { get; private set; }
        public uint Version { get; private set; }
        public uint Protocol { get; private set; }
        public uint Gnss { get; private set; }
        public uint Baud { get; private set; }
        public uint BaudSelector { get; private set; }
        public bool IsInverted { get; private set; }
        public int CorrectionSeconds { get; private set; }
        public uint MessageDisableMask { get; private set; }
        public uint UtcStatus { get; private set; }
        public int TimeToLeapSeconds { get; private set; }
        public uint GnssStatus { get; private set; }
        public uint Satellites { get; private set; }
    }

    public sealed class FrequencyCounterState
    {
        internal FrequencyCounterState(TimeCardFrequencyControlRaw value)
        {
            Counter = value.Counter;
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            IsValid = (value.Flags & 4u) != 0;
            HasError = (value.Flags & 8u) != 0;
            HasOverrun = (value.Flags & 16u) != 0;
            IntegrationSeconds = value.IntegrationSeconds;
            FrequencyHz = value.FrequencyHz;
            Control = value.Control;
            Status = value.Status;
        }

        public uint Counter { get; private set; }
        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool IsValid { get; private set; }
        public bool HasError { get; private set; }
        public bool HasOverrun { get; private set; }
        public uint IntegrationSeconds { get; private set; }
        public uint FrequencyHz { get; private set; }
        public uint Control { get; private set; }
        public uint Status { get; private set; }
    }

    public sealed class FlashDeviceStatus
    {
        internal FlashDeviceStatus(TimeCardFlashStatusRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsIdentified = (value.Flags & 2u) != 0;
            IsSupported = (value.Flags & 4u) != 0;
            UsesFourByteAddressing = (value.Flags & 8u) != 0;
            ControllerOffset = value.Offset;
            JedecId = value.JedecId;
            CapacityBytes = value.CapacityBytes;
            FirmwareOffset = value.FirmwareOffset;
            EraseSize = value.EraseSize;
            PageSize = value.PageSize;
            ControllerStatus = value.ControllerStatus;
            FlashStatus = value.FlashStatus;
            FifoDepth = value.FifoDepth;
        }

        public bool IsPresent { get; private set; }
        public bool IsIdentified { get; private set; }
        public bool IsSupported { get; private set; }
        public bool UsesFourByteAddressing { get; private set; }
        public uint ControllerOffset { get; private set; }
        public uint JedecId { get; private set; }
        public uint CapacityBytes { get; private set; }
        public uint FirmwareOffset { get; private set; }
        public uint EraseSize { get; private set; }
        public uint PageSize { get; private set; }
        public uint ControllerStatus { get; private set; }
        public uint FlashStatus { get; private set; }
        public uint FifoDepth { get; private set; }
    }

    public sealed class UartObservation
    {
        internal UartObservation(TimeCardUartObserveRaw value)
        {
            Port = value.Port;
            IsPresent = (value.Flags & 1u) != 0;
            HasActivity = (value.Flags & 2u) != 0;
            LineStatus = value.LineStatus;
        }

        public uint Port { get; private set; }
        public bool IsPresent { get; private set; }
        public bool HasActivity { get; private set; }
        public uint LineStatus { get; private set; }
    }

    public sealed class Mro50Status
    {
        internal Mro50Status(TimeCardMro50StatusRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            IsLocked = (value.Flags & 4u) != 0;
            IsFineValid = (value.Flags & 8u) != 0;
            IsCoarseValid = (value.Flags & 16u) != 0;
            IsSerialRouteEnabled = (value.Flags & 32u) != 0;
            Control = value.Control;
            FineAdjustment = value.FineAdjustment;
            CoarseAdjustment = value.CoarseAdjustment;
            TemperatureRaw = value.Temperature;
            BoardConfig = value.BoardConfig;
        }

        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool IsLocked { get; private set; }
        public bool IsFineValid { get; private set; }
        public bool IsCoarseValid { get; private set; }
        public bool IsSerialRouteEnabled { get; private set; }
        public uint Control { get; private set; }
        public uint FineAdjustment { get; private set; }
        public uint CoarseAdjustment { get; private set; }
        public uint TemperatureRaw { get; private set; }
        public uint BoardConfig { get; private set; }
    }

    public sealed class TimeCardCapabilities
    {
        internal TimeCardCapabilities(TimeCardCapabilitiesRaw value)
        {
            AbiVersion = value.AbiVersion;
            Flags = value.Flags;
            BoardProfile = value.BoardProfile;
            OscillatorType = value.OscillatorType;
            ReferencePpsIndex = value.ReferencePpsIndex;
            OscillatorPpsIndex = value.OscillatorPpsIndex;
            FineMinimum = value.FineMinimum;
            FineMaximum = value.FineMaximum;
            CoarseMinimum = value.CoarseMinimum;
            CoarseMaximum = value.CoarseMaximum;
        }

        public uint AbiVersion { get; private set; }
        public ulong Flags { get; private set; }
        public uint BoardProfile { get; private set; }
        public uint OscillatorType { get; private set; }
        public uint ReferencePpsIndex { get; private set; }
        public uint OscillatorPpsIndex { get; private set; }
        public uint FineMinimum { get; private set; }
        public uint FineMaximum { get; private set; }
        public uint CoarseMinimum { get; private set; }
        public uint CoarseMaximum { get; private set; }
        public bool HasPhc { get { return Has(1ul << 0); } }
        public bool HasGnssUart { get { return Has(1ul << 1); } }
        public bool HasAtomicUart { get { return Has(1ul << 2); } }
        public bool HasPairedPhaseMeter { get { return Has(1ul << 3); } }
        public bool HasDirectMro50 { get { return Has(1ul << 4); } }
        public bool CanAdjustPhcPhase { get { return Has(1ul << 5); } }
        public bool HasDisciplineParameters { get { return Has(1ul << 6); } }
        public bool HasTemperatureTelemetry { get { return Has(1ul << 7); } }
        public bool HasHardwareDiscipline { get { return Has(1ul << 8); } }

        private bool Has(ulong flag) { return (Flags & flag) != 0; }

        public string BoardName
        {
            get
            {
                return BoardProfile == 2u ? "Orolia/Safran ART" :
                    BoardProfile == 3u ? "Celestica Time Card" :
                    BoardProfile == 1u ? "OCP Time Card" : "Unknown Time Card";
            }
        }

        public string OscillatorName
        {
            get
            {
                return OscillatorType == 2u ? "Microchip mRO-50" :
                    OscillatorType == 3u ? "Microchip SA53" :
                    OscillatorType == 1u ? "UART oscillator (probe required)" :
                    "No controllable oscillator";
            }
        }
    }

    public sealed class TimeCardPhaseSample
    {
        internal TimeCardPhaseSample(TimeCardPhaseSampleRaw value)
        {
            Flags = value.Flags;
            ReferenceCounter = value.ReferenceCounter;
            OscillatorCounter = value.OscillatorCounter;
            ReferenceTime = value.ReferenceTime;
            OscillatorTime = value.OscillatorTime;
            PhaseNanoseconds = value.PhaseNanoseconds;
            ReferenceError = value.ReferenceError;
            OscillatorError = value.OscillatorError;
        }

        internal TimeCardTimeRaw ReferenceTime { get; private set; }
        internal TimeCardTimeRaw OscillatorTime { get; private set; }
        public uint Flags { get; private set; }
        public uint ReferenceCounter { get; private set; }
        public uint OscillatorCounter { get; private set; }
        public long PhaseNanoseconds { get; private set; }
        public uint ReferenceError { get; private set; }
        public uint OscillatorError { get; private set; }
        public bool IsPresent { get { return (Flags & 1u) != 0; } }
        public bool IsEnabled { get { return (Flags & 2u) != 0; } }
        public bool IsReferenceValid { get { return (Flags & 4u) != 0; } }
        public bool IsOscillatorValid { get { return (Flags & 8u) != 0; } }
        public bool IsPhaseValid { get { return (Flags & 16u) != 0; } }
    }

    public sealed class TimeCardDisciplineParameters
    {
        internal TimeCardDisciplineParameters(TimeCardDisciplineBlobRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            Data = value.Data == null ? new byte[0] :
                (byte[])value.Data.Clone();
        }

        public bool IsPresent { get; private set; }
        public bool IsValid { get; private set; }
        public byte[] Data { get; private set; }
    }

    public sealed class DisciplineLeaseState
    {
        internal DisciplineLeaseState(TimeCardDisciplineLeaseRaw value)
        {
            IsActive = (value.Flags & 1u) != 0u;
            IsOwner = (value.Flags & 2u) != 0u;
        }

        public bool IsActive { get; private set; }
        public bool IsOwner { get; private set; }
    }

    public sealed class TimeCardSnapshot
    {
        internal TimeCardSnapshot(TimeCardInfoRaw info,
                                  TimeCardCrossTimestampRaw timestamp,
                                  TimeCardHierarchyRaw hierarchy,
                                  ulong sampleTickMilliseconds)
        {
            AbiVersion = info.AbiVersion;
            DriverVersion = string.Format("{0}.{1}", info.DriverVersion >> 16, info.DriverVersion & 0xffff);
            Layout = info.Layout == 3 ? "Orolia ART" :
                info.Layout == 2 ? "MSI-X" :
                info.Layout == 1 ? "MSI" : "Unknown";
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
            TodCoreAvailable = info.TodVersion != uint.MaxValue;
            TodTelemetryAvailable = TodCoreAvailable &&
                info.UtcStatus != uint.MaxValue &&
                info.Leap != uint.MaxValue;
            GnssTelemetryAvailable = TodCoreAvailable &&
                info.GnssStatus != uint.MaxValue &&
                info.Satellites != uint.MaxValue;
            SeenSatellites = GnssTelemetryAvailable ?
                (int)(info.Satellites & 0xff) : 0;
            LockedSatellites = GnssTelemetryAvailable ?
                (int)((info.Satellites >> 8) & 0xff) : 0;
            SatelliteDataValid = GnssTelemetryAvailable &&
                (info.Satellites & (1u << 16)) != 0;
            GnssFixOk = GnssTelemetryAvailable &&
                (info.GnssStatus & (1u << 16)) != 0;
            GnssFixCode = GnssTelemetryAvailable ?
                (int)((info.GnssStatus >> 17) & 0xff) : -1;
            string[] fixNames = { "No fix", "Dead reckoning", "2-D fix", "3-D fix",
                "GPS + dead reckoning", "Unknown" };
            GnssFix = !GnssTelemetryAvailable ? "Not available" :
                GnssFixCode >= 0 && GnssFixCode < fixNames.Length - 1
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
            SampleTickMilliseconds = sampleTickMilliseconds;
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
        public bool IsClockSynchronized
        {
            get { return ClockStatus != uint.MaxValue && (ClockStatus & 1) != 0; }
        }
        public uint ClockSource { get; private set; }
        public uint TodVersion { get; private set; }
        public uint TodStatus { get; private set; }
        public uint UtcStatus { get; private set; }
        public uint Leap { get; private set; }
        public bool TodCoreAvailable { get; private set; }
        public bool TodTelemetryAvailable { get; private set; }
        public uint GnssStatus { get; private set; }
        public uint Satellites { get; private set; }
        public bool GnssTelemetryAvailable { get; private set; }
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
        public ulong SampleTickMilliseconds { get; private set; }
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
