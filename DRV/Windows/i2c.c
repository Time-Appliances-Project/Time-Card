/* SPDX-License-Identifier: BSD-3-Clause */
/* Bounded, read-only Xilinx AXI IIC access for the OCP Time Card. */

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
    return STATUS_DEVICE_DATA_ERROR;
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
            status = STATUS_DEVICE_DATA_ERROR;
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
    ULONG timeoutMilliseconds;
    ULONG pollCount;
    ULONG copied = 0;
    ULONG i;
    ULONG interruptStatus = 0;
    UCHAR controllerStatus = 0;
    NTSTATUS status;

    if (!context->HardwareReady || context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        !TimeCardI2cAddressValid(request->Address) ||
        request->SubaddressLength > 2u || request->Length == 0 ||
        request->Length > TIMECARD_I2C_MAX_TRANSFER) {
        return STATUS_INVALID_PARAMETER;
    }

    timeoutMilliseconds = request->TimeoutMilliseconds;
    if (timeoutMilliseconds == 0)
        timeoutMilliseconds = TIMECARD_I2C_DEFAULT_TIMEOUT_MS;
    if (timeoutMilliseconds > TIMECARD_I2C_MAX_TIMEOUT_MS)
        timeoutMilliseconds = TIMECARD_I2C_MAX_TIMEOUT_MS;
    pollCount = timeoutMilliseconds * 1000u / TIMECARD_I2C_POLL_US;

    RtlZeroMemory(transfer, sizeof(*transfer));
    transfer->Size = (ULONG)FIELD_OFFSET(TIMECARD_I2C_TRANSFER, Data) +
                     request->Length;
    transfer->Address = request->Address;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardI2cPrepareTransfer(context);
    if (!NT_SUCCESS(status))
        goto Exit;

    TimeCardI2cArmPolling(context);
    TimeCardI2cWrite8(
        context, XIIC_RFD_OFFSET,
        (UCHAR)((request->Length < XIIC_FIFO_DEPTH ? request->Length :
                 XIIC_FIFO_DEPTH) - 1u));

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
    }

    TimeCardI2cWrite16(
        context, XIIC_DTR_OFFSET,
        (USHORT)(XIIC_TX_DYN_START | (request->Address << 1) | 1u));
    TimeCardI2cWrite16(
        context, XIIC_DTR_OFFSET,
        (USHORT)(XIIC_TX_DYN_STOP | request->Length));

    status = STATUS_IO_TIMEOUT;
    for (i = 0; i < pollCount; ++i) {
        interruptStatus = TimeCardI2cRead32(context, XIIC_IISR_OFFSET);
        controllerStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);

        if ((interruptStatus & XIIC_INTR_ARB_LOST) != 0) {
            status = STATUS_DEVICE_DATA_ERROR;
            break;
        }

        while ((controllerStatus & XIIC_SR_RX_EMPTY) == 0 &&
               copied < request->Length) {
            transfer->Data[copied++] =
                TimeCardI2cRead8(context, XIIC_DRR_OFFSET);
            controllerStatus = TimeCardI2cRead8(context, XIIC_SR_OFFSET);
        }

        if (copied == request->Length &&
            (controllerStatus & XIIC_SR_BUS_BUSY) == 0) {
            status = STATUS_SUCCESS;
            break;
        }

        if ((interruptStatus & XIIC_INTR_TX_ERROR) != 0 &&
            (interruptStatus & XIIC_INTR_RX_FULL) == 0 && copied == 0 &&
            ((interruptStatus & XIIC_INTR_BNB) != 0 ||
             (controllerStatus & XIIC_SR_BUS_BUSY) == 0)) {
            status = STATUS_NO_SUCH_DEVICE;
            break;
        }

        if ((interruptStatus & XIIC_INTR_BNB) != 0 &&
            copied < request->Length) {
            status = STATUS_DEVICE_DATA_ERROR;
            break;
        }
        KeStallExecutionProcessor(TIMECARD_I2C_POLL_US);
    }

    transfer->Length = copied;
    transfer->ControllerStatus = controllerStatus;
    transfer->InterruptStatus = interruptStatus;
    (VOID)TimeCardI2cReset(context);

Exit:
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
        return STATUS_DEVICE_DATA_ERROR;

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
