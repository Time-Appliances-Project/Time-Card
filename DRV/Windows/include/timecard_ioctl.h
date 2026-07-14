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

#define TIMECARD_ABI_VERSION 4u
#define TIMECARD_LAYOUT_MSI  1u
#define TIMECARD_LAYOUT_MSIX 2u
#define TIMECARD_UART_COUNT  4u
#define TIMECARD_UART_MAX_TRANSFER 256u
#define TIMECARD_I2C_MAX_TRANSFER 255u

#define TIMECARD_UART_GNSS  0u
#define TIMECARD_UART_GNSS2 1u
#define TIMECARD_UART_MAC   2u
#define TIMECARD_UART_NMEA  3u

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

#endif /* TIMECARD_IOCTL_H */
