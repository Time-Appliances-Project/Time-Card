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
#define TIMECARD_NMEA_OUT_OFFSET_MSI    0x010b0000u
#define TIMECARD_UART_GNSS_OFFSET_MSI   0x00161000u
#define TIMECARD_UART_GNSS2_OFFSET_MSI  0x00171000u
#define TIMECARD_UART_MAC_OFFSET_MSI    0x00181000u
#define TIMECARD_UART_NMEA_OFFSET_MSI   0x00191000u

#define TIMECARD_CLOCK_OFFSET_MSIX      0x03000000u
#define TIMECARD_TOD_OFFSET_MSIX        0x03050000u
#define TIMECARD_NMEA_OUT_OFFSET_MSIX   0x030b0000u
#define TIMECARD_UART_GNSS_OFFSET_MSIX  0x02161000u
#define TIMECARD_UART_GNSS2_OFFSET_MSIX 0x02171000u
#define TIMECARD_UART_MAC_OFFSET_MSIX   0x02181000u
#define TIMECARD_UART_NMEA_OFFSET_MSIX  0x02191000u

#define TIMECARD_SMA_MAP1_OFFSET_MSI    0x00140000u
#define TIMECARD_SMA_MAP2_OFFSET_MSI    0x00220000u
#define TIMECARD_SMA_MAP1_OFFSET_MSIX   0x02140000u
#define TIMECARD_SMA_MAP2_OFFSET_MSIX   0x02220000u

#define TIMECARD_I2C_OFFSET_MSI         0x00150000u
#define TIMECARD_I2C_OFFSET_MSIX        0x02150000u
#define TIMECARD_FLASH_OFFSET_MSI       0x00310000u
#define TIMECARD_FLASH_OFFSET_MSIX      0x02310000u

#define TIMECARD_SIGNAL_BASE_MSI        0x010d0000u
#define TIMECARD_SIGNAL_BASE_MSIX       0x030d0000u
#define TIMECARD_FREQUENCY_BASE_MSI     0x01200000u
#define TIMECARD_FREQUENCY_BASE_MSIX    0x03200000u

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

typedef struct _TIMECARD_GPIO_REG {
    ULONG Gpio1;
    ULONG Reserved0;
    ULONG Gpio2;
    ULONG Reserved1;
} TIMECARD_GPIO_REG, *PTIMECARD_GPIO_REG;

typedef struct _TIMECARD_SIGNAL_REG {
    ULONG Enable;
    ULONG Status;
    ULONG Polarity;
    ULONG Version;
    ULONG Reserved0[4];
    ULONG CableDelay;
    ULONG Reserved1[3];
    ULONG Interrupt;
    ULONG InterruptMask;
    ULONG Reserved2[2];
    ULONG StartNanoseconds;
    ULONG StartSeconds;
    ULONG PulseNanoseconds;
    ULONG PulseSeconds;
    ULONG PeriodNanoseconds;
    ULONG PeriodSeconds;
    ULONG RepeatCount;
} TIMECARD_SIGNAL_REG, *PTIMECARD_SIGNAL_REG;

typedef struct _TIMECARD_FREQUENCY_REG {
    ULONG Control;
    ULONG Status;
} TIMECARD_FREQUENCY_REG, *PTIMECARD_FREQUENCY_REG;

typedef struct _DEVICE_CONTEXT {
    WDFDEVICE Device;
    volatile UCHAR *Bar0Base;
    ULONG Bar0Length;
    volatile OCP_REG *Regs;
    volatile TOD_REG *Tod;
    volatile TOD_REG *NmeaOut;
    volatile TIMECARD_GPIO_REG *SmaMap1;
    volatile TIMECARD_GPIO_REG *SmaMap2;
    volatile TIMECARD_SIGNAL_REG *Signal[TIMECARD_SIGNAL_COUNT];
    volatile TIMECARD_FREQUENCY_REG *Frequency[TIMECARD_FREQUENCY_COUNT];
    volatile UCHAR *I2c;
    volatile UCHAR *Flash;
    volatile UCHAR *Uart[TIMECARD_UART_COUNT];
    ULONG ClockOffset;
    ULONG TodOffset;
    ULONG NmeaOutOffset;
    ULONG I2cOffset;
    ULONG I2cKnownDeviceMask;
    ULONG FlashOffset;
    ULONG FlashJedecId;
    ULONG FlashCapacity;
    ULONG FlashFifoDepth;
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
NTSTATUS TimeCardSetClockSource(
    PDEVICE_CONTEXT context,
    const TIMECARD_CLOCK_SOURCE_CONTROL *request,
    TIMECARD_CLOCK_SOURCE_CONTROL *response);

NTSTATUS TimeCardUartConfigure(PDEVICE_CONTEXT context,
                               const TIMECARD_UART_CONFIG *config);
NTSTATUS TimeCardUartRead(PDEVICE_CONTEXT context,
                          const TIMECARD_UART_READ_REQUEST *request,
                          TIMECARD_UART_TRANSFER *transfer);
NTSTATUS TimeCardUartWrite(PDEVICE_CONTEXT context,
                           const TIMECARD_UART_TRANSFER *transfer,
                           ULONG inputLength,
                           TIMECARD_UART_RESULT *result);
NTSTATUS TimeCardUartObserve(PDEVICE_CONTEXT context,
                             const TIMECARD_UART_OBSERVE *request,
                             TIMECARD_UART_OBSERVE *response);

NTSTATUS TimeCardSmaQuery(PDEVICE_CONTEXT context,
                          ULONG connector,
                          TIMECARD_SMA_CONTROL *control);
NTSTATUS TimeCardSmaSet(PDEVICE_CONTEXT context,
                        const TIMECARD_SMA_CONTROL *request,
                        TIMECARD_SMA_CONTROL *response);

NTSTATUS TimeCardI2cGetStatus(PDEVICE_CONTEXT context,
                              TIMECARD_I2C_STATUS *status);
NTSTATUS TimeCardI2cProbe(PDEVICE_CONTEXT context,
                          ULONG address,
                          TIMECARD_I2C_PROBE *probe);
NTSTATUS TimeCardI2cRead(PDEVICE_CONTEXT context,
                         const TIMECARD_I2C_READ_REQUEST *request,
                         TIMECARD_I2C_TRANSFER *transfer);
NTSTATUS TimeCardI2cMuxQuery(PDEVICE_CONTEXT context,
                             TIMECARD_I2C_MUX_CONTROL *control);
NTSTATUS TimeCardI2cMuxSet(PDEVICE_CONTEXT context,
                           const TIMECARD_I2C_MUX_CONTROL *request,
                           TIMECARD_I2C_MUX_CONTROL *response);
NTSTATUS TimeCardLedQuery(PDEVICE_CONTEXT context,
                          ULONG led,
                          TIMECARD_LED_CONTROL *control);
NTSTATUS TimeCardLedSet(PDEVICE_CONTEXT context,
                        const TIMECARD_LED_CONTROL *request,
                        TIMECARD_LED_CONTROL *response);
NTSTATUS TimeCardGetIdentity(PDEVICE_CONTEXT context,
                             TIMECARD_IDENTITY *identity);

NTSTATUS TimeCardNmeaInitialize(PDEVICE_CONTEXT context);
NTSTATUS TimeCardNmeaQuery(PDEVICE_CONTEXT context,
                           TIMECARD_NMEA_CONTROL *control);
NTSTATUS TimeCardNmeaSet(PDEVICE_CONTEXT context,
                         const TIMECARD_NMEA_CONTROL *request,
                         TIMECARD_NMEA_CONTROL *response);

NTSTATUS TimeCardSignalQuery(PDEVICE_CONTEXT context, ULONG generator,
                             TIMECARD_SIGNAL_CONTROL *control);
NTSTATUS TimeCardSignalSet(PDEVICE_CONTEXT context,
                           const TIMECARD_SIGNAL_CONTROL *request,
                           TIMECARD_SIGNAL_CONTROL *response);
NTSTATUS TimeCardFrequencyQuery(PDEVICE_CONTEXT context, ULONG counter,
                                TIMECARD_FREQUENCY_CONTROL *control);
NTSTATUS TimeCardFrequencySet(PDEVICE_CONTEXT context,
                              const TIMECARD_FREQUENCY_CONTROL *request,
                              TIMECARD_FREQUENCY_CONTROL *response);

NTSTATUS TimeCardFlashQuery(PDEVICE_CONTEXT context,
                            TIMECARD_FLASH_STATUS *status);
NTSTATUS TimeCardFlashRead(PDEVICE_CONTEXT context,
                           const TIMECARD_FLASH_RANGE *request,
                           TIMECARD_FLASH_TRANSFER *transfer);
NTSTATUS TimeCardFlashErase(PDEVICE_CONTEXT context,
                            const TIMECARD_FLASH_RANGE *request,
                            TIMECARD_FLASH_RESULT *result);
NTSTATUS TimeCardFlashProgram(PDEVICE_CONTEXT context,
                              const TIMECARD_FLASH_TRANSFER *request,
                              TIMECARD_FLASH_RESULT *result);
