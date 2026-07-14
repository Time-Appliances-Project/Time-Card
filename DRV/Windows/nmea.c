/* SPDX-License-Identifier: BSD-3-Clause */
/* FPGA NMEA sentence-generator configuration, matching Linux ptp_ocp. */

#include "timecard.h"

#define TIMECARD_NMEA_ENABLE 0x00000001u
#define TIMECARD_NMEA_BAUD_MASK 0x0000000fu

static const ULONG TimeCardNmeaBaudRates[] = {
    1200u, 2400u, 4800u, 9600u, 19200u, 38400u, 57600u,
    115200u, 230400u, 460800u, 921600u, 1000000u, 2000000u
};

static NTSTATUS
TimeCardNmeaSelectorFromBaud(ULONG baud, PULONG selector)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(TimeCardNmeaBaudRates); ++i) {
        if (TimeCardNmeaBaudRates[i] == baud) {
            *selector = i;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
TimeCardNmeaQuery(PDEVICE_CONTEXT context, TIMECARD_NMEA_CONTROL *control)
{
    ULONG selector;

    if (!context->HardwareReady || context->NmeaOut == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Flags = TIMECARD_NMEA_FLAG_PRESENT;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    control->Control = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->Ctrl);
    control->Status = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->Status);
    control->Polarity = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->UartPolarity) & 1u;
    control->Version = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->Version);
    selector = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->UartBaud) & TIMECARD_NMEA_BAUD_MASK;
    WdfWaitLockRelease(context->RegisterLock);

    control->BaudSelector = selector;
    if (control->Control == MAXULONG && control->Status == MAXULONG &&
        control->Version == MAXULONG)
        return STATUS_NOT_SUPPORTED;
    if (selector < ARRAYSIZE(TimeCardNmeaBaudRates))
        control->Baud = TimeCardNmeaBaudRates[selector];
    if ((control->Control & TIMECARD_NMEA_ENABLE) != 0)
        control->Flags |= TIMECARD_NMEA_FLAG_ENABLED;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardNmeaSet(PDEVICE_CONTEXT context,
                const TIMECARD_NMEA_CONTROL *request,
                TIMECARD_NMEA_CONTROL *response)
{
    TIMECARD_NMEA_CONTROL current;
    TIMECARD_UART_CONFIG uartConfig;
    ULONG selector;
    ULONG control;
    NTSTATUS status;

    if (!context->HardwareReady || context->NmeaOut == NULL)
        return STATUS_NOT_SUPPORTED;
    if (request->Size < sizeof(*request) || request->Polarity > 1u ||
        (request->Flags & ~(TIMECARD_NMEA_FLAG_PRESENT |
                            TIMECARD_NMEA_FLAG_ENABLED)) != 0)
        return STATUS_INVALID_PARAMETER;

    status = TimeCardNmeaSelectorFromBaud(request->Baud, &selector);
    if (!NT_SUCCESS(status))
        return status;

    /* Confirm the optional block is implemented before touching its MMIO. */
    status = TimeCardNmeaQuery(context, &current);
    if (!NT_SUCCESS(status))
        return status;

    uartConfig.Port = TIMECARD_UART_NMEA;
    uartConfig.Baud = request->Baud;
    status = TimeCardUartConfigure(context, &uartConfig);
    if (!NT_SUCCESS(status))
        return status;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    control = current.Control;
    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->Ctrl,
                         control & ~TIMECARD_NMEA_ENABLE);
    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UartBaud, selector);
    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UartPolarity,
                         request->Polarity);
    if ((request->Flags & TIMECARD_NMEA_FLAG_ENABLED) != 0)
        control |= TIMECARD_NMEA_ENABLE;
    else
        control &= ~TIMECARD_NMEA_ENABLE;
    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->Ctrl, control);
    WdfWaitLockRelease(context->RegisterLock);
    return TimeCardNmeaQuery(context, response);
}

NTSTATUS
TimeCardNmeaInitialize(PDEVICE_CONTEXT context)
{
    TIMECARD_NMEA_CONTROL current;
    TIMECARD_NMEA_CONTROL request;
    TIMECARD_NMEA_CONTROL response;
    TIMECARD_UART_CONFIG uartConfig;
    NTSTATUS status;

    if (context->NmeaOut == NULL)
        return STATUS_NOT_SUPPORTED;

    status = TimeCardNmeaQuery(context, &current);
    if (!NT_SUCCESS(status))
        return status;
    if (current.Baud != 0 &&
        (current.Flags & TIMECARD_NMEA_FLAG_ENABLED) != 0) {
        uartConfig.Port = TIMECARD_UART_NMEA;
        uartConfig.Baud = current.Baud;
        return TimeCardUartConfigure(context, &uartConfig);
    }

    RtlZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.Flags = TIMECARD_NMEA_FLAG_ENABLED;
    request.Baud = 9600u;
    request.Polarity = 0u;
    return TimeCardNmeaSet(context, &request, &response);
}
