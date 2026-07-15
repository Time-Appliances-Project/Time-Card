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
    if (address == 0x50u)
        return TIMECARD_I2C_DEVICE_BOARD_EEPROM;
    if (address == 0x58u)
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
        TimeCardI2cWrite16(
            context, XIIC_DTR_OFFSET,
            (USHORT)(XIIC_TX_DYN_START | (request->Address << 1)));
        if (request->SubaddressLength == 2u) {
            TimeCardI2cWrite16(
                context, XIIC_DTR_OFFSET,
                (USHORT)((request->Subaddress >> 8) & 0xffu));
        }
        TimeCardI2cWrite16(context, XIIC_DTR_OFFSET,
                           (USHORT)(request->Subaddress & 0xffu));

        status = TimeCardI2cWaitTransmitPhase(
            context, &remainingPolls, &observedInterrupts,
            &controllerStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
    }

    /*
     * AXI IIC reports the master's expected final receive NACK through the
     * same TX_ERROR bit used for a slave address NACK.  Program a zero-based
     * RX watermark, only drain after RX_FULL, then clear RX_FULL and the
     * accompanying TX_ERROR together.  This mirrors the controller's
     * dynamic receive state machine and removes the completion race.
    */
    TimeCardI2cSetReceiveWatermark(context, request->Length);
    TimeCardI2cClearInterruptMask(
        context, XIIC_INTR_RX_FULL | XIIC_INTR_TX_ERROR);
    TimeCardI2cWrite16(
        context, XIIC_DTR_OFFSET,
        (USHORT)(XIIC_TX_DYN_START | (request->Address << 1) | 1u));
    TimeCardI2cWrite16(
        context, XIIC_DTR_OFFSET,
        (USHORT)(XIIC_TX_DYN_STOP | request->Length));

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

        if ((interrupts & XIIC_INTR_RX_FULL) != 0) {
            TimeCardI2cDrainReceiveFifo(context, request, transfer,
                                        &copied);
            TimeCardI2cClearInterruptMask(
                context, XIIC_INTR_RX_FULL | XIIC_INTR_TX_ERROR);
            controllerStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);
        }

        if (copied == request->Length &&
            ((interrupts & XIIC_INTR_BNB) != 0 ||
             (controllerStatus & XIIC_SR_BUS_BUSY) == 0)) {
            status = STATUS_SUCCESS;
            break;
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

    status = TimeCardI2cPrepareTransfer(context);
    if (!NT_SUCCESS(status))
        goto Exit;

    TimeCardI2cArmPolling(context);
    TimeCardI2cWrite16(
        context, XIIC_DTR_OFFSET,
        (USHORT)(XIIC_TX_DYN_START | (address << 1)));
    for (i = 0; i < length; ++i) {
        USHORT value = data[i];
        if (i + 1u == length)
            value |= XIIC_TX_DYN_STOP;
        TimeCardI2cWrite16(context, XIIC_DTR_OFFSET, value);
    }

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
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardI2cProbe(PDEVICE_CONTEXT context, ULONG address,
                 TIMECARD_I2C_PROBE *probe)
{
    ULONG i;
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
    status = TimeCardI2cPrepareTransfer(context);
    if (!NT_SUCCESS(status))
        goto Exit;

    TimeCardI2cArmPolling(context);
    TimeCardI2cWrite16(
        context, XIIC_DTR_OFFSET,
        (USHORT)(XIIC_TX_DYN_START | XIIC_TX_DYN_STOP | (address << 1)));

    status = STATUS_IO_TIMEOUT;
    for (i = 0; i < (TIMECARD_I2C_DEFAULT_TIMEOUT_MS * 1000u /
                     TIMECARD_I2C_POLL_US); ++i) {
        interruptStatus = TimeCardI2cRead32(context, XIIC_IISR_OFFSET);
        controllerStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);
        if ((interruptStatus & XIIC_INTR_ARB_LOST) != 0) {
            status = STATUS_IO_DEVICE_ERROR;
            break;
        }
        if ((interruptStatus & XIIC_INTR_TX_ERROR) != 0) {
            probe->Present = 0;
            status = STATUS_SUCCESS;
            break;
        }
        if ((interruptStatus & XIIC_INTR_BNB) != 0) {
            probe->Present = 1;
            status = STATUS_SUCCESS;
            break;
        }
        KeStallExecutionProcessor(TIMECARD_I2C_POLL_US);
    }

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
TimeCardLedSelectLocked(PDEVICE_CONTEXT context, UCHAR *savedMask,
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
TimeCardLedRestoreLocked(PDEVICE_CONTEXT context, UCHAR savedMask,
                         BOOLEAN changed, NTSTATUS operationStatus,
                         ULONG *controllerStatus, ULONG *interruptStatus)
{
    NTSTATUS restoreStatus;

    if (!changed)
        return operationStatus;
    restoreStatus = TimeCardI2cMuxWriteLocked(
        context, savedMask, controllerStatus, interruptStatus);
    return NT_SUCCESS(operationStatus) ? restoreStatus : operationStatus;
}

NTSTATUS
TimeCardLedQuery(PDEVICE_CONTEXT context, ULONG led,
                 TIMECARD_LED_CONTROL *control)
{
    UCHAR savedMask = 0;
    BOOLEAN changed = FALSE;
    UCHAR deviceControl = 0;
    UCHAR globalCurrent = 0;
    UCHAR pwm[6];
    ULONG controllerStatus = 0;
    ULONG interruptStatus = 0;
    NTSTATUS status;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (led >= TIMECARD_LED_COUNT)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Led = led;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardLedSelectLocked(
        context, &savedMask, &changed,
        &controllerStatus, &interruptStatus);
    control->MuxChannelMask = savedMask;
    if (!NT_SUCCESS(status))
        goto Exit;

    status = TimeCardI2cReadBytesLocked(
        context, 0x6eu, 1, 0x00u, &deviceControl, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, 0x6eu, 1, 0x6eu, &globalCurrent, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, 0x6eu, 1, 0x01u + led * 6u, pwm, sizeof(pwm),
        &controllerStatus, &interruptStatus);
    if (NT_SUCCESS(status)) {
        control->Flags = TIMECARD_LED_FLAG_PRESENT;
        if ((deviceControl & 0x01u) != 0)
            control->Flags |= TIMECARD_LED_FLAG_ENABLED;
        control->Red = pwm[0];
        control->Green = pwm[2];
        control->Blue = pwm[4];
        control->GlobalCurrent = globalCurrent;
    }

Exit:
    status = TimeCardLedRestoreLocked(
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
    ULONG controllerStatus = 0;
    ULONG interruptStatus = 0;
    NTSTATUS status;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        request->Led >= TIMECARD_LED_COUNT ||
        request->Red > 0xffu || request->Green > 0xffu ||
        request->Blue > 0xffu || request->GlobalCurrent == 0 ||
        request->GlobalCurrent > TIMECARD_LED_MAX_GLOBAL_CURRENT) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Led = request->Led;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardLedSelectLocked(
        context, &savedMask, &changed,
        &controllerStatus, &interruptStatus);
    response->MuxChannelMask = savedMask;
    if (!NT_SUCCESS(status))
        goto Exit;

    /* 8-bit PWM mode, normal operation.  This yields flicker-free PWM. */
    writeBuffer[0] = 0x00u;
    writeBuffer[1] = 0x01u;
    status = TimeCardI2cWriteLocked(
        context, 0x6eu, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    writeBuffer[0] = 0x6eu;
    writeBuffer[1] = (UCHAR)request->GlobalCurrent;
    status = TimeCardI2cWriteLocked(
        context, 0x6eu, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    /* Apply full per-channel scaling; global current remains safety-capped. */
    writeBuffer[0] = (UCHAR)(0x4au + request->Led * 3u);
    writeBuffer[1] = 0xffu;
    writeBuffer[2] = 0xffu;
    writeBuffer[3] = 0xffu;
    status = TimeCardI2cWriteLocked(
        context, 0x6eu, writeBuffer, 4,
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
        context, 0x6eu, writeBuffer, sizeof(writeBuffer),
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    writeBuffer[0] = 0x49u;
    writeBuffer[1] = 0;
    status = TimeCardI2cWriteLocked(
        context, 0x6eu, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (NT_SUCCESS(status)) {
        response->Flags = TIMECARD_LED_FLAG_PRESENT |
                          TIMECARD_LED_FLAG_ENABLED;
        response->Red = request->Red;
        response->Green = request->Green;
        response->Blue = request->Blue;
        response->GlobalCurrent = request->GlobalCurrent;
    }

Exit:
    status = TimeCardLedRestoreLocked(
        context, savedMask, changed, status,
        &controllerStatus, &interruptStatus);
    response->ControllerStatus = controllerStatus;
    response->InterruptStatus = interruptStatus;
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
    request.Address = 0x58u;
    request.SubaddressLength = 1u;
    request.Subaddress = 0u;
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
