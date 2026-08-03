/* SPDX-License-Identifier: BSD-3-Clause */
/* Internal declarations for the OCP TimeCard Windows KMDF driver. */

#pragma once

#include <ntddk.h>
#include <wdf.h>

#include "include/timecard_ioctl.h"

/* Private setup class used by the controller and its raw child PDOs. */
extern const GUID GUID_DEVCLASS_TIMECARD;

/*
 * Stable controller interface used by native Windows applications.  Device
 * interfaces, unlike the legacy \\.\TimeCard0 link, remain unique when more
 * than one manufacturer's Time Card is installed.
 * {8315A67A-3D76-4F6C-B557-B5A65D55545C}
 */
extern const GUID GUID_DEVINTERFACE_TIMECARD;

#define TIMECARD_CLOCK_OFFSET_MSI       0x01000000u
#define TIMECARD_IMAGE_OFFSET_MSI       0x00020000u
#define TIMECARD_PPS_MASTER_OFFSET_MSI  0x01030000u
#define TIMECARD_PPS_SLAVE_OFFSET_MSI   0x01040000u
#define TIMECARD_TOD_OFFSET_MSI         0x01050000u
#define TIMECARD_IRIG_SLAVE_OFFSET_MSI  0x01070000u
#define TIMECARD_IRIG_MASTER_OFFSET_MSI 0x01080000u
#define TIMECARD_DCF_SLAVE_OFFSET_MSI   0x01090000u
#define TIMECARD_DCF_MASTER_OFFSET_MSI  0x010a0000u
#define TIMECARD_NMEA_OUT_OFFSET_MSI    0x010b0000u
#define TIMECARD_TIMESTAMP0_OFFSET_MSI  0x01010000u
#define TIMECARD_TIMESTAMP1_OFFSET_MSI  0x01020000u
#define TIMECARD_TIMESTAMP2_OFFSET_MSI  0x01060000u
#define TIMECARD_TIMESTAMP3_OFFSET_MSI  0x01110000u
#define TIMECARD_TIMESTAMP4_OFFSET_MSI  0x01120000u
#define TIMECARD_TIMESTAMP5_OFFSET_MSI  0x010c0000u
#define TIMECARD_UART_GNSS_OFFSET_MSI   0x00161000u
#define TIMECARD_UART_GNSS2_OFFSET_MSI  0x00171000u
#define TIMECARD_UART_MAC_OFFSET_MSI    0x00181000u
#define TIMECARD_UART_NMEA_OFFSET_MSI   0x00191000u

#define TIMECARD_CLOCK_OFFSET_MSIX      0x03000000u
#define TIMECARD_IMAGE_OFFSET_MSIX      0x02020000u
#define TIMECARD_PPS_MASTER_OFFSET_MSIX 0x03030000u
#define TIMECARD_PPS_SLAVE_OFFSET_MSIX  0x03040000u
#define TIMECARD_TOD_OFFSET_MSIX        0x03050000u
#define TIMECARD_IRIG_SLAVE_OFFSET_MSIX 0x03070000u
#define TIMECARD_IRIG_MASTER_OFFSET_MSIX 0x03080000u
#define TIMECARD_DCF_SLAVE_OFFSET_MSIX  0x03090000u
#define TIMECARD_DCF_MASTER_OFFSET_MSIX 0x030a0000u
#define TIMECARD_NMEA_OUT_OFFSET_MSIX   0x030b0000u
#define TIMECARD_TIMESTAMP0_OFFSET_MSIX 0x03010000u
#define TIMECARD_TIMESTAMP1_OFFSET_MSIX 0x03020000u
#define TIMECARD_TIMESTAMP2_OFFSET_MSIX 0x03060000u
#define TIMECARD_TIMESTAMP3_OFFSET_MSIX 0x03110000u
#define TIMECARD_TIMESTAMP4_OFFSET_MSIX 0x03120000u
#define TIMECARD_TIMESTAMP5_OFFSET_MSIX 0x030c0000u
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

#define TIMECARD_CLOCK_OFFSET_ART       0x01000000u
#define TIMECARD_UART_GNSS_OFFSET_ART   0x00161000u
#define TIMECARD_UART_MAC_OFFSET_ART    0x00190000u
#define TIMECARD_SMA_OFFSET_ART         0x003c0000u
#define TIMECARD_I2C_OFFSET_ART         0x00350000u
#define TIMECARD_FLASH_OFFSET_ART       0x00310000u
#define TIMECARD_BOARD_CONFIG_OFFSET_ART 0x00210000u
#define TIMECARD_MRO50_OFFSET_ART       0x00340000u
#define TIMECARD_PHASE_REFERENCE_OFFSET_ART  0x00360000u
#define TIMECARD_PHASE_OSCILLATOR_OFFSET_ART 0x00330000u
#define TIMECARD_TIMESTAMP0_OFFSET_ART       0x00360000u
#define TIMECARD_TIMESTAMP1_OFFSET_ART       0x00380000u
#define TIMECARD_TIMESTAMP2_OFFSET_ART       0x00390000u
#define TIMECARD_TIMESTAMP3_OFFSET_ART       0x003a0000u
#define TIMECARD_TIMESTAMP4_OFFSET_ART       0x003b0000u
#define TIMECARD_TIMESTAMP5_OFFSET_ART       0x00330000u

#define TIMECARD_REGISTER_WINDOW_SIZE   0x00010000u
#define TIMECARD_UART_CLOCK_HZ          50000000u
#define TIMECARD_UART_RX_RING_SIZE      8192u
#define TIMECARD_UART_INTERRUPT_OBJECTS 16u
#define TIMECARD_SUBSYSTEM_COUNT        11u
#define TIMECARD_PCI_REVISION_UNKNOWN   0xffu
#define TIMECARD_OFFSET_NONE            MAXULONG

#define TIMECARD_BOARD_FB                1u
#define TIMECARD_BOARD_ART               2u
#define TIMECARD_BOARD_CELESTICA         3u
#define TIMECARD_I2C_CONTROLLER_XIIC     1u
#define TIMECARD_I2C_CONTROLLER_OCORES   2u
#define TIMECARD_FLASH_CONTROLLER_XILINX 1u
#define TIMECARD_FLASH_CONTROLLER_ALTERA 2u

#define TIMECARD_FLASH_FIRMWARE_OFFSET_FB  0x00400000u
#define TIMECARD_FLASH_FIRMWARE_OFFSET_ART 0x01000000u

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
    ULONG DriftFractions;
    ULONG Reserved3;
    ULONG InSyncThreshold;
    ULONG Reserved4[3];
    ULONG ServoOffsetP;
    ULONG ServoOffsetI;
    ULONG ServoDriftP;
    ULONG ServoDriftI;
    ULONG StatusOffset;
    ULONG StatusDrift;
    ULONG StatusOffsetFraction;
    ULONG StatusDriftFraction;
} OCP_REG, *POCP_REG;

#define OCP_CTRL_ENABLE          (1u << 0)
#define OCP_CTRL_ADJUST_TIME     (1u << 1)
#define OCP_CTRL_ADJUST_OFFSET   (1u << 2)
#define OCP_CTRL_ADJUST_DRIFT    (1u << 3)
#define OCP_CTRL_SERVO_VALID     (1u << 8)
#define OCP_CTRL_HOLDOVER        (1u << 16)
#define OCP_CTRL_HOLDOVER_OFFSET (1u << 17)
#define OCP_CTRL_AGING           (1u << 18)
#define OCP_CTRL_REVERT          (1u << 19)
#define OCP_CTRL_READ_TIME_REQ   (1u << 30)
#define OCP_CTRL_READ_TIME_DONE  (1u << 31)
#define OCP_CTRL_TRANSIENT_MASK  \
    (OCP_CTRL_ADJUST_TIME | OCP_CTRL_ADJUST_OFFSET | \
     OCP_CTRL_ADJUST_DRIFT | OCP_CTRL_SERVO_VALID | \
     OCP_CTRL_READ_TIME_REQ | OCP_CTRL_READ_TIME_DONE)
#define OCP_SELECT_CLOCK_REG     0xfeu

#define OCP_ADV_OFFSET_RATE_LIMITER  0x18u
#define OCP_ADV_DRIFT_RATE_LIMITER   0x1cu
#define OCP_ADV_AGING_CONFIGURATION  0x4cu
#define OCP_ADV_HOLDOVER_CONFIGURATION 0x54u
#define OCP_ADV_OFFSET_OUTLIER_FILTER 0x58u
#define OCP_ADV_DRIFT_OUTLIER_FILTER  0x5cu
#define OCP_ADV_STATUS_HOLDOVER       0x80u
#define OCP_ADV_STATUS_HOLDOVER_FRACTION 0x84u
#define OCP_ADV_STATUS_HOLDOVER_SAMPLES  0x88u
#define OCP_ADV_STATUS_OFFSET_OUTLIERS   0x90u
#define OCP_ADV_STATUS_DRIFT_OUTLIERS    0x94u
#define OCP_ADV_STATUS_AGING_LOW         0xa0u
#define OCP_ADV_STATUS_AGING_HIGH        0xa4u
#define OCP_ADV_STATUS_AGING_SAMPLES     0xa8u
#define OCP_ADV_DYNAMIC_CONTROL          0x100u

typedef struct _TIMECARD_TOD_SLAVE_REG {
    ULONG Ctrl;
    ULONG Status;
    ULONG UartPolarity;
    ULONG Version;
    ULONG Correction;
    ULONG Reserved0[3];
    ULONG UartBaud;
    ULONG Reserved1[3];
    ULONG UtcStatus;
    ULONG Leap;
    ULONG Reserved2[2];
    ULONG GnssStatus;
    ULONG NumSat;
} TIMECARD_TOD_SLAVE_REG, *PTIMECARD_TOD_SLAVE_REG;

typedef struct _TIMECARD_TOD_MASTER_REG {
    ULONG Ctrl;
    ULONG Status;
    ULONG UartPolarity;
    ULONG Version;
    ULONG Correction;
    ULONG LocalOffset;
    ULONG Reserved0[2];
    ULONG UartBaud;
    ULONG Reserved1[55];
    ULONG UtcInfoControl;
    ULONG UtcInfo;
} TIMECARD_TOD_MASTER_REG, *PTIMECARD_TOD_MASTER_REG;

typedef struct _TIMECARD_PPS_REG {
    ULONG Control;
    ULONG Status;
    ULONG Polarity;
    ULONG Version;
    ULONG PulseWidth;
    ULONG Reserved0[3];
    ULONG CableDelay;
} TIMECARD_PPS_REG, *PTIMECARD_PPS_REG;

typedef struct _TIMECARD_IRIG_MASTER_REG {
    ULONG Control;
    ULONG Status;
    ULONG Reserved0;
    ULONG Version;
    ULONG Correction;
    ULONG ControlBits;
} TIMECARD_IRIG_MASTER_REG, *PTIMECARD_IRIG_MASTER_REG;

typedef struct _TIMECARD_IRIG_SLAVE_REG {
    ULONG Control;
    ULONG Status;
    ULONG Reserved0;
    ULONG Version;
    ULONG Correction;
    ULONG ControlBits;
    ULONG Reserved1[2];
    ULONG CableDelay;
    ULONG ManualYear;
} TIMECARD_IRIG_SLAVE_REG, *PTIMECARD_IRIG_SLAVE_REG;

typedef struct _TIMECARD_DCF_MASTER_REG {
    ULONG Control;
    ULONG Status;
    ULONG Reserved0;
    ULONG Version;
    ULONG Correction;
} TIMECARD_DCF_MASTER_REG, *PTIMECARD_DCF_MASTER_REG;

typedef struct _TIMECARD_DCF_SLAVE_REG {
    ULONG Control;
    ULONG Status;
    ULONG Reserved0;
    ULONG Version;
    ULONG Correction;
    ULONG Reserved1[3];
    ULONG AirDelay;
    ULONG Reserved2[3];
    ULONG BitPosition;
} TIMECARD_DCF_SLAVE_REG, *PTIMECARD_DCF_SLAVE_REG;

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

typedef struct _TIMECARD_ART_SMA_ENTRY {
    ULONG Gpio;
    ULONG Reserved[3];
} TIMECARD_ART_SMA_ENTRY, *PTIMECARD_ART_SMA_ENTRY;

typedef struct _TIMECARD_ART_SMA_REG {
    TIMECARD_ART_SMA_ENTRY Map[TIMECARD_SMA_COUNT];
} TIMECARD_ART_SMA_REG, *PTIMECARD_ART_SMA_REG;

typedef struct _TIMECARD_MRO50_REG {
    ULONG Control;
    ULONG Value;
    ULONG Adjust;
    ULONG Temperature;
} TIMECARD_MRO50_REG, *PTIMECARD_MRO50_REG;

typedef struct _TIMECARD_TIMESTAMP_REG {
    ULONG Enable;
    ULONG Error;
    ULONG Polarity;
    ULONG Version;
    ULONG Reserved0[4];
    ULONG CableDelay;
    ULONG Reserved1[3];
    ULONG Interrupt;
    ULONG InterruptMask;
    ULONG EventCount;
    ULONG Reserved2;
    ULONG TimestampCount;
    ULONG TimeNanoseconds;
    ULONG TimeSeconds;
    ULONG DataWidth;
    ULONG Data;
} TIMECARD_TIMESTAMP_REG, *PTIMECARD_TIMESTAMP_REG;

typedef struct _DEVICE_CONTEXT {
    WDFDEVICE Device;
    volatile UCHAR *Bar0Base;
    ULONG Bar0Length;
    volatile OCP_REG *Regs;
    volatile TIMECARD_PPS_REG *PpsMaster;
    volatile TIMECARD_PPS_REG *PpsSlave;
    volatile TIMECARD_IRIG_MASTER_REG *IrigMaster;
    volatile TIMECARD_IRIG_SLAVE_REG *IrigSlave;
    volatile TIMECARD_DCF_MASTER_REG *DcfMaster;
    volatile TIMECARD_DCF_SLAVE_REG *DcfSlave;
    BOOLEAN IrigMasterRouteManaged;
    BOOLEAN IrigSlaveRouteManaged;
    BOOLEAN DcfMasterRouteManaged;
    BOOLEAN DcfSlaveRouteManaged;
    volatile TIMECARD_TOD_SLAVE_REG *Tod;
    volatile TIMECARD_TOD_MASTER_REG *NmeaOut;
    volatile TIMECARD_GPIO_REG *SmaMap1;
    volatile TIMECARD_GPIO_REG *SmaMap2;
    volatile TIMECARD_ART_SMA_REG *ArtSma;
    volatile ULONG *ArtBoardConfig;
    volatile TIMECARD_MRO50_REG *Mro50;
    volatile TIMECARD_TIMESTAMP_REG *PhaseReference;
    volatile TIMECARD_TIMESTAMP_REG *PhaseOscillator;
    volatile TIMECARD_TIMESTAMP_REG *Timestamp[TIMECARD_TIMESTAMP_COUNT];
    TIMECARD_TIMESTAMP_EVENT
        TimestampRing[TIMECARD_TIMESTAMP_COUNT]
                     [TIMECARD_TIMESTAMP_QUEUE_LENGTH];
    volatile LONG TimestampHead[TIMECARD_TIMESTAMP_COUNT];
    volatile LONG TimestampTail[TIMECARD_TIMESTAMP_COUNT];
    volatile LONG TimestampDropped[TIMECARD_TIMESTAMP_COUNT];
    ULONG TimestampInterruptMask;
    ULONG TimestampRequestedMask;
    UCHAR TimestampRequestedPolarity[TIMECARD_TIMESTAMP_COUNT];
    USHORT TimestampRequestedCableDelay[TIMECARD_TIMESTAMP_COUNT];
    volatile TIMECARD_SIGNAL_REG *Signal[TIMECARD_SIGNAL_COUNT];
    TIMECARD_SIGNAL_EVENT
        SignalEventRing[TIMECARD_SIGNAL_COUNT]
                       [TIMECARD_SIGNAL_EVENT_QUEUE_LENGTH];
    volatile LONG SignalEventHead[TIMECARD_SIGNAL_COUNT];
    volatile LONG SignalEventTail[TIMECARD_SIGNAL_COUNT];
    volatile LONG SignalEventDropped[TIMECARD_SIGNAL_COUNT];
    volatile LONG64 SignalEventSequence[TIMECARD_SIGNAL_COUNT];
    ULONG SignalInterruptMask;
    ULONG SignalRequestedInterruptMask;
    volatile TIMECARD_FREQUENCY_REG *Frequency[TIMECARD_FREQUENCY_COUNT];
    volatile UCHAR *I2c;
    volatile UCHAR *Flash;
    volatile UCHAR *Uart[TIMECARD_UART_COUNT];
    UCHAR UartRxRing[TIMECARD_UART_COUNT][TIMECARD_UART_RX_RING_SIZE];
    volatile LONG UartRxHead[TIMECARD_UART_COUNT];
    volatile LONG UartRxTail[TIMECARD_UART_COUNT];
    volatile LONG UartRxLineStatus[TIMECARD_UART_COUNT];
    volatile LONG UartRxDropped[TIMECARD_UART_COUNT];
    ULONG ClockOffset;
    ULONG TodOffset;
    ULONG NmeaOutOffset;
    ULONG I2cOffset;
    ULONG I2cKnownDeviceMask;
    ULONG I2cLastStartTrace;
    ULONG I2cLastStartEvents;
    ULONG I2cLedAddress;
    ULONG I2cEnvironmentAddress;
    ULONG I2cImuAddress;
    ULONG I2cSensorMuxMask;
    ULONG I2cEnvironmentMuxMask;
    ULONG I2cPowerMuxMask;
    ULONG I2cImuType;
    ULONG I2cBno08xSequence;
    ULONG I2cBno08xConfigured;
    SHORT I2cIcp10100Otp[4];
    ULONG I2cIcp10100OtpValid;
    ULONG FlashOffset;
    ULONG FlashJedecId;
    ULONG FlashCapacity;
    ULONG FlashFifoDepth;
    ULONG FlashFirmwareOffset;
    ULONG InterruptMessages;
    ULONG Layout;
    ULONG BoardProfile;
    ULONG FpgaContractImageVersion;
    ULONG FpgaContractCapabilities;
    ULONG SubsystemMask;
    ULONG I2cController;
    ULONG FlashController;
    USHORT PciVendorId;
    USHORT PciDeviceId;
    UCHAR PciRevision;
    BOOLEAN HardwareReady;
    BOOLEAN HierarchyCreated;
    BOOLEAN PhaseCaptureEnabled;
    WDFFILEOBJECT DisciplineOwner;
    UCHAR PhaseReferencePolarity;
    UCHAR PhaseOscillatorPolarity;
    volatile LONG PhaseReferenceSequence;
    volatile LONG PhaseOscillatorSequence;
    ULONG PhaseReferenceCounter;
    ULONG PhaseOscillatorCounter;
    TIMECARD_TIME PhaseReferenceTime;
    TIMECARD_TIME PhaseOscillatorTime;
    ULONG PhaseReferenceError;
    ULONG PhaseOscillatorError;
    WDFDEVICE ChildDevices[TIMECARD_SUBSYSTEM_COUNT];
    WDFWAITLOCK RegisterLock;
    WDFINTERRUPT UartInterrupt[TIMECARD_UART_INTERRUPT_OBJECTS];
    ULONG UartInterruptCount;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

typedef struct _TIMECARD_UART_INTERRUPT_CONTEXT {
    PDEVICE_CONTEXT DeviceContext;
    ULONG MessageBase;
} TIMECARD_UART_INTERRUPT_CONTEXT, *PTIMECARD_UART_INTERRUPT_CONTEXT;

/* Internal snapshot used to make compound UART/MMIO updates transactional. */
typedef struct _TIMECARD_UART_HARDWARE_STATE {
    UCHAR DivisorLow;
    UCHAR DivisorHigh;
    UCHAR LineControl;
    UCHAR InterruptEnable;
    UCHAR ModemControl;
} TIMECARD_UART_HARDWARE_STATE, *PTIMECARD_UART_HARDWARE_STATE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TIMECARD_UART_INTERRUPT_CONTEXT,
                                   UartInterruptGetContext)

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

#define TIMECARD_SUBSYSTEM_BIT(subsystem) (1u << (ULONG)(subsystem))
#define TIMECARD_SUBSYSTEM_MASK_ALL \
    ((1u << (ULONG)TimeCardSubsystemCount) - 1u)
#define TIMECARD_SUBSYSTEM_MASK_ART \
    (TIMECARD_SUBSYSTEM_BIT(TimeCardSubsystemPhc) | \
     TIMECARD_SUBSYSTEM_BIT(TimeCardSubsystemGnss) | \
     TIMECARD_SUBSYSTEM_BIT(TimeCardSubsystemAtomicClock) | \
     TIMECARD_SUBSYSTEM_BIT(TimeCardSubsystemSma) | \
     TIMECARD_SUBSYSTEM_BIT(TimeCardSubsystemTimingIo) | \
     TIMECARD_SUBSYSTEM_BIT(TimeCardSubsystemI2c) | \
     TIMECARD_SUBSYSTEM_BIT(TimeCardSubsystemFlash))

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
EVT_WDF_FILE_CLEANUP TimeCardEvtFileCleanup;

BOOLEAN TimeCardDisciplineAccessAllowed(PDEVICE_CONTEXT context,
                                        WDFFILEOBJECT fileObject);
NTSTATUS TimeCardDisciplineLeaseControl(
    PDEVICE_CONTEXT context, WDFFILEOBJECT fileObject,
    const TIMECARD_DISCIPLINE_LEASE *request,
    TIMECARD_DISCIPLINE_LEASE *response);

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
NTSTATUS TimeCardGetFpgaCapabilities(
    PDEVICE_CONTEXT context,
    TIMECARD_FPGA_CAPABILITIES *capabilities);
NTSTATUS TimeCardFpgaImageQuery(
    PDEVICE_CONTEXT context,
    TIMECARD_FPGA_IMAGE_INFO *imageInfo);
NTSTATUS TimeCardCoreInventoryQuery(
    PDEVICE_CONTEXT context,
    TIMECARD_CORE_INVENTORY *inventory);
NTSTATUS TimeCardFpgaContractQuery(
    PDEVICE_CONTEXT context,
    TIMECARD_FPGA_IMAGE_CONTRACT *contract);
NTSTATUS TimeCardFpgaContractSet(
    PDEVICE_CONTEXT context,
    const TIMECARD_FPGA_IMAGE_CONTRACT *request,
    TIMECARD_FPGA_IMAGE_CONTRACT *response);
BOOLEAN TimeCardFpgaContractAllows(PDEVICE_CONTEXT context,
                                   ULONG capability);
NTSTATUS TimeCardClockTelemetryQuery(
    PDEVICE_CONTEXT context,
    TIMECARD_CLOCK_TELEMETRY *telemetry);
NTSTATUS TimeCardClockAdjustmentQuery(
    PDEVICE_CONTEXT context,
    TIMECARD_CLOCK_ADJUSTMENT *adjustment);
NTSTATUS TimeCardClockAdjustmentSet(
    PDEVICE_CONTEXT context,
    const TIMECARD_CLOCK_ADJUSTMENT *request,
    TIMECARD_CLOCK_ADJUSTMENT *response);
NTSTATUS TimeCardClockAdvancedQuery(
    PDEVICE_CONTEXT context,
    TIMECARD_CLOCK_ADVANCED_CONTROL *control);
NTSTATUS TimeCardClockAdvancedSet(
    PDEVICE_CONTEXT context,
    const TIMECARD_CLOCK_ADVANCED_CONTROL *request,
    TIMECARD_CLOCK_ADVANCED_CONTROL *response);
NTSTATUS TimeCardPpsQuery(PDEVICE_CONTEXT context, ULONG core,
                          TIMECARD_PPS_CONTROL *control);
NTSTATUS TimeCardPpsSet(PDEVICE_CONTEXT context,
                        const TIMECARD_PPS_CONTROL *request,
                        TIMECARD_PPS_CONTROL *response);
NTSTATUS TimeCardTimecodeQuery(
    PDEVICE_CONTEXT context, ULONG format, ULONG role,
    TIMECARD_TIMECODE_CONTROL *control);
NTSTATUS TimeCardTimecodeSet(
    PDEVICE_CONTEXT context,
    const TIMECARD_TIMECODE_CONTROL *request,
    TIMECARD_TIMECODE_CONTROL *response);
NTSTATUS TimeCardTodQuery(PDEVICE_CONTEXT context,
                          TIMECARD_TOD_CONTROL *control);
NTSTATUS TimeCardTodSet(PDEVICE_CONTEXT context,
                        const TIMECARD_TOD_CONTROL *request,
                        TIMECARD_TOD_CONTROL *response);
NTSTATUS TimeCardTimestampQuery(
    PDEVICE_CONTEXT context, ULONG channel,
    TIMECARD_TIMESTAMP_CONTROL *control);
NTSTATUS TimeCardTimestampSet(
    PDEVICE_CONTEXT context,
    const TIMECARD_TIMESTAMP_CONTROL *request,
    TIMECARD_TIMESTAMP_CONTROL *response);
NTSTATUS TimeCardTimestampRead(
    PDEVICE_CONTEXT context,
    const TIMECARD_TIMESTAMP_BATCH *request,
    TIMECARD_TIMESTAMP_BATCH *response);
VOID TimeCardTimestampInitialize(PDEVICE_CONTEXT context);
VOID TimeCardTimestampPowerDown(PDEVICE_CONTEXT context);
VOID TimeCardTimestampPowerUp(PDEVICE_CONTEXT context);
BOOLEAN TimeCardTimestampMessageRangeRelevant(
    PDEVICE_CONTEXT context, ULONG first, ULONG count);
VOID TimeCardTimestampMarkInterruptRange(
    PDEVICE_CONTEXT context, ULONG first, ULONG count);
BOOLEAN TimeCardHandleTimestampInterrupt(
    PDEVICE_CONTEXT context, ULONG messageId);
VOID TimeCardRefreshRoutedCoresLocked(PDEVICE_CONTEXT context,
                                      ULONG previousDirection,
                                      ULONG previousFunction,
                                      ULONG currentDirection,
                                      ULONG currentFunction);

NTSTATUS TimeCardUartConfigure(PDEVICE_CONTEXT context,
                               const TIMECARD_UART_CONFIG *config);
NTSTATUS TimeCardUartSnapshotHardware(
    PDEVICE_CONTEXT context, ULONG port,
    PTIMECARD_UART_HARDWARE_STATE state);
NTSTATUS TimeCardUartRestoreHardware(
    PDEVICE_CONTEXT context, ULONG port,
    const TIMECARD_UART_HARDWARE_STATE *state);
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
EVT_WDF_INTERRUPT_ISR TimeCardEvtUartInterruptIsr;
VOID TimeCardUartDisableInterrupts(PDEVICE_CONTEXT context);

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
NTSTATUS TimeCardOcoresI2cGetStatusLocked(
    PDEVICE_CONTEXT context,
    TIMECARD_I2C_STATUS *status);
NTSTATUS TimeCardOcoresI2cProbeLocked(
    PDEVICE_CONTEXT context,
    ULONG address,
    TIMECARD_I2C_PROBE *probe);
NTSTATUS TimeCardOcoresI2cReadLocked(
    PDEVICE_CONTEXT context,
    const TIMECARD_I2C_READ_REQUEST *request,
    TIMECARD_I2C_TRANSFER *transfer);
NTSTATUS TimeCardOcoresI2cWriteLocked(
    PDEVICE_CONTEXT context,
    ULONG address,
    const UCHAR *data,
    ULONG length,
    ULONG *controllerStatus,
    ULONG *interruptStatus);
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
NTSTATUS TimeCardSensorQuery(PDEVICE_CONTEXT context,
                             TIMECARD_SENSOR_TELEMETRY *telemetry);
NTSTATUS TimeCardMro50Query(PDEVICE_CONTEXT context,
                            TIMECARD_MRO50_STATUS *status);
NTSTATUS TimeCardMro50Control(PDEVICE_CONTEXT context,
                              const TIMECARD_MRO50_CONTROL *control,
                              TIMECARD_MRO50_STATUS *status);
NTSTATUS TimeCardGetCapabilities(PDEVICE_CONTEXT context,
                                 TIMECARD_CAPABILITIES *capabilities);
NTSTATUS TimeCardPhaseQuery(PDEVICE_CONTEXT context,
                            TIMECARD_PHASE_SAMPLE *sample);
NTSTATUS TimeCardPhaseControl(PDEVICE_CONTEXT context,
                              const TIMECARD_PHASE_CONTROL *request,
                              TIMECARD_PHASE_CONTROL *response);
VOID TimeCardRecordPhaseTimestamp(PDEVICE_CONTEXT context, ULONG channel,
                                  ULONG error, ULONG nanoseconds,
                                  ULONG seconds);
/* RegisterLock must be held by the caller. */
VOID TimeCardPhaseDisableLocked(PDEVICE_CONTEXT context);
VOID TimeCardPhaseSuspend(PDEVICE_CONTEXT context);
NTSTATUS TimeCardAdjustPhc(PDEVICE_CONTEXT context,
                           const TIMECARD_PHC_ADJUST *request,
                           TIMECARD_PHC_ADJUST *response);
NTSTATUS TimeCardDisciplineRead(PDEVICE_CONTEXT context,
                                TIMECARD_DISCIPLINE_BLOB *blob);
NTSTATUS TimeCardDisciplineWrite(PDEVICE_CONTEXT context,
                                 const TIMECARD_DISCIPLINE_BLOB *request,
                                 TIMECARD_DISCIPLINE_BLOB *response);
NTSTATUS TimeCardGetIdentity(PDEVICE_CONTEXT context,
                             TIMECARD_IDENTITY *identity);

NTSTATUS TimeCardNmeaQuery(PDEVICE_CONTEXT context,
                           TIMECARD_NMEA_CONTROL *control);
NTSTATUS TimeCardNmeaSet(PDEVICE_CONTEXT context,
                          const TIMECARD_NMEA_CONTROL *request,
                          TIMECARD_NMEA_CONTROL *response);
NTSTATUS TimeCardNmeaUtcQuery(
    PDEVICE_CONTEXT context,
    TIMECARD_NMEA_UTC_CONTROL *control);
NTSTATUS TimeCardNmeaUtcSet(
    PDEVICE_CONTEXT context,
    const TIMECARD_NMEA_UTC_CONTROL *request,
    TIMECARD_NMEA_UTC_CONTROL *response);

NTSTATUS TimeCardSignalQuery(PDEVICE_CONTEXT context, ULONG generator,
                             TIMECARD_SIGNAL_CONTROL *control);
NTSTATUS TimeCardSignalSet(PDEVICE_CONTEXT context,
                            const TIMECARD_SIGNAL_CONTROL *request,
                            TIMECARD_SIGNAL_CONTROL *response);
NTSTATUS TimeCardSignalEventRead(
    PDEVICE_CONTEXT context,
    const TIMECARD_SIGNAL_EVENT_BATCH *request,
    TIMECARD_SIGNAL_EVENT_BATCH *response);
BOOLEAN TimeCardSignalMessageRangeRelevant(
    PDEVICE_CONTEXT context, ULONG first, ULONG count);
VOID TimeCardSignalMarkInterruptRange(
    PDEVICE_CONTEXT context, ULONG first, ULONG count);
BOOLEAN TimeCardHandleSignalInterrupt(
    PDEVICE_CONTEXT context, ULONG messageId);
VOID TimeCardSignalInterruptInitialize(PDEVICE_CONTEXT context);
VOID TimeCardSignalInterruptPowerDown(PDEVICE_CONTEXT context);
VOID TimeCardSignalInterruptPowerUp(PDEVICE_CONTEXT context);
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
NTSTATUS TimeCardAlteraFlashInitialize(PDEVICE_CONTEXT context);
NTSTATUS TimeCardAlteraFlashTransfer(PDEVICE_CONTEXT context,
                                     const UCHAR *transmit,
                                     UCHAR *receive,
                                     ULONG length);
ULONG TimeCardAlteraFlashStatus(PDEVICE_CONTEXT context);
