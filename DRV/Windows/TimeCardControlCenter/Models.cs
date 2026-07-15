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
        public uint Reserved;
        public ulong PeriodNanoseconds;
        public ulong PulseNanoseconds;
        public ulong PhaseNanoseconds;
        public ulong StartSeconds;
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
    }

    public sealed class EnvironmentSensorReading
    {
        internal EnvironmentSensorReading(TimeCardBme280ReadingRaw value)
        {
            IsPresent = (value.Flags & 1u) != 0;
            IsValid = (value.Flags & 2u) != 0;
            IsConfigured = (value.Flags & 4u) != 0;
            IsConversionReady = (value.Flags & 8u) != 0;
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

        public bool IsPresent { get; private set; }
        public bool IsValid { get; private set; }
        public bool IsConfigured { get; private set; }
        public bool IsConversionReady { get; private set; }
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
            TemperatureCelsius = (value.UnitSelection & 0x20u) != 0
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

    public sealed class SignalGeneratorState
    {
        internal SignalGeneratorState(TimeCardSignalControlRaw value)
        {
            Generator = value.Generator;
            IsPresent = (value.Flags & 1u) != 0;
            IsEnabled = (value.Flags & 2u) != 0;
            IsInverted = (value.Flags & 4u) != 0;
            Status = value.Status;
            Version = value.Version;
            RepeatCount = value.RepeatCount;
            PeriodNanoseconds = value.PeriodNanoseconds;
            PulseNanoseconds = value.PulseNanoseconds;
            PhaseNanoseconds = value.PhaseNanoseconds;
            StartSeconds = value.StartSeconds;
            StartNanoseconds = value.StartNanoseconds;
        }

        public uint Generator { get; private set; }
        public bool IsPresent { get; private set; }
        public bool IsEnabled { get; private set; }
        public bool IsInverted { get; private set; }
        public uint Status { get; private set; }
        public uint Version { get; private set; }
        public uint RepeatCount { get; private set; }
        public ulong PeriodNanoseconds { get; private set; }
        public ulong PulseNanoseconds { get; private set; }
        public ulong PhaseNanoseconds { get; private set; }
        public ulong StartSeconds { get; private set; }
        public uint StartNanoseconds { get; private set; }
        public double FrequencyHz
        {
            get { return PeriodNanoseconds == 0 ? 0 : 1000000000.0 / PeriodNanoseconds; }
        }
        public double DutyPercent
        {
            get { return PeriodNanoseconds == 0 ? 0 : PulseNanoseconds * 100.0 / PeriodNanoseconds; }
        }
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
