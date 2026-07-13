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

#define TIMECARD_ABI_VERSION 1u
#define TIMECARD_LAYOUT_MSI  1u
#define TIMECARD_LAYOUT_MSIX 2u
#define TIMECARD_UART_COUNT  4u
#define TIMECARD_UART_MAX_TRANSFER 256u

#define TIMECARD_UART_GNSS  0u
#define TIMECARD_UART_GNSS2 1u
#define TIMECARD_UART_MAC   2u
#define TIMECARD_UART_NMEA  3u

#define TIMECARD_HIERARCHY_QUERY   0u
#define TIMECARD_HIERARCHY_ENABLE  1u
#define TIMECARD_HIERARCHY_DISABLE 2u

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

#endif /* TIMECARD_IOCTL_H */
