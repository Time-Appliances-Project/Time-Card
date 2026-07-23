/* SPDX-License-Identifier: BSD-3-Clause */
/* Public IOCTL ABI for the OCP TimeCard Windows driver. */

#ifndef TIMECARD_IOCTL_H
#define TIMECARD_IOCTL_H

#if defined(_KERNEL_MODE)
#include <devioctl.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#define TIMECARD_NT_DEVICE_NAME   L"\\Device\\TimeCard0"
#define TIMECARD_DOS_DEVICE_NAME  L"\\DosDevices\\TimeCard0"
#define TIMECARD_USER_DEVICE_PATH L"\\\\.\\TimeCard0"

#define TIMECARD_IOCTL_INDEX 0x800u
#define TIMECARD_IOCTL(n, access) \
    CTL_CODE(FILE_DEVICE_UNKNOWN, TIMECARD_IOCTL_INDEX + (n), \
             METHOD_BUFFERED, (access))

#define IOCTL_TIMECARD_GET_TIME \
    TIMECARD_IOCTL(0, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_SET_TIME \
    TIMECARD_IOCTL(1, FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_GET_INFO \
    TIMECARD_IOCTL(2, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_GET_CROSSTIMESTAMP \
    TIMECARD_IOCTL(3, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_UART_CONFIGURE \
    TIMECARD_IOCTL(4, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_UART_READ \
    TIMECARD_IOCTL(5, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_UART_WRITE \
    TIMECARD_IOCTL(6, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_HIERARCHY_CONTROL \
    TIMECARD_IOCTL(7, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_SMA_QUERY \
    TIMECARD_IOCTL(8, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_SMA_SET \
    TIMECARD_IOCTL(9, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_I2C_GET_STATUS \
    TIMECARD_IOCTL(10, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_I2C_PROBE \
    TIMECARD_IOCTL(11, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_I2C_READ \
    TIMECARD_IOCTL(12, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_CLOCK_SOURCE_SET \
    TIMECARD_IOCTL(13, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_NMEA_QUERY \
    TIMECARD_IOCTL(14, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_NMEA_SET \
    TIMECARD_IOCTL(15, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_GET_IDENTITY \
    TIMECARD_IOCTL(16, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_SIGNAL_QUERY \
    TIMECARD_IOCTL(17, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_SIGNAL_SET \
    TIMECARD_IOCTL(18, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FREQUENCY_QUERY \
    TIMECARD_IOCTL(19, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FREQUENCY_SET \
    TIMECARD_IOCTL(20, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FLASH_QUERY \
    TIMECARD_IOCTL(21, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_FLASH_READ \
    TIMECARD_IOCTL(22, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FLASH_ERASE \
    TIMECARD_IOCTL(23, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_FLASH_PROGRAM \
    TIMECARD_IOCTL(24, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_UART_OBSERVE \
    TIMECARD_IOCTL(25, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_I2C_MUX_QUERY \
    TIMECARD_IOCTL(26, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_I2C_MUX_SET \
    TIMECARD_IOCTL(27, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_LED_QUERY \
    TIMECARD_IOCTL(28, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_LED_SET \
    TIMECARD_IOCTL(29, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_SENSOR_QUERY \
    TIMECARD_IOCTL(30, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_TIMECARD_MRO50_QUERY \
    TIMECARD_IOCTL(31, FILE_READ_ACCESS)
#define IOCTL_TIMECARD_MRO50_CONTROL \
    TIMECARD_IOCTL(32, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define TIMECARD_ABI_VERSION 9u
#define TIMECARD_LAYOUT_MSI  1u
#define TIMECARD_LAYOUT_MSIX 2u
#define TIMECARD_LAYOUT_ART  3u
#define TIMECARD_UART_COUNT  4u
#define TIMECARD_UART_MAX_TRANSFER 256u
#define TIMECARD_I2C_MAX_TRANSFER 255u

#define TIMECARD_UART_GNSS  0u
#define TIMECARD_UART_GNSS2 1u
#define TIMECARD_UART_MAC   2u
#define TIMECARD_UART_NMEA  3u

#define TIMECARD_MRO50_FLAG_PRESENT        (1u << 0)
#define TIMECARD_MRO50_FLAG_ENABLED        (1u << 1)
#define TIMECARD_MRO50_FLAG_LOCKED         (1u << 2)
#define TIMECARD_MRO50_FLAG_FINE_VALID     (1u << 3)
#define TIMECARD_MRO50_FLAG_COARSE_VALID   (1u << 4)
#define TIMECARD_MRO50_FLAG_SERIAL_ENABLED (1u << 5)

#define TIMECARD_MRO50_ACTION_QUERY         0u
#define TIMECARD_MRO50_ACTION_ADJUST_FINE   1u
#define TIMECARD_MRO50_ACTION_ADJUST_COARSE 2u
#define TIMECARD_MRO50_ACTION_SAVE_COARSE   3u
#define TIMECARD_MRO50_ACTION_SERIAL_ENABLE 4u

#define TIMECARD_HIERARCHY_QUERY   0u
#define TIMECARD_HIERARCHY_ENABLE  1u
#define TIMECARD_HIERARCHY_DISABLE 2u

#define TIMECARD_SMA_COUNT 4u

#define TIMECARD_SMA_DIRECTION_INPUT    0u
#define TIMECARD_SMA_DIRECTION_OUTPUT   1u
#define TIMECARD_SMA_DIRECTION_DISABLED 2u

#define TIMECARD_SMA_FLAG_PRESENT         (1u << 0)
#define TIMECARD_SMA_FLAG_FIXED_DIRECTION (1u << 1)
#define TIMECARD_SMA_FLAG_DISABLED        (1u << 2)

#define TIMECARD_I2C_FLAG_PRESENT  (1u << 0)
#define TIMECARD_I2C_FLAG_ENABLED  (1u << 1)
#define TIMECARD_I2C_FLAG_BUS_BUSY (1u << 2)
#define TIMECARD_I2C_FLAG_RX_EMPTY (1u << 3)
#define TIMECARD_I2C_FLAG_TX_EMPTY (1u << 4)

#define TIMECARD_I2C_DEVICE_BOARD_EEPROM (1u << 0)
#define TIMECARD_I2C_DEVICE_MAC_EEPROM   (1u << 1)
#define TIMECARD_BOARD_EEPROM_ADDRESS    0x50u
#define TIMECARD_IDENTITY_ADDRESS        0x58u
#define TIMECARD_IDENTITY_EUI48_OFFSET   0x9au

/* U27, PCA9546A at 0x70.  More than one downstream branch may be selected. */
#define TIMECARD_I2C_MUX_ADDRESS         0x70u
#define TIMECARD_I2C_MUX_CHANNEL_MAC     (1u << 0)
#define TIMECARD_I2C_MUX_CHANNEL_SENSORS (1u << 1)
#define TIMECARD_I2C_MUX_CHANNEL_ANADC   (1u << 2)
#define TIMECARD_I2C_MUX_CHANNEL_DC      (1u << 3)
#define TIMECARD_I2C_MUX_CHANNEL_MASK    0x0fu

/*
 * U6, IS32FL3207 on the sensor branch.  The schematic's 0x6e label is
 * the 8-bit write address; Windows and the XIIC API use 7-bit address 0x37.
 */
#define TIMECARD_LED_ADDRESS_MIN 0x34u
#define TIMECARD_LED_ADDRESS_MAX 0x37u
#define TIMECARD_LED_ADDRESS 0x37u
#define TIMECARD_LED_COUNT 6u
#define TIMECARD_LED_GNSS1 0u
#define TIMECARD_LED_GNSS2 1u
#define TIMECARD_LED_IO1   2u
#define TIMECARD_LED_IO2   3u
#define TIMECARD_LED_IO3   4u
#define TIMECARD_LED_IO4   5u
#define TIMECARD_LED_FLAG_PRESENT (1u << 0)
#define TIMECARD_LED_FLAG_ENABLED (1u << 1)
#define TIMECARD_LED_FLAG_FAULT_VALID (1u << 2)
#define TIMECARD_LED_FLAG_DC_TEST (1u << 3)
#define TIMECARD_LED_FLAG_RESET_TEST (1u << 4)
#define TIMECARD_LED_FLAG_SDB_HIGH (1u << 5)
#define TIMECARD_LED_MAX_GLOBAL_CURRENT 128u
#define TIMECARD_LED_OUTPUT_MASK ((1u << 18) - 1u)

/* Dedicated telemetry devices on PCA9546A channel 1 (SENS_I2C). */
#define TIMECARD_SENSOR_BME280_ADDRESS 0x76u
#define TIMECARD_SENSOR_BME280_ADDRESS_ALTERNATE 0x77u
#define TIMECARD_SENSOR_BNO055_ADDRESS 0x29u
#define TIMECARD_SENSOR_BNO055_ADDRESS_ALTERNATE 0x28u
#define TIMECARD_SENSOR_BNO08X_ADDRESS 0x4au
#define TIMECARD_SENSOR_BNO08X_ADDRESS_ALTERNATE 0x4bu
#define TIMECARD_SENSOR_BNO08X_CHIP_ID 0x80u
#define TIMECARD_SENSOR_INA219_12V_ADDRESS 0x40u
#define TIMECARD_SENSOR_INA219_5V_ADDRESS 0x41u
#define TIMECARD_SENSOR_INA219_3V3_ADDRESS 0x44u
#define TIMECARD_SENSOR_INA219_SHUNT_MICROOHMS 2000u
#define TIMECARD_SENSOR_FLAG_PRESENT          (1u << 0)
#define TIMECARD_SENSOR_FLAG_VALID            (1u << 1)
#define TIMECARD_SENSOR_FLAG_CONFIGURED       (1u << 2)
#define TIMECARD_SENSOR_FLAG_CONVERSION_READY (1u << 3)
#define TIMECARD_SENSOR_FLAG_OVERFLOW         (1u << 4)
#define TIMECARD_SENSOR_FLAG_EXTERNAL_CLOCK   (1u << 5)
#define TIMECARD_SENSOR_FLAG_HUMIDITY         (1u << 6)
#define TIMECARD_SENSOR_FLAG_TEMPERATURE      (1u << 7)
#define TIMECARD_SENSOR_FLAG_TEMPERATURE_Q7   (1u << 8)

#define TIMECARD_CLOCK_SOURCE_NONE 0x00u
#define TIMECARD_CLOCK_SOURCE_TOD  0x01u
#define TIMECARD_CLOCK_SOURCE_IRIG 0x02u
#define TIMECARD_CLOCK_SOURCE_PPS  0x03u
#define TIMECARD_CLOCK_SOURCE_PTP  0x04u
#define TIMECARD_CLOCK_SOURCE_RTC  0x05u
#define TIMECARD_CLOCK_SOURCE_DCF  0x06u
#define TIMECARD_CLOCK_SOURCE_REGS 0xfeu
#define TIMECARD_CLOCK_SOURCE_EXT  0xffu

#define TIMECARD_NMEA_FLAG_PRESENT (1u << 0)
#define TIMECARD_NMEA_FLAG_ENABLED (1u << 1)

#define TIMECARD_IDENTITY_FLAG_PRESENT (1u << 0)
#define TIMECARD_IDENTITY_FLAG_VALID   (1u << 1)
#define TIMECARD_IDENTITY_SERIAL_LENGTH 6u

#define TIMECARD_SIGNAL_COUNT 4u
#define TIMECARD_SIGNAL_FLAG_PRESENT  (1u << 0)
#define TIMECARD_SIGNAL_FLAG_ENABLED  (1u << 1)
#define TIMECARD_SIGNAL_FLAG_INVERTED (1u << 2)

#define TIMECARD_FREQUENCY_COUNT 4u
#define TIMECARD_FREQUENCY_FLAG_PRESENT (1u << 0)
#define TIMECARD_FREQUENCY_FLAG_ENABLED (1u << 1)
#define TIMECARD_FREQUENCY_FLAG_VALID   (1u << 2)
#define TIMECARD_FREQUENCY_FLAG_ERROR   (1u << 3)
#define TIMECARD_FREQUENCY_FLAG_OVERRUN (1u << 4)

#define TIMECARD_FLASH_MAX_TRANSFER 256u
#define TIMECARD_FLASH_FIRMWARE_OFFSET 0x00400000u
#define TIMECARD_FLASH_ERASE_SIZE 4096u
#define TIMECARD_FLASH_PAGE_SIZE 256u
#define TIMECARD_FLASH_FLAG_PRESENT    (1u << 0)
#define TIMECARD_FLASH_FLAG_IDENTIFIED (1u << 1)
#define TIMECARD_FLASH_FLAG_SUPPORTED  (1u << 2)
#define TIMECARD_FLASH_FLAG_FOUR_BYTE  (1u << 3)

#define TIMECARD_UART_OBSERVE_FLAG_PRESENT  (1u << 0)
#define TIMECARD_UART_OBSERVE_FLAG_ACTIVITY (1u << 1)

/* Function selectors match the FPGA SMA maps and the Linux ptp_ocp ABI. */
#define TIMECARD_SMA_INPUT_10MHZ 0x0000u
#define TIMECARD_SMA_INPUT_PPS1  0x0001u
#define TIMECARD_SMA_INPUT_PPS2  0x0002u
#define TIMECARD_SMA_INPUT_TS1   0x0004u
#define TIMECARD_SMA_INPUT_TS2   0x0008u
#define TIMECARD_SMA_INPUT_IRIG  0x0010u
#define TIMECARD_SMA_INPUT_DCF   0x0020u
#define TIMECARD_SMA_INPUT_TS3   0x0040u
#define TIMECARD_SMA_INPUT_TS4   0x0080u
#define TIMECARD_SMA_INPUT_FREQ1 0x0100u
#define TIMECARD_SMA_INPUT_FREQ2 0x0200u
#define TIMECARD_SMA_INPUT_FREQ3 0x0400u
#define TIMECARD_SMA_INPUT_FREQ4 0x0800u

#define TIMECARD_SMA_OUTPUT_10MHZ 0x0000u
#define TIMECARD_SMA_OUTPUT_PHC   0x0001u
#define TIMECARD_SMA_OUTPUT_MAC   0x0002u
#define TIMECARD_SMA_OUTPUT_GNSS1 0x0004u
#define TIMECARD_SMA_OUTPUT_GNSS2 0x0008u
#define TIMECARD_SMA_OUTPUT_IRIG  0x0010u
#define TIMECARD_SMA_OUTPUT_DCF   0x0020u
#define TIMECARD_SMA_OUTPUT_GEN1  0x0040u
#define TIMECARD_SMA_OUTPUT_GEN2  0x0080u
#define TIMECARD_SMA_OUTPUT_GEN3  0x0100u
#define TIMECARD_SMA_OUTPUT_GEN4  0x0200u
#define TIMECARD_SMA_OUTPUT_GND   0x2000u
#define TIMECARD_SMA_OUTPUT_VCC   0x4000u

typedef struct _TIMECARD_TIME {
    unsigned __int64 Seconds;
    unsigned __int32 Nanoseconds;
    unsigned __int32 Reserved;
} TIMECARD_TIME;

/* System time values use Windows FILETIME units: 100 ns since 1601 UTC. */
typedef struct _TIMECARD_CROSSTIMESTAMP {
    TIMECARD_TIME CardTime;
    unsigned __int64 SystemTimeBefore100ns;
    unsigned __int64 SystemTimeAfter100ns;
} TIMECARD_CROSSTIMESTAMP;

typedef struct _TIMECARD_INFO {
    unsigned __int32 AbiVersion;
    unsigned __int32 DriverVersion;
    unsigned __int32 Layout;
    unsigned __int32 InterruptMessages;
    unsigned __int32 BarLength;
    unsigned __int32 ClockOffset;
    unsigned __int32 ClockVersion;
    unsigned __int32 ClockStatus;
    unsigned __int32 ClockSelect;
    unsigned __int32 TodVersion;
    unsigned __int32 TodStatus;
    unsigned __int32 UtcStatus;
    unsigned __int32 Leap;
    unsigned __int32 GnssStatus;
    unsigned __int32 Satellites;
} TIMECARD_INFO;

typedef struct _TIMECARD_UART_CONFIG {
    unsigned __int32 Port;
    unsigned __int32 Baud;
} TIMECARD_UART_CONFIG;

typedef struct _TIMECARD_UART_READ_REQUEST {
    unsigned __int32 Port;
    unsigned __int32 MaximumBytes;
    unsigned __int32 TimeoutMilliseconds;
    unsigned __int32 Reserved;
} TIMECARD_UART_READ_REQUEST;

typedef struct _TIMECARD_UART_TRANSFER {
    unsigned __int32 Port;
    unsigned __int32 Length;
    unsigned __int32 TimeoutMilliseconds;
    unsigned __int32 LineStatus;
    unsigned char Data[TIMECARD_UART_MAX_TRANSFER];
} TIMECARD_UART_TRANSFER;

typedef struct _TIMECARD_UART_RESULT {
    unsigned __int32 BytesTransferred;
    unsigned __int32 LineStatus;
} TIMECARD_UART_RESULT;

typedef struct _TIMECARD_HIERARCHY_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Action;
    unsigned __int32 Persist;
    unsigned __int32 RuntimeEnabled;
    unsigned __int32 Persisted;
    unsigned __int32 Reserved[3];
} TIMECARD_HIERARCHY_CONTROL;

typedef struct _TIMECARD_SMA_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Connector;
    unsigned __int32 Direction;
    unsigned __int32 Function;
    unsigned __int32 Flags;
    unsigned __int32 InputMap;
    unsigned __int32 OutputMap;
    unsigned __int32 Reserved;
} TIMECARD_SMA_CONTROL;

typedef struct _TIMECARD_I2C_STATUS {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Offset;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 InterruptStatus;
    unsigned __int32 InterruptEnable;
    unsigned __int32 TxFifoOccupancy;
    unsigned __int32 RxFifoOccupancy;
    unsigned __int32 KnownDeviceMask;
    unsigned __int32 Reserved[2];
} TIMECARD_I2C_STATUS;

typedef struct _TIMECARD_I2C_PROBE {
    unsigned __int32 Size;
    unsigned __int32 Address;
    unsigned __int32 Present;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    unsigned __int32 Reserved[3];
} TIMECARD_I2C_PROBE;

/* Subaddress is transmitted most-significant byte first. */
typedef struct _TIMECARD_I2C_READ_REQUEST {
    unsigned __int32 Size;
    unsigned __int32 Address;
    unsigned __int32 SubaddressLength;
    unsigned __int32 Subaddress;
    unsigned __int32 Length;
    unsigned __int32 TimeoutMilliseconds;
    unsigned __int32 Reserved[2];
} TIMECARD_I2C_READ_REQUEST;

typedef struct _TIMECARD_I2C_TRANSFER {
    unsigned __int32 Size;
    unsigned __int32 Address;
    unsigned __int32 Length;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    unsigned char Data[TIMECARD_I2C_MAX_TRANSFER];
} TIMECARD_I2C_TRANSFER;

typedef struct _TIMECARD_I2C_MUX_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 ChannelMask;
    unsigned __int32 Present;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    unsigned __int32 Reserved[3];
} TIMECARD_I2C_MUX_CONTROL;

typedef struct _TIMECARD_LED_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Led;
    unsigned __int32 Flags;
    unsigned __int32 Red;
    unsigned __int32 Green;
    unsigned __int32 Blue;
    unsigned __int32 GlobalCurrent;
    unsigned __int32 MuxChannelMask;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    /* 18-bit OUT1..OUT18 diagnostic masks from registers 0x72..0x74. */
    unsigned __int32 OpenOutputMask;
    unsigned __int32 ShortOutputMask;
} TIMECARD_LED_CONTROL;

/* BME280 raw sample and factory calibration; all ABI fields are 32-bit. */
typedef struct _TIMECARD_BME280_READING {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 ChipId;
    unsigned __int32 Status;
    signed __int32 RawTemperature;
    unsigned __int32 RawPressure;
    unsigned __int32 RawHumidity;
    unsigned __int32 DigT1;
    signed __int32 DigT2;
    signed __int32 DigT3;
    unsigned __int32 DigP1;
    signed __int32 DigP2;
    signed __int32 DigP3;
    signed __int32 DigP4;
    signed __int32 DigP5;
    signed __int32 DigP6;
    signed __int32 DigP7;
    signed __int32 DigP8;
    signed __int32 DigP9;
    unsigned __int32 DigH1;
    signed __int32 DigH2;
    unsigned __int32 DigH3;
    signed __int32 DigH4;
    signed __int32 DigH5;
    signed __int32 DigH6;
} TIMECARD_BME280_READING;

typedef struct _TIMECARD_INA219_READING {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Address;
    unsigned __int32 BusMillivolts;
    signed __int32 ShuntMicrovolts;
    signed __int32 CurrentMilliamps;
    signed __int32 PowerMilliwatts;
    unsigned __int32 Configuration;
    unsigned __int32 RawBus;
} TIMECARD_INA219_READING;

/* BNO055 raw axes use the scale selected by UnitSelection. */
typedef struct _TIMECARD_BNO055_READING {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 ChipId;
    unsigned __int32 OperationMode;
    unsigned __int32 PowerMode;
    unsigned __int32 UnitSelection;
    unsigned __int32 SystemStatus;
    unsigned __int32 SystemError;
    unsigned __int32 Calibration;
    unsigned __int32 SelfTest;
    unsigned __int32 SystemClockStatus;
    signed __int32 Temperature;
    signed __int32 AccelerationX;
    signed __int32 AccelerationY;
    signed __int32 AccelerationZ;
    signed __int32 MagneticX;
    signed __int32 MagneticY;
    signed __int32 MagneticZ;
    signed __int32 GyroscopeX;
    signed __int32 GyroscopeY;
    signed __int32 GyroscopeZ;
    signed __int32 Heading;
    signed __int32 Roll;
    signed __int32 Pitch;
    signed __int32 QuaternionW;
    signed __int32 QuaternionX;
    signed __int32 QuaternionY;
    signed __int32 QuaternionZ;
    signed __int32 LinearAccelerationX;
    signed __int32 LinearAccelerationY;
    signed __int32 LinearAccelerationZ;
    signed __int32 GravityX;
    signed __int32 GravityY;
    signed __int32 GravityZ;
} TIMECARD_BNO055_READING;

typedef struct _TIMECARD_SENSOR_TELEMETRY {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 MuxChannelMask;
    unsigned __int32 ControllerStatus;
    unsigned __int32 InterruptStatus;
    TIMECARD_BME280_READING Environment;
    TIMECARD_INA219_READING Rail12V;
    TIMECARD_INA219_READING Rail5V;
    TIMECARD_INA219_READING Rail3V3;
    TIMECARD_BNO055_READING Imu;
} TIMECARD_SENSOR_TELEMETRY;

/*
 * Orolia/Safran ART mRO-50 FPGA bridge. Temperature is the raw gateware
 * telemetry word; its physical scaling depends on the installed ART image.
 */
typedef struct _TIMECARD_MRO50_STATUS {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Control;
    unsigned __int32 FineAdjustment;
    unsigned __int32 CoarseAdjustment;
    unsigned __int32 Temperature;
    unsigned __int32 BoardConfig;
    unsigned __int32 Reserved;
} TIMECARD_MRO50_STATUS;

typedef struct _TIMECARD_MRO50_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Action;
    unsigned __int32 Value;
    unsigned __int32 Reserved;
} TIMECARD_MRO50_CONTROL;

typedef struct _TIMECARD_CLOCK_SOURCE_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Source;
    unsigned __int32 ActiveSource;
    unsigned __int32 Reserved[5];
} TIMECARD_CLOCK_SOURCE_CONTROL;

typedef struct _TIMECARD_NMEA_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Baud;
    unsigned __int32 BaudSelector;
    unsigned __int32 Polarity;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 Version;
    unsigned __int32 Reserved[4];
} TIMECARD_NMEA_CONTROL;

typedef struct _TIMECARD_IDENTITY {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned char Serial[TIMECARD_IDENTITY_SERIAL_LENGTH];
    unsigned char Reserved[2];
} TIMECARD_IDENTITY;

/* Generator numbers are 1 through 4. Times are expressed in nanoseconds. */
typedef struct _TIMECARD_SIGNAL_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Generator;
    unsigned __int32 Flags;
    unsigned __int32 Status;
    unsigned __int32 Version;
    unsigned __int32 RepeatCount;
    unsigned __int32 StartNanoseconds;
    unsigned __int32 Reserved;
    unsigned __int64 PeriodNanoseconds;
    unsigned __int64 PulseNanoseconds;
    unsigned __int64 PhaseNanoseconds;
    unsigned __int64 StartSeconds;
} TIMECARD_SIGNAL_CONTROL;

/* Counter numbers are 1 through 4; zero integration seconds disables it. */
typedef struct _TIMECARD_FREQUENCY_CONTROL {
    unsigned __int32 Size;
    unsigned __int32 Counter;
    unsigned __int32 Flags;
    unsigned __int32 IntegrationSeconds;
    unsigned __int32 FrequencyHz;
    unsigned __int32 Control;
    unsigned __int32 Status;
    unsigned __int32 Reserved;
} TIMECARD_FREQUENCY_CONTROL;

typedef struct _TIMECARD_FLASH_STATUS {
    unsigned __int32 Size;
    unsigned __int32 Flags;
    unsigned __int32 Offset;
    unsigned __int32 JedecId;
    unsigned __int32 CapacityBytes;
    unsigned __int32 FirmwareOffset;
    unsigned __int32 EraseSize;
    unsigned __int32 PageSize;
    unsigned __int32 ControllerStatus;
    unsigned __int32 FlashStatus;
    unsigned __int32 FifoDepth;
    unsigned __int32 Reserved[5];
} TIMECARD_FLASH_STATUS;

/* Flash offsets are relative to TIMECARD_FLASH_FIRMWARE_OFFSET. */
typedef struct _TIMECARD_FLASH_RANGE {
    unsigned __int32 Size;
    unsigned __int32 Offset;
    unsigned __int32 Length;
    unsigned __int32 Reserved;
} TIMECARD_FLASH_RANGE;

typedef struct _TIMECARD_FLASH_TRANSFER {
    unsigned __int32 Size;
    unsigned __int32 Offset;
    unsigned __int32 Length;
    unsigned __int32 Status;
    unsigned char Data[TIMECARD_FLASH_MAX_TRANSFER];
} TIMECARD_FLASH_TRANSFER;

typedef struct _TIMECARD_FLASH_RESULT {
    unsigned __int32 Size;
    unsigned __int32 Offset;
    unsigned __int32 Length;
    unsigned __int32 Status;
} TIMECARD_FLASH_RESULT;

typedef struct _TIMECARD_UART_OBSERVE {
    unsigned __int32 Size;
    unsigned __int32 Port;
    unsigned __int32 TimeoutMilliseconds;
    unsigned __int32 Flags;
    unsigned __int32 LineStatus;
    unsigned __int32 Reserved[3];
} TIMECARD_UART_OBSERVE;

#endif /* TIMECARD_IOCTL_H */
