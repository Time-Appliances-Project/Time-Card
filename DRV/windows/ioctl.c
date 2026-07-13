/* SPDX-License-Identifier: BSD-3-Clause */
/* IOCTL dispatch for the OCP TimeCard Windows driver. */

#include "timecard.h"

static NTSTATUS
TimeCardGetInput(WDFREQUEST request, size_t minimum, PVOID *buffer,
                 size_t *length)
{
    return WdfRequestRetrieveInputBuffer(request, minimum, buffer, length);
}

static NTSTATUS
TimeCardGetOutput(WDFREQUEST request, size_t minimum, PVOID *buffer)
{
    return WdfRequestRetrieveOutputBuffer(request, minimum, buffer, NULL);
}

VOID
TimeCardEvtIoDeviceControl(WDFQUEUE queue, WDFREQUEST request,
                           size_t outputBufferLength,
                           size_t inputBufferLength, ULONG ioControlCode)
{
    PDEVICE_CONTEXT context = DeviceGetContext(WdfIoQueueGetDevice(queue));
    size_t information = 0;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(outputBufferLength);

    switch (ioControlCode) {
    case IOCTL_TIMECARD_GET_TIME:
    {
        TIMECARD_TIME *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardGetTime(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_SET_TIME:
    {
        TIMECARD_TIME *input;
        TIMECARD_TIME value;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (NT_SUCCESS(status)) {
            value = *input;
            status = TimeCardSetTime(context, &value);
        }
        break;
    }

    case IOCTL_TIMECARD_GET_INFO:
    {
        TIMECARD_INFO *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardGetInfo(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_GET_CROSSTIMESTAMP:
    {
        TIMECARD_CROSSTIMESTAMP *output;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardGetCrossTimestamp(context, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_UART_CONFIGURE:
    {
        TIMECARD_UART_CONFIG *input;
        TIMECARD_UART_CONFIG config;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (NT_SUCCESS(status)) {
            config = *input;
            status = TimeCardUartConfigure(context, &config);
        }
        break;
    }

    case IOCTL_TIMECARD_UART_READ:
    {
        TIMECARD_UART_READ_REQUEST *input;
        TIMECARD_UART_READ_REQUEST readRequest;
        TIMECARD_UART_TRANSFER *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        readRequest = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardUartRead(context, &readRequest, output);
            if (NT_SUCCESS(status)) {
                information = FIELD_OFFSET(TIMECARD_UART_TRANSFER, Data) +
                              output->Length;
            }
        }
        break;
    }

    case IOCTL_TIMECARD_UART_WRITE:
    {
        TIMECARD_UART_TRANSFER *input;
        TIMECARD_UART_TRANSFER transfer;
        TIMECARD_UART_RESULT *output;
        size_t actualInputLength;

        status = TimeCardGetInput(
            request, FIELD_OFFSET(TIMECARD_UART_TRANSFER, Data),
            (PVOID *)&input, &actualInputLength);
        if (!NT_SUCCESS(status))
            break;
        if (actualInputLength > sizeof(transfer))
            actualInputLength = sizeof(transfer);
        RtlZeroMemory(&transfer, sizeof(transfer));
        RtlCopyMemory(&transfer, input, actualInputLength);

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardUartWrite(context, &transfer,
                                       (ULONG)inputBufferLength, output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    case IOCTL_TIMECARD_HIERARCHY_CONTROL:
    {
        TIMECARD_HIERARCHY_CONTROL *input;
        TIMECARD_HIERARCHY_CONTROL requestValue;
        TIMECARD_HIERARCHY_CONTROL *output;

        status = TimeCardGetInput(request, sizeof(*input),
                                  (PVOID *)&input, NULL);
        if (!NT_SUCCESS(status))
            break;
        requestValue = *input;

        status = TimeCardGetOutput(request, sizeof(*output),
                                   (PVOID *)&output);
        if (NT_SUCCESS(status)) {
            status = TimeCardControlHierarchy(context, &requestValue,
                                               output);
            if (NT_SUCCESS(status))
                information = sizeof(*output);
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(request, status, information);
}
