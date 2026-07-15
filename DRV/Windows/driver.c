/* SPDX-License-Identifier: BSD-3-Clause */
/* OCP TimeCard Windows KMDF driver: entry, PnP, and BAR discovery. */

#include "timecard.h"

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
TimeCardSelectLayout(PDEVICE_CONTEXT context)
{
    ULONG uartOffsets[TIMECARD_UART_COUNT];
    ULONG smaMap1Offset;
    ULONG smaMap2Offset;
    ULONG signalBaseOffset;
    ULONG frequencyBaseOffset;
    BOOLEAN useMsix = context->InterruptMessages > 1;
    ULONG i;

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
    context->NmeaOut = NULL;
    context->I2c = NULL;
    context->I2cKnownDeviceMask = 0;
    context->I2cLastStartTrace = 0;
    context->I2cLastStartEvents = 0;
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
    KdPrint(("timecard: BAR length 0x%lx, %lu interrupt message(s), "
             "layout %s, clock offset 0x%lx\n",
             context->Bar0Length, context->InterruptMessages,
             context->Layout == TIMECARD_LAYOUT_MSIX ? "MSI-X" : "MSI",
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
