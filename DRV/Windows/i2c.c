/* SPDX-License-Identifier: BSD-3-Clause */
/* Bounded Xilinx AXI IIC access for the OCP Time Card. */

#include "timecard.h"

#define XIIC_DGIER_OFFSET 0x01cu
#define XIIC_IISR_OFFSET  0x020u
#define XIIC_IIER_OFFSET  0x028u
#define XIIC_RESETR_OFFSET 0x040u

#define XIIC_CR_OFFSET  0x100u
#define XIIC_SR_OFFSET  0x104u
#define XIIC_DTR_OFFSET 0x108u
#define XIIC_DRR_OFFSET 0x10cu
#define XIIC_TFO_OFFSET 0x114u
#define XIIC_RFO_OFFSET 0x118u
#define XIIC_RFD_OFFSET 0x120u

#define XIIC_RESET_VALUE 0x0000000au

#define XIIC_CR_ENABLE_DEVICE 0x01u
#define XIIC_CR_TX_FIFO_RESET 0x02u

#define XIIC_SR_BUS_BUSY 0x04u
#define XIIC_SR_RX_EMPTY 0x40u
#define XIIC_SR_TX_EMPTY 0x80u

#define XIIC_INTR_ARB_LOST 0x01u
#define XIIC_INTR_TX_ERROR 0x02u
#define XIIC_INTR_TX_EMPTY 0x04u
#define XIIC_INTR_RX_FULL  0x08u
#define XIIC_INTR_BNB      0x10u

#define XIIC_TX_DYN_START 0x0100u
#define XIIC_TX_DYN_STOP  0x0200u

#define XIIC_FIFO_DEPTH 16u
#define TIMECARD_I2C_DEFAULT_TIMEOUT_MS 100u
#define TIMECARD_I2C_MAX_TIMEOUT_MS 250u
#define TIMECARD_I2C_POLL_US 5u

static UCHAR
TimeCardI2cRead8(PDEVICE_CONTEXT context, ULONG offset)
{
    /*
     * Match Linux i2c-xiic's native register widths.  The control, status,
     * FIFO occupancy, and receive-data registers are byte-wide; the dynamic
     * transmit register is 16-bit so its START/STOP flags reach bits 8/9.
     */
    return READ_REGISTER_UCHAR((volatile UCHAR *)(context->I2c + offset));
}

static ULONG
TimeCardI2cRead32(PDEVICE_CONTEXT context, ULONG offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(context->I2c + offset));
}

static VOID
TimeCardI2cWrite8(PDEVICE_CONTEXT context, ULONG offset, UCHAR value)
{
    WRITE_REGISTER_UCHAR((volatile UCHAR *)(context->I2c + offset), value);
}

static VOID
TimeCardI2cWrite16(PDEVICE_CONTEXT context, ULONG offset, USHORT value)
{
    WRITE_REGISTER_USHORT((volatile USHORT *)(context->I2c + offset), value);
}

static VOID
TimeCardI2cWrite32(PDEVICE_CONTEXT context, ULONG offset, ULONG value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(context->I2c + offset), value);
}

static VOID
TimeCardI2cClearInterrupts(PDEVICE_CONTEXT context)
{
    ULONG pending = TimeCardI2cRead32(context, XIIC_IISR_OFFSET);

    if (pending != 0)
        TimeCardI2cWrite32(context, XIIC_IISR_OFFSET, pending);
}

static VOID
TimeCardI2cClearInterruptMask(PDEVICE_CONTEXT context, ULONG mask)
{
    ULONG pending = TimeCardI2cRead32(context, XIIC_IISR_OFFSET) & mask;

    if (pending != 0)
        TimeCardI2cWrite32(context, XIIC_IISR_OFFSET, pending);
}

static VOID
TimeCardI2cArmPolling(PDEVICE_CONTEXT context)
{
    TimeCardI2cClearInterrupts(context);
    TimeCardI2cWrite32(context, XIIC_IIER_OFFSET,
                      XIIC_INTR_ARB_LOST | XIIC_INTR_TX_ERROR |
                      XIIC_INTR_TX_EMPTY | XIIC_INTR_RX_FULL |
                      XIIC_INTR_BNB);
}

static NTSTATUS
TimeCardI2cReset(PDEVICE_CONTEXT context)
{
    ULONG i;

    TimeCardI2cWrite32(context, XIIC_DGIER_OFFSET, 0);
    TimeCardI2cWrite32(context, XIIC_IIER_OFFSET, 0);
    TimeCardI2cWrite32(context, XIIC_RESETR_OFFSET, XIIC_RESET_VALUE);
    TimeCardI2cWrite8(context, XIIC_RFD_OFFSET,
                      (UCHAR)(XIIC_FIFO_DEPTH - 1u));
    TimeCardI2cWrite8(context, XIIC_CR_OFFSET, XIIC_CR_TX_FIFO_RESET);
    TimeCardI2cWrite8(context, XIIC_CR_OFFSET, XIIC_CR_ENABLE_DEVICE);

    for (i = 0; i < TIMECARD_I2C_MAX_TRANSFER; ++i) {
        if ((TimeCardI2cRead8(context, XIIC_SR_OFFSET) &
             XIIC_SR_RX_EMPTY) != 0) {
            TimeCardI2cClearInterrupts(context);
            return STATUS_SUCCESS;
        }
        (VOID)TimeCardI2cRead8(context, XIIC_DRR_OFFSET);
    }
    return STATUS_IO_DEVICE_ERROR;
}

static NTSTATUS
TimeCardI2cWaitNotBusy(PDEVICE_CONTEXT context)
{
    ULONG i;

    for (i = 0; i < 600u; ++i) {
        if ((TimeCardI2cRead8(context, XIIC_SR_OFFSET) &
             XIIC_SR_BUS_BUSY) == 0)
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(TIMECARD_I2C_POLL_US);
    }
    return STATUS_DEVICE_BUSY;
}

static VOID
TimeCardI2cQueueDynamicStart(PDEVICE_CONTEXT context, USHORT addressWord)
{
    /*
     * Queue the complete dynamic command before clearing BNB.  This matches
     * Linux i2c-xiic: some AXI IIC revisions do not launch START from an
     * address-only FIFO and instead wait for data or a dynamic receive length.
     * BNB is level-derived while idle, so the caller clears it only after the
     * complete command has been queued.
     */
    TimeCardI2cClearInterruptMask(
        context, XIIC_INTR_ARB_LOST | XIIC_INTR_TX_ERROR |
                 XIIC_INTR_TX_EMPTY | XIIC_INTR_RX_FULL);
    TimeCardI2cWrite16(
        context, XIIC_DTR_OFFSET,
        (USHORT)(XIIC_TX_DYN_START | addressWord));
}

static VOID
TimeCardI2cCaptureStartTrace(PDEVICE_CONTEXT context)
{
    context->I2cLastStartTrace =
        (ULONG)TimeCardI2cRead8(context, XIIC_CR_OFFSET) |
        ((ULONG)TimeCardI2cRead8(context, XIIC_SR_OFFSET) << 8);
    context->I2cLastStartEvents =
        (ULONG)TimeCardI2cRead8(context, XIIC_TFO_OFFSET) << 16;
}

static VOID
TimeCardI2cFinishStartTrace(PDEVICE_CONTEXT context, ULONG observedInterrupts)
{
    context->I2cLastStartTrace |=
        ((ULONG)TimeCardI2cRead8(context, XIIC_CR_OFFSET) << 16) |
        ((ULONG)TimeCardI2cRead8(context, XIIC_SR_OFFSET) << 24);
    context->I2cLastStartEvents |=
        (observedInterrupts & 0xffffu) |
        ((ULONG)TimeCardI2cRead8(context, XIIC_TFO_OFFSET) << 24);
}

static NTSTATUS
TimeCardI2cPrepareTransfer(PDEVICE_CONTEXT context)
{
    NTSTATUS status;

    status = TimeCardI2cWaitNotBusy(context);
    if (!NT_SUCCESS(status)) {
        status = TimeCardI2cReset(context);
        if (!NT_SUCCESS(status))
            return status;
        status = TimeCardI2cWaitNotBusy(context);
        if (!NT_SUCCESS(status))
            return status;
    }
    return TimeCardI2cReset(context);
}

static BOOLEAN
TimeCardI2cAddressValid(ULONG address)
{
    return address >= 0x08u && address <= 0x77u;
}

static ULONG
TimeCardI2cKnownDeviceFlag(ULONG address)
{
    if (address == TIMECARD_BOARD_EEPROM_ADDRESS)
        return TIMECARD_I2C_DEVICE_BOARD_EEPROM;
    if (address == TIMECARD_IDENTITY_ADDRESS)
        return TIMECARD_I2C_DEVICE_MAC_EEPROM;
    return 0;
}

static VOID
TimeCardI2cSetReceiveWatermark(PDEVICE_CONTEXT context, ULONG remaining)
{
    ULONG bytes = remaining < XIIC_FIFO_DEPTH ? remaining : XIIC_FIFO_DEPTH;

    if (bytes != 0)
        TimeCardI2cWrite8(context, XIIC_RFD_OFFSET, (UCHAR)(bytes - 1u));
}

static NTSTATUS
TimeCardI2cWaitTransmitPhase(PDEVICE_CONTEXT context, ULONG *remainingPolls,
                             ULONG *observedInterrupts,
                             UCHAR *controllerStatus)
{
    while (*remainingPolls != 0) {
        ULONG interrupts = TimeCardI2cRead32(context, XIIC_IISR_OFFSET);

        --(*remainingPolls);
        *observedInterrupts |= interrupts;
        *controllerStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);
        if ((interrupts & XIIC_INTR_ARB_LOST) != 0)
            return STATUS_IO_DEVICE_ERROR;
        if ((interrupts & XIIC_INTR_TX_ERROR) != 0)
            return STATUS_NO_SUCH_DEVICE;
        if ((interrupts & XIIC_INTR_BNB) != 0)
            return STATUS_IO_DEVICE_ERROR;
        if ((interrupts & XIIC_INTR_TX_EMPTY) != 0) {
            TimeCardI2cClearInterruptMask(context, XIIC_INTR_TX_EMPTY);
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(TIMECARD_I2C_POLL_US);
    }
    return STATUS_IO_TIMEOUT;
}

static VOID
TimeCardI2cDrainReceiveFifo(PDEVICE_CONTEXT context,
                            const TIMECARD_I2C_READ_REQUEST *request,
                            TIMECARD_I2C_TRANSFER *transfer,
                            ULONG *copied)
{
    ULONG available;
    ULONG remaining = request->Length - *copied;
    ULONG i;

    if ((TimeCardI2cRead8(context, XIIC_SR_OFFSET) &
         XIIC_SR_RX_EMPTY) != 0) {
        return;
    }

    available = (ULONG)TimeCardI2cRead8(context, XIIC_RFO_OFFSET) + 1u;
    if (available > remaining)
        available = remaining;
    for (i = 0; i < available; ++i)
        transfer->Data[(*copied)++] =
            TimeCardI2cRead8(context, XIIC_DRR_OFFSET);

    remaining = request->Length - *copied;
    if (remaining != 0)
        TimeCardI2cSetReceiveWatermark(context, remaining);
}

static NTSTATUS
TimeCardI2cReadAttempt(PDEVICE_CONTEXT context,
                       const TIMECARD_I2C_READ_REQUEST *request,
                       TIMECARD_I2C_TRANSFER *transfer, ULONG pollCount)
{
    ULONG copied = 0;
    ULONG remainingPolls = pollCount;
    ULONG observedInterrupts = 0;
    ULONG interrupts = 0;
    UCHAR controllerStatus = 0;
    NTSTATUS status;

    context->I2cLastStartTrace = 0;
    context->I2cLastStartEvents = 0;
    status = TimeCardI2cPrepareTransfer(context);
    if (!NT_SUCCESS(status))
        goto Exit;

    TimeCardI2cArmPolling(context);

    /*
     * Keep the subaddress write as a separate dynamic-mode message.  Waiting
     * for TX empty here is important: it lets an address/data NACK surface
     * before the repeated START for the receive message is queued.
    */
    if (request->SubaddressLength != 0) {
        TimeCardI2cQueueDynamicStart(
            context, (USHORT)(request->Address << 1));
        if (request->SubaddressLength == 2u) {
            TimeCardI2cWrite16(
                context, XIIC_DTR_OFFSET,
                (USHORT)((request->Subaddress >> 8) & 0xffu));
        }
        TimeCardI2cWrite16(context, XIIC_DTR_OFFSET,
                           (USHORT)(request->Subaddress & 0xffu));
        TimeCardI2cCaptureStartTrace(context);
        TimeCardI2cClearInterruptMask(
            context, XIIC_INTR_TX_EMPTY | XIIC_INTR_TX_ERROR |
                     XIIC_INTR_BNB);

        status = TimeCardI2cWaitTransmitPhase(
            context, &remainingPolls, &observedInterrupts,
            &controllerStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
    }

    /*
     * AXI IIC reports the master's expected final receive NACK through the
     * same TX_ERROR bit used for a slave address NACK.  Program a zero-based
     * RX watermark, drain whenever data is available, then clear RX_FULL and
     * the accompanying TX_ERROR together.  This mirrors the controller's
     * dynamic receive state machine and removes the completion race.
    */
    TimeCardI2cSetReceiveWatermark(context, request->Length);
    TimeCardI2cQueueDynamicStart(
        context, (USHORT)((request->Address << 1) | 1u));
    TimeCardI2cWrite16(
        context, XIIC_DTR_OFFSET,
        (USHORT)(XIIC_TX_DYN_STOP | request->Length));
    TimeCardI2cCaptureStartTrace(context);
    TimeCardI2cClearInterruptMask(context, XIIC_INTR_BNB);

    status = STATUS_IO_TIMEOUT;
    while (remainingPolls != 0) {
        --remainingPolls;
        interrupts = TimeCardI2cRead32(context, XIIC_IISR_OFFSET);
        observedInterrupts |= interrupts;
        controllerStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);

        if ((interrupts & XIIC_INTR_ARB_LOST) != 0) {
            status = STATUS_IO_DEVICE_ERROR;
            break;
        }

        if ((controllerStatus & XIIC_SR_RX_EMPTY) == 0) {
            TimeCardI2cDrainReceiveFifo(context, request, transfer,
                                        &copied);
            if ((interrupts & XIIC_INTR_RX_FULL) != 0) {
                TimeCardI2cClearInterruptMask(
                    context, XIIC_INTR_RX_FULL | XIIC_INTR_TX_ERROR);
            }
            controllerStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);
        }

        if (copied == request->Length) {
            TimeCardI2cClearInterruptMask(context, XIIC_INTR_TX_ERROR);
            if ((interrupts & XIIC_INTR_BNB) != 0 ||
                (controllerStatus & XIIC_SR_BUS_BUSY) == 0) {
                status = STATUS_SUCCESS;
                break;
            }
            KeStallExecutionProcessor(TIMECARD_I2C_POLL_US);
            continue;
        }

        if ((interrupts & XIIC_INTR_TX_ERROR) != 0 &&
            (interrupts & XIIC_INTR_RX_FULL) == 0) {
            status = copied == 0 ? STATUS_NO_SUCH_DEVICE :
                                   STATUS_IO_DEVICE_ERROR;
            break;
        }

        if ((interrupts & XIIC_INTR_BNB) != 0 &&
            copied < request->Length) {
            status = STATUS_IO_DEVICE_ERROR;
            break;
        }
        KeStallExecutionProcessor(TIMECARD_I2C_POLL_US);
    }

Exit:
    TimeCardI2cFinishStartTrace(context, observedInterrupts);
    transfer->Length = copied;
    transfer->ControllerStatus = controllerStatus;
    transfer->InterruptStatus = observedInterrupts;
    (VOID)TimeCardI2cReset(context);
    return status;
}

static NTSTATUS
TimeCardI2cReadLocked(PDEVICE_CONTEXT context,
                      const TIMECARD_I2C_READ_REQUEST *request,
                      TIMECARD_I2C_TRANSFER *transfer)
{
    ULONG timeoutMilliseconds = request->TimeoutMilliseconds;
    ULONG pollCount;
    ULONG attempt;
    NTSTATUS status = STATUS_IO_TIMEOUT;

    if (timeoutMilliseconds == 0)
        timeoutMilliseconds = TIMECARD_I2C_DEFAULT_TIMEOUT_MS;
    if (timeoutMilliseconds > TIMECARD_I2C_MAX_TIMEOUT_MS)
        timeoutMilliseconds = TIMECARD_I2C_MAX_TIMEOUT_MS;
    pollCount = timeoutMilliseconds * 1000u / TIMECARD_I2C_POLL_US;

    RtlZeroMemory(transfer, sizeof(*transfer));
    transfer->Size = (ULONG)FIELD_OFFSET(TIMECARD_I2C_TRANSFER, Data) +
                     request->Length;
    transfer->Address = request->Address;

    for (attempt = 0; attempt < 2u; ++attempt) {
        transfer->Length = 0;
        transfer->ControllerStatus = 0;
        transfer->InterruptStatus = 0;
        status = TimeCardI2cReadAttempt(context, request, transfer,
                                        pollCount);
        if (NT_SUCCESS(status) || status == STATUS_NO_SUCH_DEVICE)
            break;
    }
    return status;
}

static NTSTATUS
TimeCardI2cWriteAttempt(PDEVICE_CONTEXT context, ULONG address,
                        const UCHAR *data, ULONG length, ULONG pollCount,
                        ULONG *controllerStatus, ULONG *interruptStatus)
{
    ULONG remainingPolls = pollCount;
    ULONG observedInterrupts = 0;
    UCHAR currentStatus = 0;
    ULONG i;
    NTSTATUS status;

    context->I2cLastStartTrace = 0;
    context->I2cLastStartEvents = 0;
    status = TimeCardI2cPrepareTransfer(context);
    if (!NT_SUCCESS(status))
        goto Exit;

    TimeCardI2cArmPolling(context);
    TimeCardI2cQueueDynamicStart(context, (USHORT)(address << 1));
    for (i = 0; i < length; ++i) {
        USHORT value = data[i];
        if (i + 1u == length)
            value |= XIIC_TX_DYN_STOP;
        TimeCardI2cWrite16(context, XIIC_DTR_OFFSET, value);
    }
    TimeCardI2cCaptureStartTrace(context);
    TimeCardI2cClearInterruptMask(
        context, XIIC_INTR_TX_EMPTY | XIIC_INTR_TX_ERROR | XIIC_INTR_BNB);

    status = STATUS_IO_TIMEOUT;
    while (remainingPolls != 0) {
        ULONG interrupts;

        --remainingPolls;
        interrupts = TimeCardI2cRead32(context, XIIC_IISR_OFFSET);
        observedInterrupts |= interrupts;
        currentStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);
        if ((interrupts & XIIC_INTR_ARB_LOST) != 0) {
            status = STATUS_IO_DEVICE_ERROR;
            break;
        }
        if ((interrupts & XIIC_INTR_TX_ERROR) != 0) {
            status = STATUS_NO_SUCH_DEVICE;
            break;
        }
        if ((interrupts & XIIC_INTR_BNB) != 0) {
            status = STATUS_SUCCESS;
            break;
        }
        KeStallExecutionProcessor(TIMECARD_I2C_POLL_US);
    }

Exit:
    TimeCardI2cFinishStartTrace(context, observedInterrupts);
    *controllerStatus = currentStatus;
    *interruptStatus = observedInterrupts;
    (VOID)TimeCardI2cReset(context);
    return status;
}

static NTSTATUS
TimeCardI2cWriteLocked(PDEVICE_CONTEXT context, ULONG address,
                       const UCHAR *data, ULONG length,
                       ULONG *controllerStatus, ULONG *interruptStatus)
{
    ULONG pollCount = TIMECARD_I2C_DEFAULT_TIMEOUT_MS * 1000u /
                      TIMECARD_I2C_POLL_US;
    ULONG attempt;
    NTSTATUS status = STATUS_IO_TIMEOUT;

    if (!TimeCardI2cAddressValid(address) || data == NULL ||
        length == 0 || length >= XIIC_FIFO_DEPTH) {
        return STATUS_INVALID_PARAMETER;
    }

    *controllerStatus = 0;
    *interruptStatus = 0;
    for (attempt = 0; attempt < 2u; ++attempt) {
        status = TimeCardI2cWriteAttempt(
            context, address, data, length, pollCount,
            controllerStatus, interruptStatus);
        if (NT_SUCCESS(status) || status == STATUS_NO_SUCH_DEVICE)
            break;
    }
    return status;
}

static NTSTATUS
TimeCardI2cReadBytesLocked(PDEVICE_CONTEXT context, ULONG address,
                           ULONG subaddressLength, ULONG subaddress,
                           UCHAR *data, ULONG length,
                           ULONG *controllerStatus, ULONG *interruptStatus)
{
    TIMECARD_I2C_READ_REQUEST request;
    TIMECARD_I2C_TRANSFER transfer;
    NTSTATUS status;

    RtlZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.Address = address;
    request.SubaddressLength = subaddressLength;
    request.Subaddress = subaddress;
    request.Length = length;
    request.TimeoutMilliseconds = TIMECARD_I2C_DEFAULT_TIMEOUT_MS;
    status = TimeCardI2cReadLocked(context, &request, &transfer);
    *controllerStatus = transfer.ControllerStatus;
    *interruptStatus = transfer.InterruptStatus;
    if (NT_SUCCESS(status))
        RtlCopyMemory(data, transfer.Data, length);
    return status;
}

static NTSTATUS
TimeCardI2cMuxReadLocked(PDEVICE_CONTEXT context, UCHAR *channelMask,
                         ULONG *controllerStatus, ULONG *interruptStatus)
{
    UCHAR value = 0;
    NTSTATUS status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_I2C_MUX_ADDRESS, 0, 0, &value, 1,
        controllerStatus, interruptStatus);

    if (NT_SUCCESS(status))
        *channelMask = value & TIMECARD_I2C_MUX_CHANNEL_MASK;
    return status;
}

static NTSTATUS
TimeCardI2cMuxWriteLocked(PDEVICE_CONTEXT context, UCHAR channelMask,
                          ULONG *controllerStatus, ULONG *interruptStatus)
{
    UCHAR value = channelMask & TIMECARD_I2C_MUX_CHANNEL_MASK;

    return TimeCardI2cWriteLocked(
        context, TIMECARD_I2C_MUX_ADDRESS, &value, 1,
        controllerStatus, interruptStatus);
}

NTSTATUS
TimeCardI2cGetStatus(PDEVICE_CONTEXT context, TIMECARD_I2C_STATUS *status)
{
    UCHAR control;
    UCHAR controllerStatus;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(status, sizeof(*status));
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    control = TimeCardI2cRead8(context, XIIC_CR_OFFSET);
    controllerStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);
    status->Size = (ULONG)sizeof(*status);
    status->Flags = TIMECARD_I2C_FLAG_PRESENT;
    if ((control & XIIC_CR_ENABLE_DEVICE) != 0)
        status->Flags |= TIMECARD_I2C_FLAG_ENABLED;
    if ((controllerStatus & XIIC_SR_BUS_BUSY) != 0)
        status->Flags |= TIMECARD_I2C_FLAG_BUS_BUSY;
    if ((controllerStatus & XIIC_SR_RX_EMPTY) != 0)
        status->Flags |= TIMECARD_I2C_FLAG_RX_EMPTY;
    if ((controllerStatus & XIIC_SR_TX_EMPTY) != 0)
        status->Flags |= TIMECARD_I2C_FLAG_TX_EMPTY;
    status->Offset = context->I2cOffset;
    status->Control = control;
    status->Status = controllerStatus;
    status->InterruptStatus =
        TimeCardI2cRead32(context, XIIC_IISR_OFFSET);
    status->InterruptEnable =
        TimeCardI2cRead32(context, XIIC_IIER_OFFSET);
    status->TxFifoOccupancy = TimeCardI2cRead8(context, XIIC_TFO_OFFSET);
    status->RxFifoOccupancy = TimeCardI2cRead8(context, XIIC_RFO_OFFSET);
    status->KnownDeviceMask = context->I2cKnownDeviceMask;
    status->Reserved[0] = context->I2cLastStartTrace;
    status->Reserved[1] = context->I2cLastStartEvents;
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardI2cProbe(PDEVICE_CONTEXT context, ULONG address,
                 TIMECARD_I2C_PROBE *probe)
{
    ULONG remainingPolls = TIMECARD_I2C_DEFAULT_TIMEOUT_MS * 1000u /
                           TIMECARD_I2C_POLL_US;
    ULONG interruptStatus = 0;
    ULONG flag;
    UCHAR controllerStatus = 0;
    NTSTATUS status;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (!TimeCardI2cAddressValid(address))
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(probe, sizeof(*probe));
    probe->Size = (ULONG)sizeof(*probe);
    probe->Address = address;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    context->I2cLastStartTrace = 0;
    context->I2cLastStartEvents = 0;
    status = TimeCardI2cPrepareTransfer(context);
    if (!NT_SUCCESS(status))
        goto Exit;

    TimeCardI2cArmPolling(context);
    TimeCardI2cQueueDynamicStart(
        context, (USHORT)(XIIC_TX_DYN_STOP | (address << 1)));
    TimeCardI2cCaptureStartTrace(context);
    TimeCardI2cClearInterruptMask(
        context, XIIC_INTR_TX_EMPTY | XIIC_INTR_TX_ERROR | XIIC_INTR_BNB);

    status = STATUS_IO_TIMEOUT;
    while (remainingPolls != 0) {
        ULONG interrupts = TimeCardI2cRead32(context, XIIC_IISR_OFFSET);

        --remainingPolls;
        interruptStatus |= interrupts;
        controllerStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);
        if ((interrupts & XIIC_INTR_ARB_LOST) != 0) {
            status = STATUS_IO_DEVICE_ERROR;
            break;
        }
        if ((interrupts & XIIC_INTR_TX_ERROR) != 0) {
            probe->Present = 0;
            status = STATUS_SUCCESS;
            break;
        }
        if ((interrupts & XIIC_INTR_BNB) != 0) {
            probe->Present = 1;
            status = STATUS_SUCCESS;
            break;
        }
        KeStallExecutionProcessor(TIMECARD_I2C_POLL_US);
    }

    TimeCardI2cFinishStartTrace(context, interruptStatus);
    probe->ControllerStatus = controllerStatus;
    probe->InterruptStatus = interruptStatus;
    if (NT_SUCCESS(status)) {
        flag = TimeCardI2cKnownDeviceFlag(address);
        if (probe->Present != 0)
            context->I2cKnownDeviceMask |= flag;
        else
            context->I2cKnownDeviceMask &= ~flag;
    }
    (VOID)TimeCardI2cReset(context);

Exit:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardI2cRead(PDEVICE_CONTEXT context,
                const TIMECARD_I2C_READ_REQUEST *request,
                TIMECARD_I2C_TRANSFER *transfer)
{
    NTSTATUS status;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        !TimeCardI2cAddressValid(request->Address) ||
        request->SubaddressLength > 2u || request->Length == 0 ||
        request->Length > TIMECARD_I2C_MAX_TRANSFER) {
        return STATUS_INVALID_PARAMETER;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardI2cReadLocked(context, request, transfer);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardI2cMuxQuery(PDEVICE_CONTEXT context,
                    TIMECARD_I2C_MUX_CONTROL *control)
{
    UCHAR channelMask = 0;
    ULONG controllerStatus = 0;
    ULONG interruptStatus = 0;
    NTSTATUS status;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardI2cMuxReadLocked(
        context, &channelMask, &controllerStatus, &interruptStatus);
    control->ControllerStatus = controllerStatus;
    control->InterruptStatus = interruptStatus;
    if (NT_SUCCESS(status)) {
        control->Present = 1;
        control->ChannelMask = channelMask;
    } else if (status == STATUS_NO_SUCH_DEVICE) {
        /* Absence is a query result, not a transport failure. */
        status = STATUS_SUCCESS;
    }
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardI2cMuxSet(PDEVICE_CONTEXT context,
                  const TIMECARD_I2C_MUX_CONTROL *request,
                  TIMECARD_I2C_MUX_CONTROL *response)
{
    UCHAR channelMask = 0;
    ULONG controllerStatus = 0;
    ULONG interruptStatus = 0;
    NTSTATUS status;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        (request->ChannelMask & ~TIMECARD_I2C_MUX_CHANNEL_MASK) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardI2cMuxWriteLocked(
        context, (UCHAR)request->ChannelMask,
        &controllerStatus, &interruptStatus);
    if (NT_SUCCESS(status)) {
        status = TimeCardI2cMuxReadLocked(
            context, &channelMask, &controllerStatus, &interruptStatus);
    }
    response->ControllerStatus = controllerStatus;
    response->InterruptStatus = interruptStatus;
    if (NT_SUCCESS(status)) {
        response->Present = 1;
        response->ChannelMask = channelMask;
        if (channelMask != (UCHAR)request->ChannelMask)
            status = STATUS_DEVICE_DATA_ERROR;
    }
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

static NTSTATUS
TimeCardSensorBranchSelectLocked(PDEVICE_CONTEXT context, UCHAR *savedMask,
                                 BOOLEAN *changed, ULONG *controllerStatus,
                                 ULONG *interruptStatus)
{
    NTSTATUS status = TimeCardI2cMuxReadLocked(
        context, savedMask, controllerStatus, interruptStatus);

    *changed = FALSE;
    if (!NT_SUCCESS(status))
        return status;
    if (*savedMask == TIMECARD_I2C_MUX_CHANNEL_SENSORS)
        return STATUS_SUCCESS;

    status = TimeCardI2cMuxWriteLocked(
        context, TIMECARD_I2C_MUX_CHANNEL_SENSORS,
        controllerStatus, interruptStatus);
    if (NT_SUCCESS(status))
        *changed = TRUE;
    return status;
}

static NTSTATUS
TimeCardSensorBranchRestoreLocked(PDEVICE_CONTEXT context, UCHAR savedMask,
                                  BOOLEAN changed, NTSTATUS operationStatus,
                                  ULONG *controllerStatus,
                                  ULONG *interruptStatus)
{
    NTSTATUS restoreStatus;

    if (!changed)
        return operationStatus;
    restoreStatus = TimeCardI2cMuxWriteLocked(
        context, savedMask, controllerStatus, interruptStatus);
    return NT_SUCCESS(operationStatus) ? restoreStatus : operationStatus;
}

static ULONG
TimeCardLedFaultMask(const UCHAR *data)
{
    return ((ULONG)data[0] | ((ULONG)data[1] << 8) |
            ((ULONG)(data[2] & 0x03u) << 16)) &
           TIMECARD_LED_OUTPUT_MASK;
}

static NTSTATUS
TimeCardLedDetectFaultsLocked(PDEVICE_CONTEXT context, ULONG *openMask,
                              ULONG *shortMask, ULONG *controllerStatus,
                              ULONG *interruptStatus)
{
    UCHAR writeBuffer[2];
    UCHAR result[3];
    NTSTATUS disableStatus;
    NTSTATUS status;

    *openMask = 0;
    *shortMask = 0;

    /* OSDE=11 selects open detection; results appear immediately. */
    writeBuffer[0] = 0x71u;
    writeBuffer[1] = 0x03u;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_LED_ADDRESS, writeBuffer, sizeof(writeBuffer),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        goto Disable;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_LED_ADDRESS, 1, 0x72u, result, sizeof(result),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        goto Disable;
    *openMask = TimeCardLedFaultMask(result);

    /* OSDE=10 selects short detection and replaces the result registers. */
    writeBuffer[1] = 0x02u;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_LED_ADDRESS, writeBuffer, sizeof(writeBuffer),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        goto Disable;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_LED_ADDRESS, 1, 0x72u, result, sizeof(result),
        controllerStatus, interruptStatus);
    if (NT_SUCCESS(status))
        *shortMask = TimeCardLedFaultMask(result);

Disable:
    writeBuffer[1] = 0x00u;
    disableStatus = TimeCardI2cWriteLocked(
        context, TIMECARD_LED_ADDRESS, writeBuffer, sizeof(writeBuffer),
        controllerStatus, interruptStatus);
    return NT_SUCCESS(status) ? disableStatus : status;
}

NTSTATUS
TimeCardLedQuery(PDEVICE_CONTEXT context, ULONG led,
                 TIMECARD_LED_CONTROL *control)
{
    UCHAR savedMask = 0;
    BOOLEAN changed = FALSE;
    UCHAR deviceControl = 0;
    UCHAR globalCurrent = 0;
    UCHAR spreadSpectrum = 0;
    UCHAR pwm[6];
    ULONG controllerStatus = 0;
    ULONG interruptStatus = 0;
    ULONG openMask = 0;
    ULONG shortMask = 0;
    NTSTATUS status;
    NTSTATUS faultStatus;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (led >= TIMECARD_LED_COUNT)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Led = led;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardSensorBranchSelectLocked(
        context, &savedMask, &changed,
        &controllerStatus, &interruptStatus);
    control->MuxChannelMask = savedMask;
    if (!NT_SUCCESS(status))
        goto Exit;

    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_LED_ADDRESS, 1, 0x00u, &deviceControl, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_LED_ADDRESS, 1, 0x6eu, &globalCurrent, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_LED_ADDRESS, 1, 0x78u, &spreadSpectrum, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_LED_ADDRESS, 1, 0x01u + led * 6u, pwm,
        sizeof(pwm),
        &controllerStatus, &interruptStatus);
    if (NT_SUCCESS(status)) {
        control->Flags = TIMECARD_LED_FLAG_PRESENT;
        if ((deviceControl & 0x01u) != 0)
            control->Flags |= TIMECARD_LED_FLAG_ENABLED;
        if ((spreadSpectrum & 0x60u) == 0x60u)
            control->Flags |= TIMECARD_LED_FLAG_DC_TEST;
        control->Red = pwm[0];
        control->Green = pwm[2];
        control->Blue = pwm[4];
        control->GlobalCurrent = globalCurrent;
        faultStatus = TimeCardLedDetectFaultsLocked(
            context, &openMask, &shortMask,
            &controllerStatus, &interruptStatus);
        if (NT_SUCCESS(faultStatus)) {
            control->Flags |= TIMECARD_LED_FLAG_FAULT_VALID;
            control->OpenOutputMask = openMask;
            control->ShortOutputMask = shortMask;
        }
    }

Exit:
    status = TimeCardSensorBranchRestoreLocked(
        context, savedMask, changed, status,
        &controllerStatus, &interruptStatus);
    control->ControllerStatus = controllerStatus;
    control->InterruptStatus = interruptStatus;
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardLedSet(PDEVICE_CONTEXT context,
               const TIMECARD_LED_CONTROL *request,
               TIMECARD_LED_CONTROL *response)
{
    UCHAR savedMask = 0;
    BOOLEAN changed = FALSE;
    UCHAR writeBuffer[7];
    UCHAR resetProbe = 0;
    BOOLEAN sdbHigh = FALSE;
    ULONG controllerStatus = 0;
    ULONG interruptStatus = 0;
    ULONG openMask = 0;
    ULONG shortMask = 0;
    NTSTATUS status;
    NTSTATUS faultStatus;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        request->Led >= TIMECARD_LED_COUNT ||
        request->Red > 0xffu || request->Green > 0xffu ||
        request->Blue > 0xffu || request->GlobalCurrent == 0 ||
        request->GlobalCurrent > TIMECARD_LED_MAX_GLOBAL_CURRENT ||
        (request->Flags & ~(TIMECARD_LED_FLAG_DC_TEST |
                            TIMECARD_LED_FLAG_RESET_TEST)) != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Led = request->Led;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardSensorBranchSelectLocked(
        context, &savedMask, &changed,
        &controllerStatus, &interruptStatus);
    response->MuxChannelMask = savedMask;
    if (!NT_SUCCESS(status))
        goto Exit;

    if ((request->Flags & TIMECARD_LED_FLAG_RESET_TEST) != 0) {
        /*
         * The reset command is honored only while SDB is physically high
         * and SSD is set.  Use a benign non-default phase value as a probe,
         * then restore normal operation before applying the requested LED.
         */
        writeBuffer[0] = 0x00u;
        writeBuffer[1] = 0x01u;
        status = TimeCardI2cWriteLocked(
            context, TIMECARD_LED_ADDRESS, writeBuffer, 2,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        writeBuffer[0] = 0x70u;
        writeBuffer[1] = 0x05u;
        status = TimeCardI2cWriteLocked(
            context, TIMECARD_LED_ADDRESS, writeBuffer, 2,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        status = TimeCardI2cReadBytesLocked(
            context, TIMECARD_LED_ADDRESS, 1, 0x70u, &resetProbe, 1,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status) || resetProbe != 0x05u) {
            if (NT_SUCCESS(status))
                status = STATUS_DEVICE_DATA_ERROR;
            goto Exit;
        }
        writeBuffer[0] = 0x7fu;
        writeBuffer[1] = 0x00u;
        status = TimeCardI2cWriteLocked(
            context, TIMECARD_LED_ADDRESS, writeBuffer, 2,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        status = TimeCardI2cReadBytesLocked(
            context, TIMECARD_LED_ADDRESS, 1, 0x70u, &resetProbe, 1,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        sdbHigh = resetProbe == 0x00u;

    }

    /* 8-bit PWM mode, normal operation.  This yields flicker-free PWM. */
    writeBuffer[0] = 0x00u;
    writeBuffer[1] = 0x01u;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_LED_ADDRESS, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    writeBuffer[0] = 0x6eu;
    writeBuffer[1] = (UCHAR)request->GlobalCurrent;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_LED_ADDRESS, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    /* Apply full per-channel scaling; global current remains safety-capped. */
    writeBuffer[0] = (UCHAR)(0x4au + request->Led * 3u);
    writeBuffer[1] = 0xffu;
    writeBuffer[2] = 0xffu;
    writeBuffer[3] = 0xffu;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_LED_ADDRESS, writeBuffer, 4,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    writeBuffer[0] = (UCHAR)(0x01u + request->Led * 6u);
    writeBuffer[1] = (UCHAR)request->Red;
    writeBuffer[2] = 0;
    writeBuffer[3] = (UCHAR)request->Green;
    writeBuffer[4] = 0;
    writeBuffer[5] = (UCHAR)request->Blue;
    writeBuffer[6] = 0;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_LED_ADDRESS, writeBuffer, sizeof(writeBuffer),
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    writeBuffer[0] = 0x49u;
    writeBuffer[1] = 0;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_LED_ADDRESS, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    /*
     * DCPWM is a guarded electrical diagnostic.  0x60 forces OUT1-18 on
     * independently of the PWM/update path; normal requests explicitly
     * return the device to PWM mode.
     */
    writeBuffer[0] = 0x78u;
    writeBuffer[1] =
        (request->Flags & TIMECARD_LED_FLAG_DC_TEST) != 0 ? 0x60u : 0x00u;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_LED_ADDRESS, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (NT_SUCCESS(status)) {
        response->Flags = TIMECARD_LED_FLAG_PRESENT |
                          TIMECARD_LED_FLAG_ENABLED;
        if ((request->Flags & TIMECARD_LED_FLAG_DC_TEST) != 0)
            response->Flags |= TIMECARD_LED_FLAG_DC_TEST;
        if ((request->Flags & TIMECARD_LED_FLAG_RESET_TEST) != 0)
            response->Flags |= TIMECARD_LED_FLAG_RESET_TEST;
        if (sdbHigh)
            response->Flags |= TIMECARD_LED_FLAG_SDB_HIGH;
        response->Red = request->Red;
        response->Green = request->Green;
        response->Blue = request->Blue;
        response->GlobalCurrent = request->GlobalCurrent;
        faultStatus = TimeCardLedDetectFaultsLocked(
            context, &openMask, &shortMask,
            &controllerStatus, &interruptStatus);
        if (NT_SUCCESS(faultStatus)) {
            response->Flags |= TIMECARD_LED_FLAG_FAULT_VALID;
            response->OpenOutputMask = openMask;
            response->ShortOutputMask = shortMask;
        }
    }

Exit:
    status = TimeCardSensorBranchRestoreLocked(
        context, savedMask, changed, status,
        &controllerStatus, &interruptStatus);
    response->ControllerStatus = controllerStatus;
    response->InterruptStatus = interruptStatus;
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

static VOID
TimeCardSensorDelayMilliseconds(ULONG milliseconds)
{
    LARGE_INTEGER interval;

    interval.QuadPart = -((LONGLONG)milliseconds * 10000ll);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

static USHORT
TimeCardSensorReadLe16(const UCHAR *data)
{
    return (USHORT)((USHORT)data[0] | ((USHORT)data[1] << 8));
}

static LONG
TimeCardSensorReadLeSigned16(const UCHAR *data)
{
    return (LONG)(SHORT)TimeCardSensorReadLe16(data);
}

static USHORT
TimeCardSensorReadBe16(const UCHAR *data)
{
    return (USHORT)(((USHORT)data[0] << 8) | (USHORT)data[1]);
}

static LONG
TimeCardSensorReadBeSigned16(const UCHAR *data)
{
    return (LONG)(SHORT)TimeCardSensorReadBe16(data);
}

static VOID
TimeCardBme280ReadLocked(PDEVICE_CONTEXT context,
                         TIMECARD_BME280_READING *reading,
                         ULONG *controllerStatus, ULONG *interruptStatus)
{
    UCHAR chipId = 0;
    UCHAR statusByte = 0;
    UCHAR calibration1[26];
    UCHAR calibration2[7];
    UCHAR data[8];
    UCHAR writeBuffer[2];
    ULONG index;
    NTSTATUS status;

    reading->Size = sizeof(*reading);
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BME280_ADDRESS, 1, 0xd0u,
        &chipId, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status) || chipId != 0x60u)
        return;
    reading->ChipId = chipId;
    reading->Flags = TIMECARD_SENSOR_FLAG_PRESENT;

    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BME280_ADDRESS, 1, 0x88u,
        calibration1, sizeof(calibration1),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BME280_ADDRESS, 1, 0xe1u,
        calibration2, sizeof(calibration2),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;

    reading->DigT1 = TimeCardSensorReadLe16(&calibration1[0]);
    reading->DigT2 = TimeCardSensorReadLeSigned16(&calibration1[2]);
    reading->DigT3 = TimeCardSensorReadLeSigned16(&calibration1[4]);
    reading->DigP1 = TimeCardSensorReadLe16(&calibration1[6]);
    reading->DigP2 = TimeCardSensorReadLeSigned16(&calibration1[8]);
    reading->DigP3 = TimeCardSensorReadLeSigned16(&calibration1[10]);
    reading->DigP4 = TimeCardSensorReadLeSigned16(&calibration1[12]);
    reading->DigP5 = TimeCardSensorReadLeSigned16(&calibration1[14]);
    reading->DigP6 = TimeCardSensorReadLeSigned16(&calibration1[16]);
    reading->DigP7 = TimeCardSensorReadLeSigned16(&calibration1[18]);
    reading->DigP8 = TimeCardSensorReadLeSigned16(&calibration1[20]);
    reading->DigP9 = TimeCardSensorReadLeSigned16(&calibration1[22]);
    reading->DigH1 = calibration1[25];
    reading->DigH2 = TimeCardSensorReadLeSigned16(&calibration2[0]);
    reading->DigH3 = calibration2[2];
    reading->DigH4 = (LONG)(CHAR)calibration2[3] * 16L +
                     (LONG)(calibration2[4] & 0x0fu);
    reading->DigH5 = (LONG)(CHAR)calibration2[5] * 16L +
                     (LONG)(calibration2[4] >> 4);
    reading->DigH6 = (LONG)(CHAR)calibration2[6];

    /* Humidity x1, temperature x2, pressure x4, one forced sample. */
    writeBuffer[0] = 0xf2u;
    writeBuffer[1] = 0x01u;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_SENSOR_BME280_ADDRESS, writeBuffer, 2,
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    writeBuffer[0] = 0xf4u;
    writeBuffer[1] = 0x4du;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_SENSOR_BME280_ADDRESS, writeBuffer, 2,
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    reading->Flags |= TIMECARD_SENSOR_FLAG_CONFIGURED;

    for (index = 0; index < 20u; ++index) {
        TimeCardSensorDelayMilliseconds(5u);
        status = TimeCardI2cReadBytesLocked(
            context, TIMECARD_SENSOR_BME280_ADDRESS, 1, 0xf3u,
            &statusByte, 1, controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        if ((statusByte & 0x08u) == 0)
            break;
    }
    reading->Status = statusByte;
    if ((statusByte & 0x08u) == 0)
        reading->Flags |= TIMECARD_SENSOR_FLAG_CONVERSION_READY;

    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BME280_ADDRESS, 1, 0xf7u,
        data, sizeof(data), controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    reading->RawPressure = ((ULONG)data[0] << 12) |
                           ((ULONG)data[1] << 4) |
                           ((ULONG)data[2] >> 4);
    reading->RawTemperature = (LONG)(((ULONG)data[3] << 12) |
                                    ((ULONG)data[4] << 4) |
                                    ((ULONG)data[5] >> 4));
    reading->RawHumidity = ((ULONG)data[6] << 8) | data[7];
    if ((ULONG)reading->RawTemperature != 0x80000u &&
        reading->RawPressure != 0x80000u) {
        reading->Flags |= TIMECARD_SENSOR_FLAG_VALID;
    }
}

static VOID
TimeCardIna219ReadLocked(PDEVICE_CONTEXT context, ULONG address,
                         TIMECARD_INA219_READING *reading,
                         ULONG *controllerStatus, ULONG *interruptStatus)
{
    UCHAR data[2];
    USHORT busRaw;
    LONG shuntRaw;
    LONGLONG power;
    NTSTATUS status;

    reading->Size = sizeof(*reading);
    reading->Address = address;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x00u, data, 2,
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    reading->Flags = TIMECARD_SENSOR_FLAG_PRESENT;
    reading->Configuration = TimeCardSensorReadBe16(data);
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x01u, data, 2,
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    shuntRaw = TimeCardSensorReadBeSigned16(data);
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x02u, data, 2,
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    busRaw = TimeCardSensorReadBe16(data);

    reading->RawBus = busRaw;
    reading->BusMillivolts = ((ULONG)busRaw >> 3) * 4u;
    reading->ShuntMicrovolts = shuntRaw * 10L;
    /* Each shunt is 2 milliohms on Time Card V9: 10 uV/LSB = 5 mA/LSB. */
    reading->CurrentMilliamps = shuntRaw * 5L;
    power = ((LONGLONG)reading->BusMillivolts *
             (LONGLONG)reading->CurrentMilliamps) / 1000ll;
    if (power > 2147483647ll)
        power = 2147483647ll;
    else if (power < (-2147483647ll - 1ll))
        power = (-2147483647ll - 1ll);
    reading->PowerMilliwatts = (LONG)power;
    reading->Flags |= TIMECARD_SENSOR_FLAG_VALID |
                      TIMECARD_SENSOR_FLAG_CONFIGURED;
    if ((busRaw & 0x0002u) != 0)
        reading->Flags |= TIMECARD_SENSOR_FLAG_CONVERSION_READY;
    if ((busRaw & 0x0001u) != 0)
        reading->Flags |= TIMECARD_SENSOR_FLAG_OVERFLOW;
}

static VOID
TimeCardBno055ReadLocked(PDEVICE_CONTEXT context,
                         TIMECARD_BNO055_READING *reading,
                         ULONG *controllerStatus, ULONG *interruptStatus)
{
    UCHAR chipId = 0;
    UCHAR operationMode = 0;
    UCHAR powerMode = 0;
    UCHAR calibration = 0;
    UCHAR selfTest = 0;
    UCHAR clockStatus = 0;
    UCHAR systemStatus = 0;
    UCHAR systemError = 0;
    UCHAR unitSelection = 0;
    UCHAR systemTrigger = 0;
    UCHAR data[45];
    UCHAR writeBuffer[2];
    NTSTATUS status;

    reading->Size = sizeof(*reading);
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x00u,
        &chipId, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status) || chipId != 0xa0u)
        return;
    reading->ChipId = chipId;
    reading->Flags = TIMECARD_SENSOR_FLAG_PRESENT;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x3du,
        &operationMode, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    operationMode &= 0x0fu;

    if (operationMode != 0x0cu) {
        if (operationMode != 0) {
            writeBuffer[0] = 0x3du;
            writeBuffer[1] = 0x00u;
            status = TimeCardI2cWriteLocked(
                context, TIMECARD_SENSOR_BNO055_ADDRESS,
                writeBuffer, 2, controllerStatus, interruptStatus);
            if (!NT_SUCCESS(status))
                return;
            TimeCardSensorDelayMilliseconds(20u);
        }
        /* Celsius, degrees, dps, m/s2, and Android orientation convention. */
        writeBuffer[0] = 0x3bu;
        writeBuffer[1] = 0x80u;
        status = TimeCardI2cWriteLocked(
            context, TIMECARD_SENSOR_BNO055_ADDRESS,
            writeBuffer, 2, controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        /* V9 fits Y1, so select the external 32.768 kHz timebase. */
        writeBuffer[0] = 0x3fu;
        writeBuffer[1] = 0x80u;
        status = TimeCardI2cWriteLocked(
            context, TIMECARD_SENSOR_BNO055_ADDRESS,
            writeBuffer, 2, controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        TimeCardSensorDelayMilliseconds(650u);
        writeBuffer[0] = 0x3du;
        writeBuffer[1] = 0x0cu;
        status = TimeCardI2cWriteLocked(
            context, TIMECARD_SENSOR_BNO055_ADDRESS,
            writeBuffer, 2, controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        TimeCardSensorDelayMilliseconds(10u);
        operationMode = 0x0cu;
    }

    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x08u,
        data, sizeof(data), controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x35u,
        &calibration, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x36u,
        &selfTest, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x38u,
        &clockStatus, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x39u,
        &systemStatus, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x3au,
        &systemError, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x3bu,
        &unitSelection, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x3eu,
        &powerMode, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, TIMECARD_SENSOR_BNO055_ADDRESS, 1, 0x3fu,
        &systemTrigger, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;

    reading->Calibration = calibration;
    reading->SelfTest = selfTest;
    reading->SystemClockStatus = clockStatus;
    reading->SystemStatus = systemStatus;
    reading->SystemError = systemError;
    reading->UnitSelection = unitSelection;
    reading->OperationMode = operationMode;
    reading->PowerMode = powerMode & 0x03u;
    reading->AccelerationX = TimeCardSensorReadLeSigned16(&data[0]);
    reading->AccelerationY = TimeCardSensorReadLeSigned16(&data[2]);
    reading->AccelerationZ = TimeCardSensorReadLeSigned16(&data[4]);
    reading->MagneticX = TimeCardSensorReadLeSigned16(&data[6]);
    reading->MagneticY = TimeCardSensorReadLeSigned16(&data[8]);
    reading->MagneticZ = TimeCardSensorReadLeSigned16(&data[10]);
    reading->GyroscopeX = TimeCardSensorReadLeSigned16(&data[12]);
    reading->GyroscopeY = TimeCardSensorReadLeSigned16(&data[14]);
    reading->GyroscopeZ = TimeCardSensorReadLeSigned16(&data[16]);
    reading->Heading = TimeCardSensorReadLeSigned16(&data[18]);
    reading->Roll = TimeCardSensorReadLeSigned16(&data[20]);
    reading->Pitch = TimeCardSensorReadLeSigned16(&data[22]);
    reading->QuaternionW = TimeCardSensorReadLeSigned16(&data[24]);
    reading->QuaternionX = TimeCardSensorReadLeSigned16(&data[26]);
    reading->QuaternionY = TimeCardSensorReadLeSigned16(&data[28]);
    reading->QuaternionZ = TimeCardSensorReadLeSigned16(&data[30]);
    reading->LinearAccelerationX =
        TimeCardSensorReadLeSigned16(&data[32]);
    reading->LinearAccelerationY =
        TimeCardSensorReadLeSigned16(&data[34]);
    reading->LinearAccelerationZ =
        TimeCardSensorReadLeSigned16(&data[36]);
    reading->GravityX = TimeCardSensorReadLeSigned16(&data[38]);
    reading->GravityY = TimeCardSensorReadLeSigned16(&data[40]);
    reading->GravityZ = TimeCardSensorReadLeSigned16(&data[42]);
    reading->Temperature = (LONG)(CHAR)data[44];
    reading->Flags |= TIMECARD_SENSOR_FLAG_VALID |
                      TIMECARD_SENSOR_FLAG_CONVERSION_READY;
    if (reading->OperationMode == 0x0cu)
        reading->Flags |= TIMECARD_SENSOR_FLAG_CONFIGURED;
    if ((systemTrigger & 0x80u) != 0 &&
        (reading->SystemClockStatus & 0x01u) == 0) {
        reading->Flags |= TIMECARD_SENSOR_FLAG_EXTERNAL_CLOCK;
    }
}

NTSTATUS
TimeCardSensorQuery(PDEVICE_CONTEXT context,
                    TIMECARD_SENSOR_TELEMETRY *telemetry)
{
    UCHAR savedMask = 0;
    BOOLEAN changed = FALSE;
    ULONG controllerStatus = 0;
    ULONG interruptStatus = 0;
    NTSTATUS status;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(telemetry, sizeof(*telemetry));
    telemetry->Size = sizeof(*telemetry);
    telemetry->Environment.Size = sizeof(telemetry->Environment);
    telemetry->Rail12V.Size = sizeof(telemetry->Rail12V);
    telemetry->Rail12V.Address = TIMECARD_SENSOR_INA219_12V_ADDRESS;
    telemetry->Rail5V.Size = sizeof(telemetry->Rail5V);
    telemetry->Rail5V.Address = TIMECARD_SENSOR_INA219_5V_ADDRESS;
    telemetry->Rail3V3.Size = sizeof(telemetry->Rail3V3);
    telemetry->Rail3V3.Address = TIMECARD_SENSOR_INA219_3V3_ADDRESS;
    telemetry->Imu.Size = sizeof(telemetry->Imu);

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardSensorBranchSelectLocked(
        context, &savedMask, &changed,
        &controllerStatus, &interruptStatus);
    telemetry->MuxChannelMask = savedMask;
    if (!NT_SUCCESS(status))
        goto Exit;
    telemetry->Flags = TIMECARD_SENSOR_FLAG_PRESENT;

    TimeCardBme280ReadLocked(
        context, &telemetry->Environment,
        &controllerStatus, &interruptStatus);
    TimeCardIna219ReadLocked(
        context, TIMECARD_SENSOR_INA219_12V_ADDRESS,
        &telemetry->Rail12V, &controllerStatus, &interruptStatus);
    TimeCardIna219ReadLocked(
        context, TIMECARD_SENSOR_INA219_5V_ADDRESS,
        &telemetry->Rail5V, &controllerStatus, &interruptStatus);
    TimeCardIna219ReadLocked(
        context, TIMECARD_SENSOR_INA219_3V3_ADDRESS,
        &telemetry->Rail3V3, &controllerStatus, &interruptStatus);
    TimeCardBno055ReadLocked(
        context, &telemetry->Imu,
        &controllerStatus, &interruptStatus);
    telemetry->Flags |= TIMECARD_SENSOR_FLAG_VALID;

Exit:
    status = TimeCardSensorBranchRestoreLocked(
        context, savedMask, changed, status,
        &controllerStatus, &interruptStatus);
    telemetry->ControllerStatus = controllerStatus;
    telemetry->InterruptStatus = interruptStatus;
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardGetIdentity(PDEVICE_CONTEXT context, TIMECARD_IDENTITY *identity)
{
    TIMECARD_I2C_READ_REQUEST request;
    TIMECARD_I2C_TRANSFER transfer;
    BOOLEAN allZero = TRUE;
    BOOLEAN allOnes = TRUE;
    NTSTATUS status;
    ULONG i;

    RtlZeroMemory(identity, sizeof(*identity));
    identity->Size = sizeof(*identity);

    RtlZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.Address = TIMECARD_IDENTITY_ADDRESS;
    request.SubaddressLength = 1u;
    request.Subaddress = TIMECARD_IDENTITY_EUI48_OFFSET;
    request.Length = TIMECARD_IDENTITY_SERIAL_LENGTH;
    request.TimeoutMilliseconds = TIMECARD_I2C_DEFAULT_TIMEOUT_MS;

    status = TimeCardI2cRead(context, &request, &transfer);
    if (!NT_SUCCESS(status))
        return status;
    if (transfer.Length != TIMECARD_IDENTITY_SERIAL_LENGTH)
        return STATUS_IO_DEVICE_ERROR;

    identity->Flags = TIMECARD_IDENTITY_FLAG_PRESENT;
    for (i = 0; i < TIMECARD_IDENTITY_SERIAL_LENGTH; ++i) {
        identity->Serial[i] = transfer.Data[i];
        if (transfer.Data[i] != 0)
            allZero = FALSE;
        if (transfer.Data[i] != 0xffu)
            allOnes = FALSE;
    }
    if (!allZero && !allOnes)
        identity->Flags |= TIMECARD_IDENTITY_FLAG_VALID;
    return STATUS_SUCCESS;
}
