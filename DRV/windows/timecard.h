/* SPDX-License-Identifier: BSD-3-Clause */
/* Internal declarations for the OCP TimeCard Windows KMDF driver. */

#pragma once

#include <ntddk.h>
#include <wdf.h>

#include "include/timecard_ioctl.h"

/* Private setup class used by the controller and its raw child PDOs. */
extern const GUID GUID_DEVCLASS_TIMECARD;

#define TIMECARD_CLOCK_OFFSET_MSI       0x01000000u
#define TIMECARD_TOD_OFFSET_MSI         0x01050000u
#define TIMECARD_UART_GNSS_OFFSET_MSI   0x00161000u
#define TIMECARD_UART_GNSS2_OFFSET_MSI  0x00171000u
#define TIMECARD_UART_MAC_OFFSET_MSI    0x00181000u
#define TIMECARD_UART_NMEA_OFFSET_MSI   0x00191000u

#define TIMECARD_CLOCK_OFFSET_MSIX      0x03000000u
#define TIMECARD_TOD_OFFSET_MSIX        0x03050000u
#define TIMECARD_UART_GNSS_OFFSET_MSIX  0x02161000u
#define TIMECARD_UART_GNSS2_OFFSET_MSIX 0x02171000u
#define TIMECARD_UART_MAC_OFFSET_MSIX   0x02181000u
#define TIMECARD_UART_NMEA_OFFSET_MSIX  0x02191000u

#define TIMECARD_REGISTER_WINDOW_SIZE   0x00010000u
#define TIMECARD_UART_CLOCK_HZ          50000000u
#define TIMECARD_SUBSYSTEM_COUNT        11u

typedef struct _OCP_REG {
    ULONG Ctrl;
    ULONG Status;
    ULONG Select;
    ULONG Version;
    ULONG TimeNs;
    ULONG TimeSec;
    ULONG Reserved0[2];
    ULONG AdjustNs;
    ULONG AdjustSec;
    ULONG Reserved1[2];
    ULONG OffsetNs;
    ULONG OffsetWindowNs;
    ULONG Reserved2[2];
    ULONG DriftNs;
    ULONG DriftWindowNs;
    ULONG Reserved3[6];
    ULONG ServoOffsetP;
    ULONG ServoOffsetI;
    ULONG ServoDriftP;
    ULONG ServoDriftI;
    ULONG StatusOffset;
    ULONG StatusDrift;
} OCP_REG, *POCP_REG;

#define OCP_CTRL_ENABLE          (1u << 0)
#define OCP_CTRL_ADJUST_TIME     (1u << 1)
#define OCP_CTRL_READ_TIME_REQ   (1u << 30)
#define OCP_CTRL_READ_TIME_DONE  (1u << 31)
#define OCP_SELECT_CLOCK_REG     0xfeu

typedef struct _TOD_REG {
    ULONG Ctrl;
    ULONG Status;
    ULONG UartPolarity;
    ULONG Version;
    ULONG AdjSec;
    ULONG Reserved0[3];
    ULONG UartBaud;
    ULONG Reserved1[3];
    ULONG UtcStatus;
    ULONG Leap;
    ULONG Reserved2[2];
    ULONG GnssStatus;
    ULONG NumSat;
} TOD_REG, *PTOD_REG;

typedef struct _DEVICE_CONTEXT {
    WDFDEVICE Device;
    volatile UCHAR *Bar0Base;
    ULONG Bar0Length;
    volatile OCP_REG *Regs;
    volatile TOD_REG *Tod;
    volatile UCHAR *Uart[TIMECARD_UART_COUNT];
    ULONG ClockOffset;
    ULONG TodOffset;
    ULONG InterruptMessages;
    ULONG Layout;
    BOOLEAN HardwareReady;
    BOOLEAN HierarchyCreated;
    WDFDEVICE ChildDevices[TIMECARD_SUBSYSTEM_COUNT];
    WDFWAITLOCK RegisterLock;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

typedef enum _TIMECARD_SUBSYSTEM {
    TimeCardSubsystemPhc = 0,
    TimeCardSubsystemTod,
    TimeCardSubsystemGnss,
    TimeCardSubsystemGnss2,
    TimeCardSubsystemAtomicClock,
    TimeCardSubsystemNmea,
    TimeCardSubsystemSma,
    TimeCardSubsystemTimingIo,
    TimeCardSubsystemI2c,
    TimeCardSubsystemFlash,
    TimeCardSubsystemPtm,
    TimeCardSubsystemCount
} TIMECARD_SUBSYSTEM;

typedef struct _TIMECARD_CHILD_CONTEXT {
    TIMECARD_SUBSYSTEM Subsystem;
} TIMECARD_CHILD_CONTEXT, *PTIMECARD_CHILD_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TIMECARD_CHILD_CONTEXT,
                                   TimeCardChildGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD TimeCardEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE TimeCardEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE TimeCardEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY TimeCardEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT TimeCardEvtD0Exit;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL TimeCardEvtIoDeviceControl;

NTSTATUS TimeCardCreateSubsystemDevices(PDEVICE_CONTEXT context);
NTSTATUS TimeCardGetHierarchySetting(PDEVICE_CONTEXT context,
                                     PBOOLEAN enabled);
NTSTATUS TimeCardControlHierarchy(
    PDEVICE_CONTEXT context,
    const TIMECARD_HIERARCHY_CONTROL *request,
    TIMECARD_HIERARCHY_CONTROL *response);

NTSTATUS TimeCardGetTime(PDEVICE_CONTEXT context, TIMECARD_TIME *time);
NTSTATUS TimeCardGetCrossTimestamp(PDEVICE_CONTEXT context,
                                   TIMECARD_CROSSTIMESTAMP *timestamp);
NTSTATUS TimeCardSetTime(PDEVICE_CONTEXT context,
                         const TIMECARD_TIME *time);
NTSTATUS TimeCardGetInfo(PDEVICE_CONTEXT context, TIMECARD_INFO *info);

NTSTATUS TimeCardUartConfigure(PDEVICE_CONTEXT context,
                               const TIMECARD_UART_CONFIG *config);
NTSTATUS TimeCardUartRead(PDEVICE_CONTEXT context,
                          const TIMECARD_UART_READ_REQUEST *request,
                          TIMECARD_UART_TRANSFER *transfer);
NTSTATUS TimeCardUartWrite(PDEVICE_CONTEXT context,
                           const TIMECARD_UART_TRANSFER *transfer,
                           ULONG inputLength,
                           TIMECARD_UART_RESULT *result);
