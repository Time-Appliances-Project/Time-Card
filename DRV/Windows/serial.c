/* SPDX-License-Identifier: BSD-3-Clause */
/* Interrupt-backed 16550 UART access for GNSS, GNSS2, MAC, and NMEA ports. */

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
#define UART_IER_RDI  0x01u
#define UART_LSR_THRE 0x20u
#define UART_LSR_TEMT 0x40u

#define TIMECARD_UART_MAX_TIMEOUT_MS 5000u
#define TIMECARD_UART_NO_PORT        MAXULONG
#define TIMECARD_UART_BURST_POLL_US  50u
#define TIMECARD_UART_BURST_IDLE_US  2000u

C_ASSERT((TIMECARD_UART_RX_RING_SIZE &
          (TIMECARD_UART_RX_RING_SIZE - 1u)) == 0);

static UCHAR TimeCardUartReadRegister(PDEVICE_CONTEXT context, ULONG port,
                                      ULONG reg);
static BOOLEAN TimeCardUartValid(PDEVICE_CONTEXT context, ULONG port);

static ULONG
TimeCardUartPortForMessage(PDEVICE_CONTEXT context, ULONG messageId)
{
    static const ULONG msiMessages[TIMECARD_UART_COUNT] = { 3u, 4u, 5u, 10u };
    static const ULONG msixMessages[TIMECARD_UART_COUNT] = { 35u, 36u, 37u, 42u };
    static const ULONG artMessages[TIMECARD_UART_COUNT] = {
        3u, MAXULONG, 7u, MAXULONG
    };
    const ULONG *messages = context->Layout == TIMECARD_LAYOUT_MSIX ?
        msixMessages : context->Layout == TIMECARD_LAYOUT_ART ?
        artMessages : msiMessages;
    ULONG port;

    for (port = 0; port < TIMECARD_UART_COUNT; ++port) {
        if (messages[port] == messageId)
            return port;
    }
    return TIMECARD_UART_NO_PORT;
}

static BOOLEAN
TimeCardUartHasInterrupt(PDEVICE_CONTEXT context, ULONG port)
{
    ULONG message;

    if (context->UartInterruptCount == 0)
        return FALSE;
    for (message = 0; message < context->InterruptMessages; ++message) {
        if (TimeCardUartPortForMessage(context, message) == port)
            return TRUE;
    }
    return FALSE;
}

static VOID
TimeCardUartRingPush(PDEVICE_CONTEXT context, ULONG port, UCHAR value)
{
    ULONG head = (ULONG)context->UartRxHead[port];
    ULONG next = (head + 1u) & (TIMECARD_UART_RX_RING_SIZE - 1u);

    if (next == (ULONG)context->UartRxTail[port]) {
        InterlockedIncrement(&context->UartRxDropped[port]);
        return;
    }
    context->UartRxRing[port][head] = value;
    KeMemoryBarrier();
    InterlockedExchange(&context->UartRxHead[port], (LONG)next);
}

static VOID
TimeCardUartRingDrain(PDEVICE_CONTEXT context, ULONG port,
                      TIMECARD_UART_TRANSFER *transfer, ULONG maximum)
{
    while (transfer->Length < maximum) {
        ULONG tail = (ULONG)context->UartRxTail[port];

        if (tail == (ULONG)context->UartRxHead[port])
            break;
        KeMemoryBarrier();
        transfer->Data[transfer->Length++] = context->UartRxRing[port][tail];
        InterlockedExchange(&context->UartRxTail[port],
            (LONG)((tail + 1u) & (TIMECARD_UART_RX_RING_SIZE - 1u)));
    }
}

BOOLEAN
TimeCardEvtUartInterruptIsr(WDFINTERRUPT interrupt, ULONG messageId)
{
    PTIMECARD_UART_INTERRUPT_CONTEXT interruptContext =
        UartInterruptGetContext(interrupt);
    PDEVICE_CONTEXT context = interruptContext->DeviceContext;
    ULONG globalMessageId = interruptContext->MessageBase + messageId;
    ULONG port = TimeCardUartPortForMessage(context, globalMessageId);
    ULONG drained = 0;
    BOOLEAN handled;
    UCHAR lsr;

    handled = TimeCardHandleTimestampInterrupt(context, globalMessageId);
    handled = TimeCardHandleSignalInterrupt(context, globalMessageId) ||
        handled;

    /*
     * A few PCI stacks report a global MSI-X message number even when the
     * CM_RESOURCE descriptor describes only one vector. Accept both forms.
     */
    if (port == TIMECARD_UART_NO_PORT && messageId != globalMessageId)
        port = TimeCardUartPortForMessage(context, messageId);
    if (!handled && messageId != globalMessageId)
        handled = TimeCardHandleTimestampInterrupt(context, messageId);
    if (messageId != globalMessageId)
        handled = TimeCardHandleSignalInterrupt(context, messageId) ||
            handled;
    if (port == TIMECARD_UART_NO_PORT || !TimeCardUartValid(context, port))
        return handled;
    lsr = TimeCardUartReadRegister(context, port, UART_LSR);
    InterlockedOr(&context->UartRxLineStatus[port], lsr);
    while ((lsr & UART_LSR_DR) != 0 && drained < 1024u) {
        TimeCardUartRingPush(
            context, port, TimeCardUartReadRegister(context, port, UART_RBR));
        ++drained;
        lsr = TimeCardUartReadRegister(context, port, UART_LSR);
        InterlockedOr(&context->UartRxLineStatus[port], lsr);
    }
    return handled || drained != 0;
}

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
TimeCardUartSnapshotHardware(
    PDEVICE_CONTEXT context, ULONG port,
    PTIMECARD_UART_HARDWARE_STATE state)
{
    if (!TimeCardUartValid(context, port) || state == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    state->LineControl = TimeCardUartReadRegister(context, port, UART_LCR);
    TimeCardUartWriteRegister(context, port, UART_LCR,
                              state->LineControl & ~UART_LCR_DLAB);
    state->InterruptEnable = TimeCardUartReadRegister(
        context, port, UART_IER);
    TimeCardUartWriteRegister(context, port, UART_IER, 0u);
    state->ModemControl = TimeCardUartReadRegister(context, port, UART_MCR);
    TimeCardUartWriteRegister(context, port, UART_LCR,
                              state->LineControl | UART_LCR_DLAB);
    state->DivisorLow = TimeCardUartReadRegister(context, port, UART_DLL);
    state->DivisorHigh = TimeCardUartReadRegister(context, port, UART_DLM);
    TimeCardUartWriteRegister(context, port, UART_LCR,
                              state->LineControl & ~UART_LCR_DLAB);
    TimeCardUartWriteRegister(context, port, UART_IER,
                              state->InterruptEnable);
    TimeCardUartWriteRegister(context, port, UART_LCR,
                              state->LineControl);
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardUartRestoreHardware(
    PDEVICE_CONTEXT context, ULONG port,
    const TIMECARD_UART_HARDWARE_STATE *state)
{
    if (!TimeCardUartValid(context, port) || state == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    TimeCardUartWriteRegister(context, port, UART_LCR,
                              state->LineControl & ~UART_LCR_DLAB);
    TimeCardUartWriteRegister(context, port, UART_IER, 0u);
    TimeCardUartWriteRegister(context, port, UART_MCR, state->ModemControl);
    TimeCardUartWriteRegister(context, port, UART_LCR,
                              state->LineControl | UART_LCR_DLAB);
    TimeCardUartWriteRegister(context, port, UART_DLL, state->DivisorLow);
    TimeCardUartWriteRegister(context, port, UART_DLM, state->DivisorHigh);
    TimeCardUartWriteRegister(context, port, UART_LCR,
                              state->LineControl & ~UART_LCR_DLAB);
    TimeCardUartWriteRegister(context, port, UART_IER,
                              state->InterruptEnable);
    TimeCardUartWriteRegister(context, port, UART_LCR,
                              state->LineControl);
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
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
    InterlockedExchange(&context->UartRxHead[config->Port], 0);
    InterlockedExchange(&context->UartRxTail[config->Port], 0);
    InterlockedExchange(&context->UartRxLineStatus[config->Port], 0);
    InterlockedExchange(&context->UartRxDropped[config->Port], 0);
    if (TimeCardUartHasInterrupt(context, config->Port))
        TimeCardUartWriteRegister(
            context, config->Port, UART_IER, UART_IER_RDI);
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardUartRead(PDEVICE_CONTEXT context,
                 const TIMECARD_UART_READ_REQUEST *request,
                 TIMECARD_UART_TRANSFER *transfer)
{
    BOOLEAN burstActive = FALSE;
    ULONGLONG deadline;
    ULONG burstIdlePolls = 0;
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
    transfer->LineStatus = (ULONG)InterlockedExchange(
        &context->UartRxLineStatus[request->Port], 0);

    do {
        BOOLEAN received = FALSE;
        ULONG previousLength = transfer->Length;
        UCHAR lsr;

        TimeCardUartRingDrain(context, request->Port, transfer, maximum);
        if (transfer->Length != previousLength) {
            burstActive = TRUE;
            burstIdlePolls = 0;
        }
        if (transfer->Length >= maximum)
            break;
        WdfWaitLockAcquire(context->RegisterLock, NULL);
        lsr = TimeCardUartReadRegister(context, request->Port, UART_LSR);
        transfer->LineStatus |= lsr;
        while ((lsr & UART_LSR_DR) != 0 && transfer->Length < maximum) {
            transfer->Data[transfer->Length++] =
                TimeCardUartReadRegister(context, request->Port, UART_RBR);
            received = TRUE;
            lsr = TimeCardUartReadRegister(context, request->Port, UART_LSR);
            transfer->LineStatus |= lsr;
        }
        WdfWaitLockRelease(context->RegisterLock);

        if (received) {
            burstActive = TRUE;
            burstIdlePolls = 0;
        } else if (burstActive) {
            ++burstIdlePolls;
            if (burstIdlePolls >=
                TIMECARD_UART_BURST_IDLE_US /
                TIMECARD_UART_BURST_POLL_US) {
                break;
            }
        }

        if (transfer->Length >= maximum || (!burstActive && timeout == 0))
            break;
        if (!burstActive && KeQueryInterruptTime() >= deadline)
            break;
        if (burstActive)
            KeStallExecutionProcessor(TIMECARD_UART_BURST_POLL_US);
        else
            TimeCardUartPause();
    } while (TRUE);

    return transfer->Length != 0 ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

VOID
TimeCardUartDisableInterrupts(PDEVICE_CONTEXT context)
{
    ULONG port;

    if (!context->HardwareReady)
        return;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    for (port = 0; port < TIMECARD_UART_COUNT; ++port) {
        if (context->Uart[port] != NULL)
            TimeCardUartWriteRegister(context, port, UART_IER, 0);
    }
    WdfWaitLockRelease(context->RegisterLock);
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
    response->Reserved[0] = context->UartInterruptCount;
    response->Reserved[1] =
        ((ULONG)context->UartRxHead[request->Port] -
         (ULONG)context->UartRxTail[request->Port]) &
        (TIMECARD_UART_RX_RING_SIZE - 1u);
    response->Reserved[2] = (ULONG)context->UartRxDropped[request->Port];

    if (context->UartRxHead[request->Port] !=
        context->UartRxTail[request->Port]) {
        response->Flags |= TIMECARD_UART_OBSERVE_FLAG_ACTIVITY;
        response->LineStatus = (ULONG)context->UartRxLineStatus[request->Port];
        return STATUS_SUCCESS;
    }

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
