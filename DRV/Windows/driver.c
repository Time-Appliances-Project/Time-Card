/* SPDX-License-Identifier: BSD-3-Clause */
/* OCP TimeCard Windows KMDF driver: entry, PnP, and BAR discovery. */

#include "timecard.h"

static int
TimeCardHexDigit(WCHAR value)
{
    if (value >= L'0' && value <= L'9')
        return value - L'0';
    if (value >= L'A' && value <= L'F')
        return value - L'A' + 10;
    if (value >= L'a' && value <= L'f')
        return value - L'a' + 10;
    return -1;
}

static BOOLEAN
TimeCardFindPciField(const WCHAR *hardwareIds, ULONG characters,
                     const WCHAR *field, ULONG digits, PULONG value)
{
    ULONG i;

    for (i = 0; i + 5u + digits <= characters; ++i) {
        ULONG parsed = 0;
        ULONG j;

        if (hardwareIds[i] != L'&')
            continue;
        for (j = 0; j < 4u; ++j) {
            WCHAR actual = hardwareIds[i + 1u + j];
            WCHAR expected = field[j];

            if (actual >= L'a' && actual <= L'z')
                actual -= L'a' - L'A';
            if (actual != expected)
                break;
        }
        if (j != 4u)
            continue;
        for (j = 0; j < digits; ++j) {
            int digit = TimeCardHexDigit(hardwareIds[i + 5u + j]);

            if (digit < 0)
                break;
            parsed = (parsed << 4) | (ULONG)digit;
        }
        if (j == digits) {
            *value = parsed;
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
TimeCardGetPciIdentity(WDFDEVICE device, PUSHORT vendorId,
                       PUSHORT deviceId, PUCHAR revision)
{
    WCHAR hardwareIds[512];
    ULONG bytes = 0;
    ULONG characters;
    ULONG value;
    NTSTATUS status;

    *vendorId = MAXUSHORT;
    *deviceId = MAXUSHORT;
    *revision = TIMECARD_PCI_REVISION_UNKNOWN;
    RtlZeroMemory(hardwareIds, sizeof(hardwareIds));
    status = WdfDeviceQueryProperty(
        device, DevicePropertyHardwareID, sizeof(hardwareIds),
        hardwareIds, &bytes);
    if (!NT_SUCCESS(status))
        return;

    characters = bytes / sizeof(hardwareIds[0]);
    if (characters > RTL_NUMBER_OF(hardwareIds))
        characters = RTL_NUMBER_OF(hardwareIds);
    if (TimeCardFindPciField(hardwareIds, characters, L"VEN_", 4u, &value))
        *vendorId = (USHORT)value;
    if (TimeCardFindPciField(hardwareIds, characters, L"DEV_", 4u, &value))
        *deviceId = (USHORT)value;
    if (TimeCardFindPciField(hardwareIds, characters, L"REV_", 2u, &value))
        *revision = (UCHAR)value;
}

static VOID
TimeCardConfigureBoardProfile(PDEVICE_CONTEXT context)
{
    if (context->PciVendorId == 0x1ad7u &&
        context->PciDeviceId == 0xa000u) {
        context->BoardProfile = TIMECARD_BOARD_ART;
        context->SubsystemMask = TIMECARD_SUBSYSTEM_MASK_ART;
    } else {
        /*
         * Meta/Facebook (1d9b:0400) and Celestica (18d4:1008) share
         * the two fb resource maps in Linux. Keep that profile as the
         * conservative fallback if Windows omits a useful hardware ID.
         */
        context->BoardProfile = TIMECARD_BOARD_FB;
        context->SubsystemMask = TIMECARD_SUBSYSTEM_MASK_ALL;
    }
}

static BOOLEAN
TimeCardRangeFits(ULONG barLength, ULONG offset, ULONG length)
{
    return offset <= barLength && length <= barLength - offset;
}

static ULONG
TimeCardCountInterruptMessages(WDFCMRESLIST resourcesRaw,
                               WDFCMRESLIST resourcesTranslated)
{
    ULONG i;
    ULONG messages = 0;
    ULONG count = WdfCmResourceListGetCount(resourcesTranslated);

    for (i = 0; i < count; ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR translated;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR raw;

        translated = WdfCmResourceListGetDescriptor(resourcesTranslated, i);
        raw = WdfCmResourceListGetDescriptor(resourcesRaw, i);
        if (translated == NULL || translated->Type != CmResourceTypeInterrupt)
            continue;

        if ((translated->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0 &&
            raw != NULL && raw->Type == CmResourceTypeInterrupt &&
            raw->u.MessageInterrupt.Raw.MessageCount != 0) {
            messages += raw->u.MessageInterrupt.Raw.MessageCount;
        } else {
            ++messages;
        }
    }
    return messages;
}

static NTSTATUS
TimeCardSelectArtLayout(PDEVICE_CONTEXT context)
{
    ULONG i;

    context->Layout = TIMECARD_LAYOUT_ART;
    context->ClockOffset = TIMECARD_CLOCK_OFFSET_ART;
    context->TodOffset = TIMECARD_OFFSET_NONE;
    context->NmeaOutOffset = TIMECARD_OFFSET_NONE;
    context->I2cOffset = TIMECARD_I2C_OFFSET_ART;
    context->FlashOffset = TIMECARD_FLASH_OFFSET_ART;
    context->FlashFirmwareOffset = TIMECARD_FLASH_FIRMWARE_OFFSET_ART;
    context->I2cController = TIMECARD_I2C_CONTROLLER_OCORES;
    context->FlashController = TIMECARD_FLASH_CONTROLLER_ALTERA;

    context->Regs = NULL;
    context->Tod = NULL;
    context->NmeaOut = NULL;
    context->SmaMap1 = NULL;
    context->SmaMap2 = NULL;
    context->ArtSma = NULL;
    context->I2c = NULL;
    context->Flash = NULL;
    context->I2cKnownDeviceMask = 0;
    context->I2cLastStartTrace = 0;
    context->I2cLastStartEvents = 0;
    context->I2cLedAddress = 0;
    context->I2cEnvironmentAddress = 0;
    context->I2cImuAddress = 0;
    context->FlashJedecId = 0;
    context->FlashCapacity = 0;
    context->FlashFifoDepth = 0;
    for (i = 0; i < TIMECARD_UART_COUNT; ++i)
        context->Uart[i] = NULL;
    for (i = 0; i < TIMECARD_SIGNAL_COUNT; ++i) {
        context->Signal[i] = NULL;
        context->Frequency[i] = NULL;
    }

    if (!TimeCardRangeFits(context->Bar0Length, context->ClockOffset,
                           sizeof(OCP_REG))) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    context->Regs = (volatile OCP_REG *)(context->Bar0Base +
                                         context->ClockOffset);

    if (TimeCardRangeFits(context->Bar0Length,
                          TIMECARD_UART_GNSS_OFFSET_ART, 0x20u)) {
        context->Uart[TIMECARD_UART_GNSS] =
            context->Bar0Base + TIMECARD_UART_GNSS_OFFSET_ART;
    }
    if (TimeCardRangeFits(context->Bar0Length,
                          TIMECARD_UART_MAC_OFFSET_ART, 0x20u)) {
        context->Uart[TIMECARD_UART_MAC] =
            context->Bar0Base + TIMECARD_UART_MAC_OFFSET_ART;
    }
    if (TimeCardRangeFits(context->Bar0Length, TIMECARD_SMA_OFFSET_ART,
                          sizeof(TIMECARD_ART_SMA_REG))) {
        context->ArtSma = (volatile TIMECARD_ART_SMA_REG *)(
            context->Bar0Base + TIMECARD_SMA_OFFSET_ART);
    }
    if (TimeCardRangeFits(context->Bar0Length, context->I2cOffset, 0x100u))
        context->I2c = context->Bar0Base + context->I2cOffset;
    if (TimeCardRangeFits(context->Bar0Length, context->FlashOffset,
                          TIMECARD_REGISTER_WINDOW_SIZE)) {
        context->Flash = context->Bar0Base + context->FlashOffset;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardSelectLayout(PDEVICE_CONTEXT context)
{
    ULONG uartOffsets[TIMECARD_UART_COUNT];
    ULONG smaMap1Offset;
    ULONG smaMap2Offset;
    ULONG signalBaseOffset;
    ULONG frequencyBaseOffset;
    BOOLEAN useMsix;
    ULONG i;

    if (context->BoardProfile == TIMECARD_BOARD_ART)
        return TimeCardSelectArtLayout(context);

    /*
     * The Linux ptp_ocp driver uses its rev1 register map for MSI gateware
     * and its rev2 map for MSI-X/LitePCIe gateware. PCI MSI can expose up to
     * 32 messages, while the current MSI-X image exposes 64. Older boards
     * commonly report PCI revision 00 and either 2 or 32 MSI messages.
     */
    if (context->InterruptMessages > 32) {
        useMsix = TRUE;
    } else if (context->PciRevision != TIMECARD_PCI_REVISION_UNKNOWN) {
        useMsix = context->PciRevision >= 2;
    } else {
        useMsix = FALSE;
    }

    /*
     * Prefer the other known map when the selected core windows cannot fit
     * in the assigned BAR. This fails safely before any register access and
     * also covers firmware that does not publish a useful PCI revision.
     */
    if (useMsix &&
        (!TimeCardRangeFits(context->Bar0Length,
                            TIMECARD_CLOCK_OFFSET_MSIX, sizeof(OCP_REG)) ||
         !TimeCardRangeFits(context->Bar0Length,
                            TIMECARD_TOD_OFFSET_MSIX, sizeof(TOD_REG))) &&
        TimeCardRangeFits(context->Bar0Length,
                          TIMECARD_CLOCK_OFFSET_MSI, sizeof(OCP_REG)) &&
        TimeCardRangeFits(context->Bar0Length,
                          TIMECARD_TOD_OFFSET_MSI, sizeof(TOD_REG))) {
        useMsix = FALSE;
    } else if (!useMsix &&
               (!TimeCardRangeFits(context->Bar0Length,
                                   TIMECARD_CLOCK_OFFSET_MSI,
                                   sizeof(OCP_REG)) ||
                !TimeCardRangeFits(context->Bar0Length,
                                   TIMECARD_TOD_OFFSET_MSI,
                                   sizeof(TOD_REG))) &&
               TimeCardRangeFits(context->Bar0Length,
                                 TIMECARD_CLOCK_OFFSET_MSIX,
                                 sizeof(OCP_REG)) &&
               TimeCardRangeFits(context->Bar0Length,
                                 TIMECARD_TOD_OFFSET_MSIX,
                                 sizeof(TOD_REG))) {
        useMsix = TRUE;
    }

    if (useMsix) {
        context->Layout = TIMECARD_LAYOUT_MSIX;
        context->ClockOffset = TIMECARD_CLOCK_OFFSET_MSIX;
        context->TodOffset = TIMECARD_TOD_OFFSET_MSIX;
        context->NmeaOutOffset = TIMECARD_NMEA_OUT_OFFSET_MSIX;
        uartOffsets[TIMECARD_UART_GNSS] = TIMECARD_UART_GNSS_OFFSET_MSIX;
        uartOffsets[TIMECARD_UART_GNSS2] = TIMECARD_UART_GNSS2_OFFSET_MSIX;
        uartOffsets[TIMECARD_UART_MAC] = TIMECARD_UART_MAC_OFFSET_MSIX;
        uartOffsets[TIMECARD_UART_NMEA] = TIMECARD_UART_NMEA_OFFSET_MSIX;
        smaMap1Offset = TIMECARD_SMA_MAP1_OFFSET_MSIX;
        smaMap2Offset = TIMECARD_SMA_MAP2_OFFSET_MSIX;
        signalBaseOffset = TIMECARD_SIGNAL_BASE_MSIX;
        frequencyBaseOffset = TIMECARD_FREQUENCY_BASE_MSIX;
        context->I2cOffset = TIMECARD_I2C_OFFSET_MSIX;
        context->FlashOffset = TIMECARD_FLASH_OFFSET_MSIX;
    } else {
        context->Layout = TIMECARD_LAYOUT_MSI;
        context->ClockOffset = TIMECARD_CLOCK_OFFSET_MSI;
        context->TodOffset = TIMECARD_TOD_OFFSET_MSI;
        context->NmeaOutOffset = TIMECARD_NMEA_OUT_OFFSET_MSI;
        uartOffsets[TIMECARD_UART_GNSS] = TIMECARD_UART_GNSS_OFFSET_MSI;
        uartOffsets[TIMECARD_UART_GNSS2] = TIMECARD_UART_GNSS2_OFFSET_MSI;
        uartOffsets[TIMECARD_UART_MAC] = TIMECARD_UART_MAC_OFFSET_MSI;
        uartOffsets[TIMECARD_UART_NMEA] = TIMECARD_UART_NMEA_OFFSET_MSI;
        smaMap1Offset = TIMECARD_SMA_MAP1_OFFSET_MSI;
        smaMap2Offset = TIMECARD_SMA_MAP2_OFFSET_MSI;
        signalBaseOffset = TIMECARD_SIGNAL_BASE_MSI;
        frequencyBaseOffset = TIMECARD_FREQUENCY_BASE_MSI;
        context->I2cOffset = TIMECARD_I2C_OFFSET_MSI;
        context->FlashOffset = TIMECARD_FLASH_OFFSET_MSI;
    }
    context->FlashFirmwareOffset = TIMECARD_FLASH_FIRMWARE_OFFSET_FB;
    context->I2cController = TIMECARD_I2C_CONTROLLER_XIIC;
    context->FlashController = TIMECARD_FLASH_CONTROLLER_XILINX;

    if (!TimeCardRangeFits(context->Bar0Length, context->ClockOffset,
                           sizeof(OCP_REG)) ||
        !TimeCardRangeFits(context->Bar0Length, context->TodOffset,
                           sizeof(TOD_REG))) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    context->Regs = (volatile OCP_REG *)(context->Bar0Base +
                                         context->ClockOffset);
    context->Tod = (volatile TOD_REG *)(context->Bar0Base +
                                        context->TodOffset);

    for (i = 0; i < TIMECARD_UART_COUNT; ++i) {
        if (!TimeCardRangeFits(context->Bar0Length, uartOffsets[i], 0x20u))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        context->Uart[i] = context->Bar0Base + uartOffsets[i];
    }

    /* SMA routing is optional and must never prevent the controller booting. */
    context->SmaMap1 = NULL;
    context->SmaMap2 = NULL;
    context->ArtSma = NULL;
    context->NmeaOut = NULL;
    context->I2c = NULL;
    context->I2cKnownDeviceMask = 0;
    context->I2cLastStartTrace = 0;
    context->I2cLastStartEvents = 0;
    context->I2cLedAddress = 0;
    context->I2cEnvironmentAddress = 0;
    context->I2cImuAddress = 0;
    context->Flash = NULL;
    context->FlashJedecId = 0;
    context->FlashCapacity = 0;
    context->FlashFifoDepth = 0;
    for (i = 0; i < TIMECARD_SIGNAL_COUNT; ++i) {
        ULONG signalOffset = signalBaseOffset +
                             i * TIMECARD_REGISTER_WINDOW_SIZE;
        ULONG frequencyOffset = frequencyBaseOffset +
                                i * TIMECARD_REGISTER_WINDOW_SIZE;

        context->Signal[i] = TimeCardRangeFits(
            context->Bar0Length, signalOffset,
            sizeof(TIMECARD_SIGNAL_REG)) ?
            (volatile TIMECARD_SIGNAL_REG *)(context->Bar0Base +
                                              signalOffset) : NULL;
        context->Frequency[i] = TimeCardRangeFits(
            context->Bar0Length, frequencyOffset,
            sizeof(TIMECARD_FREQUENCY_REG)) ?
            (volatile TIMECARD_FREQUENCY_REG *)(context->Bar0Base +
                                                 frequencyOffset) : NULL;
    }
    if (TimeCardRangeFits(context->Bar0Length, smaMap1Offset,
                          sizeof(TIMECARD_GPIO_REG)) &&
        TimeCardRangeFits(context->Bar0Length, smaMap2Offset,
                          sizeof(TIMECARD_GPIO_REG))) {
        context->SmaMap1 = (volatile TIMECARD_GPIO_REG *)(
            context->Bar0Base + smaMap1Offset);
        context->SmaMap2 = (volatile TIMECARD_GPIO_REG *)(
            context->Bar0Base + smaMap2Offset);
    }

    /* The FPGA NMEA sentence generator is optional on older images. */
    if (TimeCardRangeFits(context->Bar0Length, context->NmeaOutOffset,
                          sizeof(TOD_REG))) {
        context->NmeaOut = (volatile TOD_REG *)(
            context->Bar0Base + context->NmeaOutOffset);
    }

    /* I2C is optional and is only initialized when an I2C IOCTL is used. */
    if (TimeCardRangeFits(context->Bar0Length, context->I2cOffset,
                          TIMECARD_REGISTER_WINDOW_SIZE)) {
        context->I2c = context->Bar0Base + context->I2cOffset;
    }

    /* The Xilinx SPI controller owns the FPGA firmware region of SPI-NOR. */
    if (TimeCardRangeFits(context->Bar0Length, context->FlashOffset,
                          TIMECARD_REGISTER_WINDOW_SIZE)) {
        context->Flash = context->Bar0Base + context->FlashOffset;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, TimeCardEvtDeviceAdd);
    return WdfDriverCreate(driverObject, registryPath,
                           WDF_NO_OBJECT_ATTRIBUTES, &config,
                           WDF_NO_HANDLE);
}

NTSTATUS
TimeCardEvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT deviceInit)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS powerCallbacks;
    WDF_IO_QUEUE_CONFIG queueConfig;
    DECLARE_CONST_UNICODE_STRING(symbolicLink, TIMECARD_DOS_DEVICE_NAME);
    PDEVICE_CONTEXT context;
    BOOLEAN enableHierarchy = FALSE;
    WDFDEVICE device;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(driver);

    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetIoType(deviceInit, WdfDeviceIoBuffered);
    WdfDeviceInitSetCharacteristics(deviceInit, FILE_DEVICE_SECURE_OPEN,
                                    FALSE);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&powerCallbacks);
    powerCallbacks.EvtDevicePrepareHardware = TimeCardEvtPrepareHardware;
    powerCallbacks.EvtDeviceReleaseHardware = TimeCardEvtReleaseHardware;
    powerCallbacks.EvtDeviceD0Entry = TimeCardEvtD0Entry;
    powerCallbacks.EvtDeviceD0Exit = TimeCardEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &powerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&deviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
        return status;

    context = DeviceGetContext(device);
    RtlZeroMemory(context, sizeof(*context));
    context->Device = device;
    TimeCardGetPciIdentity(device, &context->PciVendorId,
                           &context->PciDeviceId,
                           &context->PciRevision);
    TimeCardConfigureBoardProfile(context);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfWaitLockCreate(&attributes, &context->RegisterLock);
    if (!NT_SUCCESS(status))
        return status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig,
                                            WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = TimeCardEvtIoDeviceControl;
    status = WdfIoQueueCreate(device, &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
        return status;

    status = WdfDeviceCreateSymbolicLink(device, &symbolicLink);
    if (!NT_SUCCESS(status))
        return status;

    status = TimeCardGetHierarchySetting(context, &enableHierarchy);
    if (!NT_SUCCESS(status)) {
        KdPrint(("timecard: hierarchy setting unavailable, status 0x%08lx; "
                 "starting controller only\n", status));
        return STATUS_SUCCESS;
    }
    if (!enableHierarchy)
        return STATUS_SUCCESS;

    status = TimeCardCreateSubsystemDevices(context);
    if (!NT_SUCCESS(status)) {
        /*
         * Subsystem nodes are descriptive children, not a prerequisite for
         * PHC/UART operation. Never fail the controller AddDevice path (and
         * therefore boot) merely because optional hierarchy creation failed.
         */
        KdPrint(("timecard: subsystem hierarchy disabled, status 0x%08lx\n",
                 status));
    }
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardEvtPrepareHardware(WDFDEVICE device, WDFCMRESLIST resourcesRaw,
                           WDFCMRESLIST resourcesTranslated)
{
    PDEVICE_CONTEXT context = DeviceGetContext(device);
    PHYSICAL_ADDRESS barStart;
    ULONG barLength = 0;
    ULONG i;
    NTSTATUS status;

    barStart.QuadPart = 0;
    context->InterruptMessages =
        TimeCardCountInterruptMessages(resourcesRaw, resourcesTranslated);
    TimeCardGetPciIdentity(device, &context->PciVendorId,
                           &context->PciDeviceId,
                           &context->PciRevision);
    TimeCardConfigureBoardProfile(context);

    for (i = 0; i < WdfCmResourceListGetCount(resourcesTranslated); ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;

        descriptor = WdfCmResourceListGetDescriptor(resourcesTranslated, i);
        if (descriptor != NULL && descriptor->Type == CmResourceTypeMemory &&
            descriptor->u.Memory.Length > barLength) {
            barStart = descriptor->u.Memory.Start;
            barLength = descriptor->u.Memory.Length;
        }
    }

    if (barLength == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    context->Bar0Base = (volatile UCHAR *)MmMapIoSpaceEx(
        barStart, barLength, PAGE_READWRITE | PAGE_NOCACHE);
    if (context->Bar0Base == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    context->Bar0Length = barLength;

    status = TimeCardSelectLayout(context);
    if (!NT_SUCCESS(status)) {
        MmUnmapIoSpace((PVOID)context->Bar0Base, context->Bar0Length);
        context->Bar0Base = NULL;
        context->Bar0Length = 0;
        return status;
    }

    context->HardwareReady = TRUE;
    KdPrint(("timecard: PCI %04x:%04x revision 0x%02x, BAR length 0x%lx, "
             "%lu interrupt message(s), layout %lu, clock offset 0x%lx\n",
             context->PciVendorId, context->PciDeviceId,
             context->PciRevision,
             context->Bar0Length, context->InterruptMessages,
             context->Layout,
             context->ClockOffset));
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardEvtReleaseHardware(WDFDEVICE device,
                           WDFCMRESLIST resourcesTranslated)
{
    PDEVICE_CONTEXT context = DeviceGetContext(device);
    ULONG i;

    UNREFERENCED_PARAMETER(resourcesTranslated);
    context->HardwareReady = FALSE;
    context->Regs = NULL;
    context->Tod = NULL;
    context->NmeaOut = NULL;
    context->SmaMap1 = NULL;
    context->SmaMap2 = NULL;
    context->ArtSma = NULL;
    context->I2c = NULL;
    context->Flash = NULL;
    context->FlashJedecId = 0;
    context->FlashCapacity = 0;
    context->FlashFifoDepth = 0;
    for (i = 0; i < TIMECARD_SIGNAL_COUNT; ++i) {
        context->Signal[i] = NULL;
        context->Frequency[i] = NULL;
    }
    for (i = 0; i < TIMECARD_UART_COUNT; ++i)
        context->Uart[i] = NULL;
    if (context->Bar0Base != NULL) {
        MmUnmapIoSpace((PVOID)context->Bar0Base, context->Bar0Length);
        context->Bar0Base = NULL;
        context->Bar0Length = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardEvtD0Entry(WDFDEVICE device, WDF_POWER_DEVICE_STATE previousState)
{
    PDEVICE_CONTEXT context = DeviceGetContext(device);
    NTSTATUS status;

    UNREFERENCED_PARAMETER(previousState);

    status = TimeCardNmeaInitialize(context);
    if (!NT_SUCCESS(status)) {
        KdPrint(("timecard: NMEA output initialization unavailable, "
                 "status 0x%08lx\n", status));
    }
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardEvtD0Exit(WDFDEVICE device, WDF_POWER_DEVICE_STATE targetState)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(targetState);
    return STATUS_SUCCESS;
}
