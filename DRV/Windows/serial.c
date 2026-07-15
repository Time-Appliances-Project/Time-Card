/* SPDX-License-Identifier: BSD-3-Clause */
/* Polled 16550 UART access for GNSS, GNSS2, MAC, and NMEA ports. */

#include "timecard.h"

#define UART_RBR 0u
#define UART_THR 0u
#define UART_DLL 0u
#define UART_IER 1u
#define UART_DLM 1u
#define UART_FCR 2u
#define UART_LCR 3u
#define UART_MCR 4u
#define UART_LSR 5u

#define UART_LCR_DLAB 0x80u
#define UART_LCR_8N1  0x03u
#define UART_FCR_INIT 0x07u
#define UART_MCR_INIT 0x03u
#define UART_LSR_DR   0x01u
#define UART_LSR_THRE 0x20u
#define UART_LSR_TEMT 0x40u

#define TIMECARD_UART_MAX_TIMEOUT_MS 5000u

static UCHAR
TimeCardUartReadRegister(PDEVICE_CONTEXT context, ULONG port, ULONG reg)
{
    return READ_REGISTER_UCHAR((PUCHAR)(context->Uart[port] + reg * 4u));
}

static VOID
TimeCardUartWriteRegister(PDEVICE_CONTEXT context, ULONG port, ULONG reg,
                          UCHAR value)
{
    WRITE_REGISTER_UCHAR((PUCHAR)(context->Uart[port] + reg * 4u), value);
}

static BOOLEAN
TimeCardUartValid(PDEVICE_CONTEXT context, ULONG port)
{
    return context->HardwareReady && port < TIMECARD_UART_COUNT &&
           context->Uart[port] != NULL;
}

static ULONG
TimeCardUartClampTimeout(ULONG milliseconds)
{
    return milliseconds > TIMECARD_UART_MAX_TIMEOUT_MS ?
           TIMECARD_UART_MAX_TIMEOUT_MS : milliseconds;
}

static VOID
TimeCardUartPause(VOID)
{
    LARGE_INTEGER interval;

    interval.QuadPart = -10000; /* one millisecond, relative */
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

NTSTATUS
TimeCardUartConfigure(PDEVICE_CONTEXT context,
                      const TIMECARD_UART_CONFIG *config)
{
    ULONG divisor;

    if (!TimeCardUartValid(context, config->Port))
        return STATUS_INVALID_DEVICE_STATE;
    if (config->Baud < 50u || config->Baud > 3000000u)
        return STATUS_INVALID_PARAMETER;

    divisor = (TIMECARD_UART_CLOCK_HZ + config->Baud * 8u) /
              (config->Baud * 16u);
    if (divisor == 0 || divisor > 0xffffu)
        return STATUS_INVALID_PARAMETER;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    TimeCardUartWriteRegister(context, config->Port, UART_IER, 0);
    TimeCardUartWriteRegister(context, config->Port, UART_LCR,
                              UART_LCR_DLAB);
    TimeCardUartWriteRegister(context, config->Port, UART_DLL,
                              (UCHAR)divisor);
    TimeCardUartWriteRegister(context, config->Port, UART_DLM,
                              (UCHAR)(divisor >> 8));
    TimeCardUartWriteRegister(context, config->Port, UART_LCR, UART_LCR_8N1);
    TimeCardUartWriteRegister(context, config->Port, UART_FCR, UART_FCR_INIT);
    TimeCardUartWriteRegister(context, config->Port, UART_MCR, UART_MCR_INIT);
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardUartRead(PDEVICE_CONTEXT context,
                 const TIMECARD_UART_READ_REQUEST *request,
                 TIMECARD_UART_TRANSFER *transfer)
{
    ULONGLONG deadline;
    ULONG maximum;
    ULONG timeout;

    if (!TimeCardUartValid(context, request->Port))
        return STATUS_INVALID_DEVICE_STATE;
    if (request->MaximumBytes == 0)
        return STATUS_INVALID_PARAMETER;

    maximum = request->MaximumBytes;
    if (maximum > TIMECARD_UART_MAX_TRANSFER)
        maximum = TIMECARD_UART_MAX_TRANSFER;
    timeout = TimeCardUartClampTimeout(request->TimeoutMilliseconds);
    deadline = KeQueryInterruptTime() + (ULONGLONG)timeout * 10000u;

    RtlZeroMemory(transfer, sizeof(*transfer));
    transfer->Port = request->Port;
    transfer->TimeoutMilliseconds = timeout;

    do {
        UCHAR lsr;

        WdfWaitLockAcquire(context->RegisterLock, NULL);
        lsr = TimeCardUartReadRegister(context, request->Port, UART_LSR);
        transfer->LineStatus |= lsr;
        while ((lsr & UART_LSR_DR) != 0 && transfer->Length < maximum) {
            transfer->Data[transfer->Length++] =
                TimeCardUartReadRegister(context, request->Port, UART_RBR);
            lsr = TimeCardUartReadRegister(context, request->Port, UART_LSR);
            transfer->LineStatus |= lsr;
        }
        WdfWaitLockRelease(context->RegisterLock);

        if (transfer->Length >= maximum || timeout == 0)
            break;
        if (KeQueryInterruptTime() >= deadline)
            break;
        TimeCardUartPause();
    } while (TRUE);

    return transfer->Length != 0 ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

NTSTATUS
TimeCardUartWrite(PDEVICE_CONTEXT context,
                  const TIMECARD_UART_TRANSFER *transfer,
                  ULONG inputLength,
                  TIMECARD_UART_RESULT *result)
{
    ULONGLONG deadline;
    ULONG required;
    ULONG timeout;

    if (!TimeCardUartValid(context, transfer->Port))
        return STATUS_INVALID_DEVICE_STATE;
    if (transfer->Length > TIMECARD_UART_MAX_TRANSFER)
        return STATUS_INVALID_PARAMETER;
    required = FIELD_OFFSET(TIMECARD_UART_TRANSFER, Data) + transfer->Length;
    if (inputLength < required)
        return STATUS_BUFFER_TOO_SMALL;

    timeout = TimeCardUartClampTimeout(transfer->TimeoutMilliseconds);
    deadline = KeQueryInterruptTime() + (ULONGLONG)timeout * 10000u;
    RtlZeroMemory(result, sizeof(*result));

    while (result->BytesTransferred < transfer->Length) {
        UCHAR lsr;

        WdfWaitLockAcquire(context->RegisterLock, NULL);
        lsr = TimeCardUartReadRegister(context, transfer->Port, UART_LSR);
        result->LineStatus |= lsr;
        if ((lsr & UART_LSR_THRE) != 0) {
            TimeCardUartWriteRegister(
                context, transfer->Port, UART_THR,
                transfer->Data[result->BytesTransferred]);
            ++result->BytesTransferred;
        }
        WdfWaitLockRelease(context->RegisterLock);

        if (result->BytesTransferred == transfer->Length)
            break;
        if (timeout == 0 || KeQueryInterruptTime() >= deadline)
            return STATUS_IO_TIMEOUT;
        TimeCardUartPause();
    }

    /* Report the final line state; completion does not require TEMT. */
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    result->LineStatus |=
        TimeCardUartReadRegister(context, transfer->Port, UART_LSR);
    WdfWaitLockRelease(context->RegisterLock);
    UNREFERENCED_PARAMETER(UART_LSR_TEMT);
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardUartObserve(PDEVICE_CONTEXT context,
                    const TIMECARD_UART_OBSERVE *request,
                    TIMECARD_UART_OBSERVE *response)
{
    ULONGLONG deadline;
    ULONG timeout;

    if (request->Size < sizeof(*request) ||
        request->Port >= TIMECARD_UART_COUNT)
        return STATUS_INVALID_PARAMETER;
    if (!TimeCardUartValid(context, request->Port))
        return STATUS_INVALID_DEVICE_STATE;

    timeout = TimeCardUartClampTimeout(request->TimeoutMilliseconds);
    deadline = KeQueryInterruptTime() + (ULONGLONG)timeout * 10000u;
    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Port = request->Port;
    response->TimeoutMilliseconds = timeout;
    response->Flags = TIMECARD_UART_OBSERVE_FLAG_PRESENT;

    do {
        UCHAR lsr;

        WdfWaitLockAcquire(context->RegisterLock, NULL);
        lsr = TimeCardUartReadRegister(context, request->Port, UART_LSR);
        WdfWaitLockRelease(context->RegisterLock);
        response->LineStatus |= lsr;
        if ((lsr & UART_LSR_DR) != 0) {
            response->Flags |= TIMECARD_UART_OBSERVE_FLAG_ACTIVITY;
            break;
        }
        if (timeout == 0 || KeQueryInterruptTime() >= deadline)
            break;
        TimeCardUartPause();
    } while (TRUE);

    return STATUS_SUCCESS;
}
