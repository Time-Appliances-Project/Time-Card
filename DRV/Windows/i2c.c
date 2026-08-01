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
#define TIMECARD_INA219_CONFIGURATION 0x399fu
#define TIMECARD_IMU_TYPE_NONE 0u
#define TIMECARD_IMU_TYPE_BNO055 1u
#define TIMECARD_IMU_TYPE_BNO08X 2u
#define TIMECARD_BNO08X_MAX_PACKET_LENGTH 1024u
#define TIMECARD_BNO08X_REPORT_INTERVAL_US 250000u
#define TIMECARD_BNO08X_CHANNEL_CONTROL 2u
#define TIMECARD_BNO08X_CHANNEL_REPORTS 3u
#define TIMECARD_BNO08X_SET_FEATURE 0xfdu
#define TIMECARD_BNO08X_BASE_TIMESTAMP 0xfbu
#define TIMECARD_BNO08X_ACCELEROMETER 0x01u
#define TIMECARD_BNO08X_GYROSCOPE 0x02u
#define TIMECARD_BNO08X_MAGNETIC_FIELD 0x03u
#define TIMECARD_BNO08X_LINEAR_ACCELERATION 0x04u
#define TIMECARD_BNO08X_ROTATION_VECTOR 0x05u
#define TIMECARD_BNO08X_GRAVITY 0x06u
#define TIMECARD_BNO08X_TEMPERATURE 0x0eu

#define TIMECARD_SHT3X_STATUS_COMMAND_HI 0xf3u
#define TIMECARD_SHT3X_STATUS_COMMAND_LO 0x2du
#define TIMECARD_SHT3X_MEASURE_COMMAND_HI 0x24u
#define TIMECARD_SHT3X_MEASURE_COMMAND_LO 0x00u
#define TIMECARD_ICP10100_PRODUCT_ID 0x08u
#define TIMECARD_ICP10100_READ_ID_HI 0xefu
#define TIMECARD_ICP10100_READ_ID_LO 0xc8u
#define TIMECARD_ICP10100_MEASURE_HI 0x50u
#define TIMECARD_ICP10100_MEASURE_LO 0x59u

static NTSTATUS
TimeCardI2cWriteLocked(PDEVICE_CONTEXT context, ULONG address,
                       const UCHAR *data, ULONG length,
                       ULONG *controllerStatus, ULONG *interruptStatus);

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
    if (address == TIMECARD_BOARD_EEPROM_ADDRESS || address == 0x52u)
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
        /*
         * Match Linux i2c-xiic's transmit ordering: put START/address in the
         * FIFO, acknowledge stale completion state, then append payload.
         * Clearing TX_EMPTY after the payload can erase a real completion on
         * short register transactions.
         */
        TimeCardI2cClearInterruptMask(
            context, XIIC_INTR_TX_EMPTY | XIIC_INTR_TX_ERROR |
                     XIIC_INTR_BNB);
        if (request->SubaddressLength == 2u) {
            TimeCardI2cWrite16(
                context, XIIC_DTR_OFFSET,
                (USHORT)((request->Subaddress >> 8) & 0xffu));
        }
        TimeCardI2cWrite16(context, XIIC_DTR_OFFSET,
                           (USHORT)(request->Subaddress & 0xffu));
        TimeCardI2cCaptureStartTrace(context);

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
    TimeCardI2cClearInterruptMask(
        context, XIIC_INTR_ARB_LOST | XIIC_INTR_TX_ERROR |
                 XIIC_INTR_TX_EMPTY | XIIC_INTR_RX_FULL);
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

    if (context->I2cController == TIMECARD_I2C_CONTROLLER_OCORES)
        return TimeCardOcoresI2cReadLocked(context, request, transfer);

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

    if (status == STATUS_NO_SUCH_DEVICE &&
        request->SubaddressLength != 0) {
        TIMECARD_I2C_READ_REQUEST readRequest = *request;
        UCHAR pointer[2];
        ULONG controllerStatus = 0;
        ULONG interruptStatus = 0;

        /*
         * Some V9 register reads ACK the address and pointer byte but return a
         * read-address NACK during the AXI-IIC dynamic repeated START.
         * INA219 and BME/BMP280 retain their register pointer across STOP, so
         * retry the exact same operation as two bounded bus messages. This
         * also matches the common SMBus "write byte, read word" transaction.
         */
        if (request->SubaddressLength == 2u) {
            pointer[0] = (UCHAR)((request->Subaddress >> 8) & 0xffu);
            pointer[1] = (UCHAR)(request->Subaddress & 0xffu);
        } else {
            pointer[0] = (UCHAR)(request->Subaddress & 0xffu);
        }
        status = TimeCardI2cWriteLocked(
            context, request->Address, pointer,
            request->SubaddressLength,
            &controllerStatus, &interruptStatus);
        if (NT_SUCCESS(status)) {
            readRequest.SubaddressLength = 0;
            readRequest.Subaddress = 0;
            transfer->Length = 0;
            transfer->ControllerStatus = 0;
            transfer->InterruptStatus = 0;
            status = TimeCardI2cReadAttempt(
                context, &readRequest, transfer, pollCount);
        }
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
    BOOLEAN traceCaptured = FALSE;
    ULONG chunk;
    ULONG i = 0;
    NTSTATUS status;

    context->I2cLastStartTrace = 0;
    context->I2cLastStartEvents = 0;
    status = TimeCardI2cPrepareTransfer(context);
    if (!NT_SUCCESS(status))
        goto Exit;

    TimeCardI2cArmPolling(context);
    TimeCardI2cQueueDynamicStart(context, (USHORT)(address << 1));
    TimeCardI2cClearInterruptMask(
        context, XIIC_INTR_TX_EMPTY | XIIC_INTR_TX_ERROR | XIIC_INTR_BNB);

    /*
     * Stream transfers larger than the 16-entry FIFO without releasing the
     * bus.  This is required by BNO08x SHTP control packets (21 bytes).
     * The first chunk reserves one FIFO entry for START/address; later chunks
     * can use the whole FIFO.  STOP is attached only to the final payload.
     */
    while (i < length) {
        ULONG capacity = i == 0 ? XIIC_FIFO_DEPTH - 1u :
                                  XIIC_FIFO_DEPTH;

        chunk = length - i;
        if (chunk > capacity)
            chunk = capacity;
        while (chunk-- != 0) {
            USHORT value = data[i];

            if (i + 1u == length)
                value |= XIIC_TX_DYN_STOP;
            TimeCardI2cWrite16(context, XIIC_DTR_OFFSET, value);
            ++i;
        }
        if (!traceCaptured) {
            TimeCardI2cCaptureStartTrace(context);
            traceCaptured = TRUE;
        }
        if (i < length) {
            status = TimeCardI2cWaitTransmitPhase(
                context, &remainingPolls, &observedInterrupts,
                &currentStatus);
            if (!NT_SUCCESS(status))
                goto Exit;
        }
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
        length == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    if (context->I2cController == TIMECARD_I2C_CONTROLLER_OCORES) {
        return TimeCardOcoresI2cWriteLocked(
            context, address, data, length,
            controllerStatus, interruptStatus);
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

    if (context->I2cController == TIMECARD_I2C_CONTROLLER_OCORES) {
        NTSTATUS result;

        WdfWaitLockAcquire(context->RegisterLock, NULL);
        result = TimeCardOcoresI2cGetStatusLocked(context, status);
        WdfWaitLockRelease(context->RegisterLock);
        return result;
    }

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

    if (context->I2cController == TIMECARD_I2C_CONTROLLER_OCORES) {
        WdfWaitLockAcquire(context->RegisterLock, NULL);
        status = TimeCardOcoresI2cProbeLocked(
            context, address, probe);
        if (NT_SUCCESS(status)) {
            flag = TimeCardI2cKnownDeviceFlag(address);
            if (probe->Present != 0)
                context->I2cKnownDeviceMask |= flag;
            else
                context->I2cKnownDeviceMask &= ~flag;
        }
        WdfWaitLockRelease(context->RegisterLock);
        return status;
    }

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

static BOOLEAN
TimeCardBno08xHeaderValid(const UCHAR *header)
{
    ULONG length = (ULONG)header[0] |
                   (((ULONG)header[1] & 0x7fu) << 8);

    return length >= 4u &&
           length <= TIMECARD_BNO08X_MAX_PACKET_LENGTH &&
           header[2] <= 5u;
}

static BOOLEAN
TimeCardSensorBranchProbeLocked(PDEVICE_CONTEXT context, UCHAR channelMask,
                                ULONG *controllerStatus,
                                ULONG *interruptStatus)
{
    static const ULONG bno055Addresses[] = {
        TIMECARD_SENSOR_BNO055_ADDRESS,
        TIMECARD_SENSOR_BNO055_ADDRESS_ALTERNATE
    };
    static const ULONG bno08xAddresses[] = {
        TIMECARD_SENSOR_BNO08X_ADDRESS,
        TIMECARD_SENSOR_BNO08X_ADDRESS_ALTERNATE
    };
    UCHAR value = 0;
    UCHAR header[4];
    ULONG i;
    NTSTATUS status;

    status = TimeCardI2cMuxWriteLocked(
        context, channelMask, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return FALSE;
    KeStallExecutionProcessor(10u);

    for (i = 0; i < ARRAYSIZE(bno055Addresses); ++i) {
        status = TimeCardI2cReadBytesLocked(
            context, bno055Addresses[i], 1, 0x00u, &value, 1,
            controllerStatus, interruptStatus);
        if (NT_SUCCESS(status) && value == 0xa0u) {
            context->I2cImuType = TIMECARD_IMU_TYPE_BNO055;
            context->I2cImuAddress = bno055Addresses[i];
            return TRUE;
        }
    }

    /*
     * BNO08x does not expose BNO055-style registers.  Its first four bytes
     * are an SHTP header; a bounded length and channel identify it without
     * writing or consuming the queued transfer.
     */
    for (i = 0; i < ARRAYSIZE(bno08xAddresses); ++i) {
        status = TimeCardI2cReadBytesLocked(
            context, bno08xAddresses[i], 0, 0, header, sizeof(header),
            controllerStatus, interruptStatus);
        if (NT_SUCCESS(status) && TimeCardBno08xHeaderValid(header)) {
            context->I2cImuType = TIMECARD_IMU_TYPE_BNO08X;
            context->I2cImuAddress = bno08xAddresses[i];
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
TimeCardEnvironmentBranchProbeLocked(PDEVICE_CONTEXT context,
                                     UCHAR channelMask,
                                     ULONG *controllerStatus,
                                     ULONG *interruptStatus)
{
    static const ULONG addresses[] = {
        TIMECARD_SENSOR_BME280_ADDRESS,
        TIMECARD_SENSOR_BME280_ADDRESS_ALTERNATE
    };
    UCHAR chipId = 0;
    ULONG i;
    NTSTATUS status = TimeCardI2cMuxWriteLocked(
        context, channelMask, controllerStatus, interruptStatus);

    if (!NT_SUCCESS(status))
        return FALSE;
    KeStallExecutionProcessor(10u);
    for (i = 0; i < ARRAYSIZE(addresses); ++i) {
        status = TimeCardI2cReadBytesLocked(
            context, addresses[i], 1, 0xd0u, &chipId, 1,
            controllerStatus, interruptStatus);
        if (NT_SUCCESS(status) && (chipId == 0x60u || chipId == 0x58u)) {
            context->I2cEnvironmentAddress = addresses[i];
            return TRUE;
        }
    }
    return FALSE;
}

static BOOLEAN
TimeCardPowerBranchProbeLocked(PDEVICE_CONTEXT context, UCHAR channelMask,
                               ULONG *controllerStatus,
                               ULONG *interruptStatus)
{
    static const ULONG addresses[] = {
        TIMECARD_SENSOR_INA219_12V_ADDRESS,
        TIMECARD_SENSOR_INA219_5V_ADDRESS,
        TIMECARD_SENSOR_INA219_3V3_ADDRESS
    };
    UCHAR configuration[2];
    ULONG i;
    NTSTATUS status = TimeCardI2cMuxWriteLocked(
        context, channelMask, controllerStatus, interruptStatus);

    if (!NT_SUCCESS(status))
        return FALSE;
    KeStallExecutionProcessor(10u);
    for (i = 0; i < ARRAYSIZE(addresses); ++i) {
        status = TimeCardI2cReadBytesLocked(
            context, addresses[i], 1, 0x00u, configuration,
            sizeof(configuration), controllerStatus, interruptStatus);
        if (NT_SUCCESS(status))
            return TRUE;
    }
    return FALSE;
}

static UCHAR
TimeCardMonitorBranchResolveLocked(PDEVICE_CONTEXT context,
                                   BOOLEAN environment,
                                   ULONG *controllerStatus,
                                   ULONG *interruptStatus)
{
    static const UCHAR candidates[] = {
        TIMECARD_I2C_MUX_CHANNEL_SENSORS,
        TIMECARD_I2C_MUX_CHANNEL_DC,
        TIMECARD_I2C_MUX_CHANNEL_MAC,
        TIMECARD_I2C_MUX_CHANNEL_ANADC
    };
    ULONG *cachedMask = environment ? &context->I2cEnvironmentMuxMask :
                                      &context->I2cPowerMuxMask;
    ULONG i;

    if (*cachedMask != 0)
        return (UCHAR)*cachedMask;
    for (i = 0; i < ARRAYSIZE(candidates); ++i) {
        BOOLEAN found = environment ?
            TimeCardEnvironmentBranchProbeLocked(
                context, candidates[i], controllerStatus, interruptStatus) :
            TimeCardPowerBranchProbeLocked(
                context, candidates[i], controllerStatus, interruptStatus);
        if (found) {
            *cachedMask = candidates[i];
            return candidates[i];
        }
    }
    return TIMECARD_I2C_MUX_CHANNEL_SENSORS;
}

static NTSTATUS
TimeCardSensorBranchSelectLocked(PDEVICE_CONTEXT context, UCHAR *savedMask,
                                 BOOLEAN *changed, ULONG *controllerStatus,
                                 ULONG *interruptStatus)
{
    static const UCHAR candidates[] = {
        TIMECARD_I2C_MUX_CHANNEL_SENSORS,
        TIMECARD_I2C_MUX_CHANNEL_DC,
        TIMECARD_I2C_MUX_CHANNEL_MAC,
        TIMECARD_I2C_MUX_CHANNEL_ANADC
    };
    UCHAR selectedMask;
    ULONG i;
    NTSTATUS status = TimeCardI2cMuxReadLocked(
        context, savedMask, controllerStatus, interruptStatus);

    *changed = FALSE;
    if (!NT_SUCCESS(status))
        return status;

    selectedMask = (UCHAR)context->I2cSensorMuxMask;
    if (selectedMask == 0) {
        context->I2cImuType = TIMECARD_IMU_TYPE_NONE;
        context->I2cImuAddress = 0;
        for (i = 0; i < ARRAYSIZE(candidates); ++i) {
            if (TimeCardSensorBranchProbeLocked(
                    context, candidates[i],
                    controllerStatus, interruptStatus)) {
                selectedMask = candidates[i];
                context->I2cSensorMuxMask = selectedMask;
                break;
            }
        }
        if (selectedMask == 0)
            selectedMask = TIMECARD_I2C_MUX_CHANNEL_SENSORS;
    }

    if (*savedMask == selectedMask)
        return STATUS_SUCCESS;

    status = TimeCardI2cMuxWriteLocked(
        context, selectedMask,
        controllerStatus, interruptStatus);
    if (NT_SUCCESS(status)) {
        *changed = TRUE;
        /*
         * PCA9546A switching is fast, but allow the selected branch and its
         * pull-ups to settle before issuing a repeated controller reset/START.
         */
        KeStallExecutionProcessor(10u);
    }
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

static VOID
TimeCardSensorDelayMilliseconds(ULONG milliseconds)
{
    LARGE_INTEGER interval;

    interval.QuadPart = -((LONGLONG)milliseconds * 10000ll);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

static UCHAR
TimeCardSensorCrc8(const UCHAR *data, ULONG length)
{
    UCHAR crc = 0xffu;
    ULONG byteIndex;

    for (byteIndex = 0; byteIndex < length; ++byteIndex) {
        ULONG bit;

        crc ^= data[byteIndex];
        for (bit = 0; bit < 8u; ++bit) {
            crc = (crc & 0x80u) != 0 ?
                (UCHAR)((crc << 1) ^ 0x31u) : (UCHAR)(crc << 1);
        }
    }
    return crc;
}

static BOOLEAN
TimeCardSensorFrameCrcValid(const UCHAR *frame)
{
    return TimeCardSensorCrc8(frame, 2u) == frame[2];
}

static VOID
TimeCardLm75bReadLocked(PDEVICE_CONTEXT context, ULONG address,
                        TIMECARD_LM75B_READING *reading,
                        ULONG *controllerStatus, ULONG *interruptStatus)
{
    UCHAR data[2];
    SHORT raw;
    NTSTATUS status;

    RtlZeroMemory(reading, sizeof(*reading));
    reading->Size = sizeof(*reading);
    reading->Address = address;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1u, 0x00u, data, sizeof(data),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;

    reading->Flags = TIMECARD_SENSOR_FLAG_PRESENT;
    raw = (SHORT)(((USHORT)data[0] << 8) | data[1]);
    reading->RawTemperature = raw;
    reading->TemperatureMilliCelsius = ((LONG)raw * 1000l) / 256l;
    if (reading->TemperatureMilliCelsius < -55000l ||
        reading->TemperatureMilliCelsius > 125000l) {
        return;
    }
    reading->Flags |= TIMECARD_SENSOR_FLAG_VALID |
                      TIMECARD_SENSOR_FLAG_CONVERSION_READY |
                      TIMECARD_SENSOR_FLAG_TEMPERATURE;
}

static VOID
TimeCardSht3xReadLocked(PDEVICE_CONTEXT context,
                        TIMECARD_SHT3X_READING *reading,
                        ULONG *controllerStatus, ULONG *interruptStatus)
{
    static const UCHAR statusCommand[] = {
        TIMECARD_SHT3X_STATUS_COMMAND_HI,
        TIMECARD_SHT3X_STATUS_COMMAND_LO
    };
    static const UCHAR measureCommand[] = {
        TIMECARD_SHT3X_MEASURE_COMMAND_HI,
        TIMECARD_SHT3X_MEASURE_COMMAND_LO
    };
    UCHAR statusFrame[3];
    UCHAR data[6];
    NTSTATUS status;

    RtlZeroMemory(reading, sizeof(*reading));
    reading->Size = sizeof(*reading);
    reading->Address = TIMECARD_SENSOR_SHT3X_ADDRESS;
    status = TimeCardI2cWriteLocked(
        context, reading->Address, statusCommand, sizeof(statusCommand),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    reading->Flags = TIMECARD_SENSOR_FLAG_PRESENT;
    TimeCardSensorDelayMilliseconds(1u);
    status = TimeCardI2cReadBytesLocked(
        context, reading->Address, 0u, 0u, statusFrame,
        sizeof(statusFrame), controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status) || !TimeCardSensorFrameCrcValid(statusFrame))
        return;
    reading->Status = ((ULONG)statusFrame[0] << 8) | statusFrame[1];

    status = TimeCardI2cWriteLocked(
        context, reading->Address, measureCommand, sizeof(measureCommand),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    TimeCardSensorDelayMilliseconds(20u);
    status = TimeCardI2cReadBytesLocked(
        context, reading->Address, 0u, 0u, data, sizeof(data),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status) || !TimeCardSensorFrameCrcValid(&data[0]) ||
        !TimeCardSensorFrameCrcValid(&data[3])) {
        return;
    }

    reading->RawTemperature = ((ULONG)data[0] << 8) | data[1];
    reading->RawHumidity = ((ULONG)data[3] << 8) | data[4];
    reading->TemperatureMilliCelsius = -45000l + (LONG)(
        (175000ll * reading->RawTemperature) / 65535ll);
    reading->HumidityMilliPercent = (ULONG)(
        (100000ull * reading->RawHumidity) / 65535ull);
    reading->Flags |= TIMECARD_SENSOR_FLAG_VALID |
                      TIMECARD_SENSOR_FLAG_CONFIGURED |
                      TIMECARD_SENSOR_FLAG_CONVERSION_READY |
                      TIMECARD_SENSOR_FLAG_HUMIDITY |
                      TIMECARD_SENSOR_FLAG_TEMPERATURE |
                      TIMECARD_SENSOR_FLAG_CRC_VALID;
}

static NTSTATUS
TimeCardIcp10100ReadOtpLocked(PDEVICE_CONTEXT context,
                              ULONG *controllerStatus,
                              ULONG *interruptStatus)
{
    static const UCHAR setPointer[] = { 0xc5u, 0x95u, 0x00u, 0x66u, 0x9cu };
    static const UCHAR incrementPointer[] = { 0xc7u, 0xf7u };
    ULONG index;
    NTSTATUS status;

    if (context->I2cIcp10100OtpValid != 0)
        return STATUS_SUCCESS;
    status = TimeCardI2cWriteLocked(
        context, TIMECARD_SENSOR_ICP10100_ADDRESS,
        setPointer, sizeof(setPointer), controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return status;

    for (index = 0; index < 4u; ++index) {
        UCHAR frame[3];

        status = TimeCardI2cWriteLocked(
            context, TIMECARD_SENSOR_ICP10100_ADDRESS,
            incrementPointer, sizeof(incrementPointer),
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return status;
        status = TimeCardI2cReadBytesLocked(
            context, TIMECARD_SENSOR_ICP10100_ADDRESS, 0u, 0u,
            frame, sizeof(frame), controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return status;
        if (!TimeCardSensorFrameCrcValid(frame))
            return STATUS_CRC_ERROR;
        context->I2cIcp10100Otp[index] =
            (SHORT)(((USHORT)frame[0] << 8) | frame[1]);
    }
    context->I2cIcp10100OtpValid = 1u;
    return STATUS_SUCCESS;
}

static VOID
TimeCardIcp10100ReadLocked(PDEVICE_CONTEXT context,
                           TIMECARD_ICP10100_READING *reading,
                           ULONG *controllerStatus, ULONG *interruptStatus)
{
    static const UCHAR idCommand[] = {
        TIMECARD_ICP10100_READ_ID_HI, TIMECARD_ICP10100_READ_ID_LO
    };
    static const UCHAR measureCommand[] = {
        TIMECARD_ICP10100_MEASURE_HI, TIMECARD_ICP10100_MEASURE_LO
    };
    UCHAR idFrame[3];
    UCHAR data[9];
    ULONG index;
    NTSTATUS status;

    RtlZeroMemory(reading, sizeof(*reading));
    reading->Size = sizeof(*reading);
    reading->Address = TIMECARD_SENSOR_ICP10100_ADDRESS;
    status = TimeCardI2cWriteLocked(
        context, reading->Address, idCommand, sizeof(idCommand),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    reading->Flags = TIMECARD_SENSOR_FLAG_PRESENT;
    TimeCardSensorDelayMilliseconds(1u);
    status = TimeCardI2cReadBytesLocked(
        context, reading->Address, 0u, 0u, idFrame, sizeof(idFrame),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status) || !TimeCardSensorFrameCrcValid(idFrame))
        return;
    reading->ProductId =
        ((((ULONG)idFrame[0] << 8) | idFrame[1]) & 0x3fu);
    if (reading->ProductId != TIMECARD_ICP10100_PRODUCT_ID)
        return;

    status = TimeCardIcp10100ReadOtpLocked(
        context, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    for (index = 0; index < 4u; ++index)
        reading->Otp[index] = context->I2cIcp10100Otp[index];

    status = TimeCardI2cWriteLocked(
        context, reading->Address, measureCommand, sizeof(measureCommand),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    TimeCardSensorDelayMilliseconds(30u);
    status = TimeCardI2cReadBytesLocked(
        context, reading->Address, 0u, 0u, data, sizeof(data),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status) || !TimeCardSensorFrameCrcValid(&data[0]) ||
        !TimeCardSensorFrameCrcValid(&data[3]) ||
        !TimeCardSensorFrameCrcValid(&data[6])) {
        return;
    }

    reading->RawPressure = ((ULONG)data[0] << 16) |
                           ((ULONG)data[1] << 8) | data[3];
    reading->RawTemperature = ((ULONG)data[6] << 8) | data[7];
    reading->TemperatureMilliCelsius = -45000l + (LONG)(
        (175000ll * reading->RawTemperature) / 65536ll);
    reading->Flags |= TIMECARD_SENSOR_FLAG_VALID |
                      TIMECARD_SENSOR_FLAG_CONFIGURED |
                      TIMECARD_SENSOR_FLAG_CONVERSION_READY |
                      TIMECARD_SENSOR_FLAG_TEMPERATURE |
                      TIMECARD_SENSOR_FLAG_CRC_VALID;
}

static NTSTATUS
TimeCardLedResolveAddressLocked(PDEVICE_CONTEXT context, ULONG *address,
                                ULONG *controllerStatus,
                                ULONG *interruptStatus)
{
    static const ULONG candidates[] = { 0x37u, 0x36u, 0x35u, 0x34u };
    UCHAR deviceControl;
    UCHAR globalCurrent;
    ULONG i;
    NTSTATUS status = STATUS_NO_SUCH_DEVICE;

    if (context->I2cLedAddress >= TIMECARD_LED_ADDRESS_MIN &&
        context->I2cLedAddress <= TIMECARD_LED_ADDRESS_MAX) {
        status = TimeCardI2cReadBytesLocked(
            context, context->I2cLedAddress, 1, 0x00u,
            &deviceControl, 1, controllerStatus, interruptStatus);
        if (NT_SUCCESS(status)) {
            *address = context->I2cLedAddress;
            return STATUS_SUCCESS;
        }
        context->I2cLedAddress = 0;
    }

    for (i = 0; i < ARRAYSIZE(candidates); ++i) {
        status = TimeCardI2cReadBytesLocked(
            context, candidates[i], 1, 0x00u, &deviceControl, 1,
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            continue;
        status = TimeCardI2cReadBytesLocked(
            context, candidates[i], 1, 0x6eu, &globalCurrent, 1,
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            continue;
        /*
         * D7 and D3 are reserved.  Do not reject a chip merely because a
         * boot loader selected a different legal oscillator/PWM mode or a
         * global-current value above the Control Center's safety cap.
         */
        if ((deviceControl & 0x88u) != 0) {
            continue;
        }
        context->I2cLedAddress = candidates[i];
        *address = candidates[i];
        return STATUS_SUCCESS;
    }
    return status;
}

static NTSTATUS
TimeCardEnvironmentResolveAddressLocked(PDEVICE_CONTEXT context,
                                        ULONG *address, UCHAR *chipId,
                                        ULONG *controllerStatus,
                                        ULONG *interruptStatus)
{
    static const ULONG candidates[] = {
        TIMECARD_SENSOR_BME280_ADDRESS,
        TIMECARD_SENSOR_BME280_ADDRESS_ALTERNATE
    };
    ULONG i;
    NTSTATUS status = STATUS_NO_SUCH_DEVICE;

    if (context->I2cEnvironmentAddress != 0) {
        status = TimeCardI2cReadBytesLocked(
            context, context->I2cEnvironmentAddress, 1, 0xd0u,
            chipId, 1, controllerStatus, interruptStatus);
        if (NT_SUCCESS(status) &&
            (*chipId == 0x60u || *chipId == 0x58u)) {
            *address = context->I2cEnvironmentAddress;
            return STATUS_SUCCESS;
        }
        context->I2cEnvironmentAddress = 0;
    }

    for (i = 0; i < ARRAYSIZE(candidates); ++i) {
        status = TimeCardI2cReadBytesLocked(
            context, candidates[i], 1, 0xd0u, chipId, 1,
            controllerStatus, interruptStatus);
        if (NT_SUCCESS(status) &&
            (*chipId == 0x60u || *chipId == 0x58u)) {
            context->I2cEnvironmentAddress = candidates[i];
            *address = candidates[i];
            return STATUS_SUCCESS;
        }
    }
    return status;
}

static NTSTATUS
TimeCardImuResolveAddressLocked(PDEVICE_CONTEXT context, ULONG *address,
                                UCHAR *chipId, ULONG *controllerStatus,
                                ULONG *interruptStatus)
{
    static const ULONG candidates[] = {
        TIMECARD_SENSOR_BNO055_ADDRESS,
        TIMECARD_SENSOR_BNO055_ADDRESS_ALTERNATE
    };
    ULONG i;
    NTSTATUS status = STATUS_NO_SUCH_DEVICE;

    if (context->I2cImuAddress != 0) {
        status = TimeCardI2cReadBytesLocked(
            context, context->I2cImuAddress, 1, 0x00u,
            chipId, 1, controllerStatus, interruptStatus);
        if (NT_SUCCESS(status) && *chipId == 0xa0u) {
            *address = context->I2cImuAddress;
            return STATUS_SUCCESS;
        }
        context->I2cImuAddress = 0;
    }

    for (i = 0; i < ARRAYSIZE(candidates); ++i) {
        status = TimeCardI2cReadBytesLocked(
            context, candidates[i], 1, 0x00u, chipId, 1,
            controllerStatus, interruptStatus);
        if (NT_SUCCESS(status) && *chipId == 0xa0u) {
            context->I2cImuAddress = candidates[i];
            *address = candidates[i];
            return STATUS_SUCCESS;
        }
    }
    return status;
}

static BOOLEAN
TimeCardLedUsesLegacyWiring(PDEVICE_CONTEXT context)
{
    /*
     * The Rev00/MSI assembly routes the six RGB packages differently from
     * the V9 Rev02/MSI-X schematic and exchanges each package's R/G sinks.
     * Keep the V9 mapping unchanged for the original card.
     */
    return context->BoardProfile == TIMECARD_BOARD_FB &&
           context->Layout == TIMECARD_LAYOUT_MSI;
}

static BOOLEAN
TimeCardLedSwapsRedGreen(PDEVICE_CONTEXT context)
{
    /* Celestica drives green, red, blue on each consecutive output group. */
    return TimeCardLedUsesLegacyWiring(context) ||
           context->BoardProfile == TIMECARD_BOARD_CELESTICA;
}

static BOOLEAN
TimeCardLedIsFitted(PDEVICE_CONTEXT context, ULONG logicalLed)
{
    /* R4006 has one GPS indicator and four SMA indicators; OUT16-18 are NC. */
    return context->BoardProfile != TIMECARD_BOARD_CELESTICA ||
           logicalLed != TIMECARD_LED_GNSS2;
}

static ULONG
TimeCardLedPhysicalIndex(PDEVICE_CONTEXT context, ULONG logicalLed)
{
    static const UCHAR legacyMap[TIMECARD_LED_COUNT] = {
        TIMECARD_LED_IO3,
        TIMECARD_LED_IO4,
        TIMECARD_LED_IO1,
        TIMECARD_LED_IO2,
        TIMECARD_LED_GNSS1,
        TIMECARD_LED_GNSS2
    };
    static const UCHAR celesticaMap[TIMECARD_LED_COUNT] = {
        4u, 5u, 0u, 1u, 2u, 3u
    };

    return context->BoardProfile == TIMECARD_BOARD_CELESTICA ?
        celesticaMap[logicalLed] : TimeCardLedUsesLegacyWiring(context) ?
            legacyMap[logicalLed] : logicalLed;
}

static ULONG
TimeCardLedFaultMask(const UCHAR *data)
{
    return ((ULONG)data[0] | ((ULONG)data[1] << 8) |
            ((ULONG)(data[2] & 0x03u) << 16)) &
           TIMECARD_LED_OUTPUT_MASK;
}

static NTSTATUS
TimeCardLedDetectFaultsLocked(PDEVICE_CONTEXT context, ULONG address,
                              ULONG *openMask,
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
        context, address, writeBuffer, sizeof(writeBuffer),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        goto Disable;
    TimeCardSensorDelayMilliseconds(1u);
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x72u, result, sizeof(result),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        goto Disable;
    *openMask = TimeCardLedFaultMask(result);
    if (context->BoardProfile == TIMECARD_BOARD_CELESTICA)
        *openMask &= (1u << 15) - 1u;

    /* OSDE=10 selects short detection and replaces the result registers. */
    writeBuffer[1] = 0x02u;
    status = TimeCardI2cWriteLocked(
        context, address, writeBuffer, sizeof(writeBuffer),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        goto Disable;
    TimeCardSensorDelayMilliseconds(1u);
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x72u, result, sizeof(result),
        controllerStatus, interruptStatus);
    if (NT_SUCCESS(status))
        *shortMask = TimeCardLedFaultMask(result);
    if (context->BoardProfile == TIMECARD_BOARD_CELESTICA)
        *shortMask &= (1u << 15) - 1u;

Disable:
    writeBuffer[1] = 0x00u;
    disableStatus = TimeCardI2cWriteLocked(
        context, address, writeBuffer, sizeof(writeBuffer),
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
    ULONG address = 0;
    ULONG physicalLed;
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
    if (!TimeCardLedIsFitted(context, led))
        return STATUS_SUCCESS;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardSensorBranchSelectLocked(
        context, &savedMask, &changed,
        &controllerStatus, &interruptStatus);
    control->MuxChannelMask = savedMask;
    if (!NT_SUCCESS(status))
        goto Exit;

    status = TimeCardLedResolveAddressLocked(
        context, &address, &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    physicalLed = TimeCardLedPhysicalIndex(context, led);

    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x00u, &deviceControl, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x6eu, &globalCurrent, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x78u, &spreadSpectrum, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x01u + physicalLed * 6u, pwm,
        sizeof(pwm),
        &controllerStatus, &interruptStatus);
    if (NT_SUCCESS(status)) {
        control->Flags = TIMECARD_LED_FLAG_PRESENT;
        if ((deviceControl & 0x01u) != 0)
            control->Flags |= TIMECARD_LED_FLAG_ENABLED;
        if ((spreadSpectrum & 0x60u) == 0x60u)
            control->Flags |= TIMECARD_LED_FLAG_DC_TEST;
        if (TimeCardLedSwapsRedGreen(context)) {
            control->Red = pwm[2];
            control->Green = pwm[0];
        } else {
            control->Red = pwm[0];
            control->Green = pwm[2];
        }
        control->Blue = pwm[4];
        control->GlobalCurrent = globalCurrent;
        faultStatus = TimeCardLedDetectFaultsLocked(
            context, address, &openMask, &shortMask,
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
    UCHAR readControl = 0;
    UCHAR readCurrent = 0;
    UCHAR readScaling[3];
    UCHAR readPwm[6];
    UCHAR readSpread = 0;
    UCHAR expectedSpread;
    UCHAR resetProbe = 0;
    BOOLEAN sdbHigh = FALSE;
    ULONG address = 0;
    ULONG physicalLed;
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
    if (!TimeCardLedIsFitted(context, request->Led))
        return STATUS_SUCCESS;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardSensorBranchSelectLocked(
        context, &savedMask, &changed,
        &controllerStatus, &interruptStatus);
    response->MuxChannelMask = savedMask;
    if (!NT_SUCCESS(status))
        goto Exit;

    status = TimeCardLedResolveAddressLocked(
        context, &address, &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    physicalLed = TimeCardLedPhysicalIndex(context, request->Led);

    if ((request->Flags & TIMECARD_LED_FLAG_RESET_TEST) != 0) {
        /*
         * The reset command is honored only while SDB is physically high
         * and SSD is set.  Use a benign non-default phase value as a probe,
         * then restore normal operation before applying the requested LED.
         */
        writeBuffer[0] = 0x00u;
        writeBuffer[1] = 0x01u;
        status = TimeCardI2cWriteLocked(
            context, address, writeBuffer, 2,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        writeBuffer[0] = 0x70u;
        writeBuffer[1] = 0x05u;
        status = TimeCardI2cWriteLocked(
            context, address, writeBuffer, 2,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        status = TimeCardI2cReadBytesLocked(
            context, address, 1, 0x70u, &resetProbe, 1,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status) || resetProbe != 0x05u) {
            if (NT_SUCCESS(status))
                status = STATUS_DEVICE_DATA_ERROR;
            goto Exit;
        }
        writeBuffer[0] = 0x7fu;
        writeBuffer[1] = 0x00u;
        status = TimeCardI2cWriteLocked(
            context, address, writeBuffer, 2,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        status = TimeCardI2cReadBytesLocked(
            context, address, 1, 0x70u, &resetProbe, 1,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        sdbHigh = resetProbe == 0x00u;

    }

    /* 8-bit PWM mode, normal operation.  This yields flicker-free PWM. */
    writeBuffer[0] = 0x00u;
    writeBuffer[1] = 0x01u;
    status = TimeCardI2cWriteLocked(
        context, address, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    writeBuffer[0] = 0x6eu;
    writeBuffer[1] = (UCHAR)request->GlobalCurrent;
    status = TimeCardI2cWriteLocked(
        context, address, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    /* Apply full per-channel scaling; global current remains safety-capped. */
    writeBuffer[0] = (UCHAR)(0x4au + physicalLed * 3u);
    writeBuffer[1] = 0xffu;
    writeBuffer[2] = 0xffu;
    writeBuffer[3] = 0xffu;
    status = TimeCardI2cWriteLocked(
        context, address, writeBuffer, 4,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    writeBuffer[0] = (UCHAR)(0x01u + physicalLed * 6u);
    writeBuffer[1] = TimeCardLedSwapsRedGreen(context) ?
        (UCHAR)request->Green : (UCHAR)request->Red;
    writeBuffer[2] = 0;
    writeBuffer[3] = TimeCardLedSwapsRedGreen(context) ?
        (UCHAR)request->Red : (UCHAR)request->Green;
    writeBuffer[4] = 0;
    writeBuffer[5] = (UCHAR)request->Blue;
    writeBuffer[6] = 0;
    status = TimeCardI2cWriteLocked(
        context, address, writeBuffer, sizeof(writeBuffer),
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    writeBuffer[0] = 0x49u;
    writeBuffer[1] = 0;
    status = TimeCardI2cWriteLocked(
        context, address, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    /*
     * DCPWM is a guarded electrical diagnostic.  0x60 forces OUT1-18 on
     * independently of the PWM/update path; normal requests explicitly
     * return the device to PWM mode.
    */
    writeBuffer[0] = 0x78u;
    expectedSpread =
        (request->Flags & TIMECARD_LED_FLAG_DC_TEST) != 0 ? 0x60u : 0x00u;
    writeBuffer[1] = expectedSpread;
    status = TimeCardI2cWriteLocked(
        context, address, writeBuffer, 2,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;

    /*
     * Do not report a successful LED operation unless the IS32FL3207
     * actually retained every setting that controls visible output.
     */
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x00u, &readControl, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x6eu, &readCurrent, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x4au + physicalLed * 3u,
        readScaling, sizeof(readScaling),
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x01u + physicalLed * 6u,
        readPwm, sizeof(readPwm),
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x78u, &readSpread, 1,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    if ((readControl & 0x01u) == 0 ||
        readCurrent != (UCHAR)request->GlobalCurrent ||
        readScaling[0] != 0xffu || readScaling[1] != 0xffu ||
        readScaling[2] != 0xffu ||
        readPwm[0] != (TimeCardLedSwapsRedGreen(context) ?
            (UCHAR)request->Green : (UCHAR)request->Red) ||
        readPwm[1] != 0 ||
        readPwm[2] != (TimeCardLedSwapsRedGreen(context) ?
            (UCHAR)request->Red : (UCHAR)request->Green) ||
        readPwm[3] != 0 ||
        readPwm[4] != (UCHAR)request->Blue || readPwm[5] != 0 ||
        (readSpread & 0x60u) != expectedSpread) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto Exit;
    }

    if (NT_SUCCESS(status)) {
        response->Flags = TIMECARD_LED_FLAG_PRESENT |
                          TIMECARD_LED_FLAG_ENABLED;
        if ((request->Flags & TIMECARD_LED_FLAG_DC_TEST) != 0)
            response->Flags |= TIMECARD_LED_FLAG_DC_TEST;
        if ((request->Flags & TIMECARD_LED_FLAG_RESET_TEST) != 0)
            response->Flags |= TIMECARD_LED_FLAG_RESET_TEST;
        if (sdbHigh)
            response->Flags |= TIMECARD_LED_FLAG_SDB_HIGH;
        if (TimeCardLedSwapsRedGreen(context)) {
            response->Red = readPwm[2];
            response->Green = readPwm[0];
        } else {
            response->Red = readPwm[0];
            response->Green = readPwm[2];
        }
        response->Blue = readPwm[4];
        response->GlobalCurrent = readCurrent;
        faultStatus = TimeCardLedDetectFaultsLocked(
            context, address, &openMask, &shortMask,
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
    ULONG address = 0;
    ULONG dataLength;
    ULONG index;
    NTSTATUS status;

    reading->Size = sizeof(*reading);
    status = TimeCardEnvironmentResolveAddressLocked(
        context, &address, &chipId, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    reading->ChipId = chipId;
    reading->Flags = TIMECARD_SENSOR_FLAG_PRESENT;
    if (chipId == 0x60u)
        reading->Flags |= TIMECARD_SENSOR_FLAG_HUMIDITY;

    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x88u,
        calibration1, sizeof(calibration1),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    if (chipId == 0x60u) {
        status = TimeCardI2cReadBytesLocked(
            context, address, 1, 0xe1u,
            calibration2, sizeof(calibration2),
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
    } else {
        RtlZeroMemory(calibration2, sizeof(calibration2));
    }

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
    if (chipId == 0x60u) {
        reading->DigH1 = calibration1[25];
        reading->DigH2 = TimeCardSensorReadLeSigned16(&calibration2[0]);
        reading->DigH3 = calibration2[2];
        reading->DigH4 = (LONG)(CHAR)calibration2[3] * 16L +
                         (LONG)(calibration2[4] & 0x0fu);
        reading->DigH5 = (LONG)(CHAR)calibration2[5] * 16L +
                         (LONG)(calibration2[4] >> 4);
        reading->DigH6 = (LONG)(CHAR)calibration2[6];
    }

    /* Temperature x2, pressure x4, plus humidity x1 when BME280 is fitted. */
    if (chipId == 0x60u) {
        writeBuffer[0] = 0xf2u;
        writeBuffer[1] = 0x01u;
        status = TimeCardI2cWriteLocked(
            context, address, writeBuffer, 2,
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
    }
    writeBuffer[0] = 0xf4u;
    writeBuffer[1] = 0x4du;
    status = TimeCardI2cWriteLocked(
        context, address, writeBuffer, 2,
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    reading->Flags |= TIMECARD_SENSOR_FLAG_CONFIGURED;

    for (index = 0; index < 20u; ++index) {
        TimeCardSensorDelayMilliseconds(5u);
        status = TimeCardI2cReadBytesLocked(
            context, address, 1, 0xf3u,
            &statusByte, 1, controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        if ((statusByte & 0x08u) == 0)
            break;
    }
    reading->Status = statusByte;
    if ((statusByte & 0x08u) == 0)
        reading->Flags |= TIMECARD_SENSOR_FLAG_CONVERSION_READY;

    dataLength = chipId == 0x60u ? sizeof(data) : 6u;
    RtlZeroMemory(data, sizeof(data));
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0xf7u,
        data, dataLength, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    reading->RawPressure = ((ULONG)data[0] << 12) |
                           ((ULONG)data[1] << 4) |
                           ((ULONG)data[2] >> 4);
    reading->RawTemperature = (LONG)(((ULONG)data[3] << 12) |
                                    ((ULONG)data[4] << 4) |
                                    ((ULONG)data[5] >> 4));
    if (chipId == 0x60u)
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
    UCHAR writeBuffer[3];
    USHORT busRaw;
    USHORT configuration;
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
    configuration = TimeCardSensorReadBe16(data);

    /*
     * Some Time Card assemblies leave INA219 in its reset/power-down state.
     * Program the same safe 32 V, +/-320 mV, 12-bit continuous bus+shunt
     * configuration used by the device default before sampling.  This does
     * not use the calibration/current registers; current remains derived from
     * the schematic's 2 milliohm shunt voltage.
     */
    if (configuration != TIMECARD_INA219_CONFIGURATION) {
        writeBuffer[0] = 0x00u;
        writeBuffer[1] =
            (UCHAR)(TIMECARD_INA219_CONFIGURATION >> 8);
        writeBuffer[2] =
            (UCHAR)(TIMECARD_INA219_CONFIGURATION & 0xffu);
        status = TimeCardI2cWriteLocked(
            context, address, writeBuffer, sizeof(writeBuffer),
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        TimeCardSensorDelayMilliseconds(2u);
        status = TimeCardI2cReadBytesLocked(
            context, address, 1, 0x00u, data, 2,
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        configuration = TimeCardSensorReadBe16(data);
        if (configuration != TIMECARD_INA219_CONFIGURATION)
            return;
    }
    reading->Configuration = configuration;
    reading->Flags |= TIMECARD_SENSOR_FLAG_CONFIGURED;
    TimeCardSensorDelayMilliseconds(2u);
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
    reading->Flags |= TIMECARD_SENSOR_FLAG_VALID;
    if ((busRaw & 0x0002u) != 0)
        reading->Flags |= TIMECARD_SENSOR_FLAG_CONVERSION_READY;
    if ((busRaw & 0x0001u) != 0)
        reading->Flags |= TIMECARD_SENSOR_FLAG_OVERFLOW;
}

static LONG
TimeCardBno08xQ8ToCenti(SHORT value)
{
    LONG scaled = (LONG)value * 100L;

    scaled += scaled >= 0 ? 128L : -128L;
    return scaled / 256L;
}

static LONG
TimeCardBno08xQ9ToDegrees16(SHORT value)
{
    /*
     * BNO08x calibrated gyro is Q9 radians/second.  The existing ABI carries
     * BNO055-compatible degrees/second at 16 LSB/degree, so convert using
     * 16 * 180 / (pi * 512), rounded with a fixed-point rational.
     */
    LONGLONG scaled = (LONGLONG)value * 17905ll;

    scaled += scaled >= 0 ? 5000ll : -5000ll;
    return (LONG)(scaled / 10000ll);
}

static NTSTATUS
TimeCardBno08xResolveAddressLocked(PDEVICE_CONTEXT context, ULONG *address,
                                   ULONG *controllerStatus,
                                   ULONG *interruptStatus)
{
    static const ULONG candidates[] = {
        TIMECARD_SENSOR_BNO08X_ADDRESS,
        TIMECARD_SENSOR_BNO08X_ADDRESS_ALTERNATE
    };
    UCHAR header[4];
    ULONG i;
    NTSTATUS status = STATUS_NO_SUCH_DEVICE;

    if (context->I2cImuType == TIMECARD_IMU_TYPE_BNO08X &&
        (context->I2cImuAddress == TIMECARD_SENSOR_BNO08X_ADDRESS ||
         context->I2cImuAddress ==
             TIMECARD_SENSOR_BNO08X_ADDRESS_ALTERNATE)) {
        *address = context->I2cImuAddress;
        return STATUS_SUCCESS;
    }

    for (i = 0; i < ARRAYSIZE(candidates); ++i) {
        status = TimeCardI2cReadBytesLocked(
            context, candidates[i], 0, 0, header, sizeof(header),
            controllerStatus, interruptStatus);
        if (NT_SUCCESS(status) && TimeCardBno08xHeaderValid(header)) {
            context->I2cImuType = TIMECARD_IMU_TYPE_BNO08X;
            context->I2cImuAddress = candidates[i];
            *address = candidates[i];
            return STATUS_SUCCESS;
        }
    }
    return status;
}

static NTSTATUS
TimeCardBno08xReadTransferLocked(PDEVICE_CONTEXT context, ULONG address,
                                 UCHAR *data, ULONG *dataLength,
                                 ULONG *controllerStatus,
                                 ULONG *interruptStatus)
{
    UCHAR header[4];
    ULONG length;
    NTSTATUS status;

    *dataLength = 0;
    status = TimeCardI2cReadBytesLocked(
        context, address, 0, 0, header, sizeof(header),
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return status;
    if (!TimeCardBno08xHeaderValid(header))
        return STATUS_DEVICE_DATA_ERROR;

    length = (ULONG)header[0] | (((ULONG)header[1] & 0x7fu) << 8);
    if (length > TIMECARD_I2C_MAX_TRANSFER)
        length = TIMECARD_I2C_MAX_TRANSFER;
    status = TimeCardI2cReadBytesLocked(
        context, address, 0, 0, data, length,
        controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return status;
    if (length < 4u || !TimeCardBno08xHeaderValid(data))
        return STATUS_DEVICE_DATA_ERROR;
    *dataLength = length;
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardBno08xSetFeatureLocked(PDEVICE_CONTEXT context, ULONG address,
                               UCHAR reportId, ULONG intervalMicroseconds,
                               ULONG *controllerStatus,
                               ULONG *interruptStatus)
{
    UCHAR packet[21];

    RtlZeroMemory(packet, sizeof(packet));
    packet[0] = (UCHAR)sizeof(packet);
    packet[1] = 0;
    packet[2] = TIMECARD_BNO08X_CHANNEL_CONTROL;
    packet[3] = (UCHAR)context->I2cBno08xSequence++;
    packet[4] = TIMECARD_BNO08X_SET_FEATURE;
    packet[5] = reportId;
    packet[9] = (UCHAR)(intervalMicroseconds & 0xffu);
    packet[10] = (UCHAR)((intervalMicroseconds >> 8) & 0xffu);
    packet[11] = (UCHAR)((intervalMicroseconds >> 16) & 0xffu);
    packet[12] = (UCHAR)((intervalMicroseconds >> 24) & 0xffu);
    return TimeCardI2cWriteLocked(
        context, address, packet, sizeof(packet),
        controllerStatus, interruptStatus);
}

static NTSTATUS
TimeCardBno08xConfigureLocked(PDEVICE_CONTEXT context, ULONG address,
                              ULONG *controllerStatus,
                              ULONG *interruptStatus)
{
    static const UCHAR reports[] = {
        TIMECARD_BNO08X_ACCELEROMETER,
        TIMECARD_BNO08X_GYROSCOPE,
        TIMECARD_BNO08X_MAGNETIC_FIELD,
        TIMECARD_BNO08X_LINEAR_ACCELERATION,
        TIMECARD_BNO08X_ROTATION_VECTOR,
        TIMECARD_BNO08X_GRAVITY,
        TIMECARD_BNO08X_TEMPERATURE
    };
    UCHAR discard[TIMECARD_I2C_MAX_TRANSFER];
    ULONG discardLength;
    ULONG i;
    NTSTATUS status;

    /*
     * Remove boot advertisements and reset notifications so the first live
     * sample does not sit behind a multi-fragment SHTP descriptor.
     */
    for (i = 0; i < 12u; ++i) {
        status = TimeCardBno08xReadTransferLocked(
            context, address, discard, &discardLength,
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            break;
    }

    for (i = 0; i < ARRAYSIZE(reports); ++i) {
        status = TimeCardBno08xSetFeatureLocked(
            context, address, reports[i],
            reports[i] == TIMECARD_BNO08X_TEMPERATURE ?
                1000000u : TIMECARD_BNO08X_REPORT_INTERVAL_US,
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return status;
        TimeCardSensorDelayMilliseconds(1u);
    }
    context->I2cBno08xConfigured = 1;
    TimeCardSensorDelayMilliseconds(300u);
    return STATUS_SUCCESS;
}

static VOID
TimeCardBno08xParseReports(TIMECARD_BNO055_READING *reading,
                           const UCHAR *payload, ULONG length)
{
    ULONG cursor = 0;
    BOOLEAN valid = FALSE;

    while (cursor < length) {
        UCHAR reportId = payload[cursor];
        ULONG reportLength;
        ULONG accuracy;

        if (reportId == TIMECARD_BNO08X_BASE_TIMESTAMP)
            reportLength = 5u;
        else if (reportId == TIMECARD_BNO08X_ROTATION_VECTOR)
            reportLength = 14u;
        else if (reportId == TIMECARD_BNO08X_TEMPERATURE)
            reportLength = 6u;
        else if (reportId >= TIMECARD_BNO08X_ACCELEROMETER &&
                 reportId <= TIMECARD_BNO08X_GRAVITY)
            reportLength = 10u;
        else
            break;
        if (cursor + reportLength > length)
            break;
        if (reportId == TIMECARD_BNO08X_BASE_TIMESTAMP) {
            cursor += reportLength;
            continue;
        }

        accuracy = payload[cursor + 2u] & 0x03u;
        if (reportId == TIMECARD_BNO08X_ACCELEROMETER) {
            reading->AccelerationX = TimeCardBno08xQ8ToCenti(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 4u]));
            reading->AccelerationY = TimeCardBno08xQ8ToCenti(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 6u]));
            reading->AccelerationZ = TimeCardBno08xQ8ToCenti(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 8u]));
            reading->Calibration =
                (reading->Calibration & ~(3u << 2)) | (accuracy << 2);
            valid = TRUE;
        } else if (reportId == TIMECARD_BNO08X_GYROSCOPE) {
            reading->GyroscopeX = TimeCardBno08xQ9ToDegrees16(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 4u]));
            reading->GyroscopeY = TimeCardBno08xQ9ToDegrees16(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 6u]));
            reading->GyroscopeZ = TimeCardBno08xQ9ToDegrees16(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 8u]));
            reading->Calibration =
                (reading->Calibration & ~(3u << 4)) | (accuracy << 4);
            valid = TRUE;
        } else if (reportId == TIMECARD_BNO08X_MAGNETIC_FIELD) {
            reading->MagneticX = TimeCardSensorReadLeSigned16(
                &payload[cursor + 4u]);
            reading->MagneticY = TimeCardSensorReadLeSigned16(
                &payload[cursor + 6u]);
            reading->MagneticZ = TimeCardSensorReadLeSigned16(
                &payload[cursor + 8u]);
            reading->Calibration =
                (reading->Calibration & ~3u) | accuracy;
            valid = TRUE;
        } else if (reportId == TIMECARD_BNO08X_LINEAR_ACCELERATION) {
            reading->LinearAccelerationX = TimeCardBno08xQ8ToCenti(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 4u]));
            reading->LinearAccelerationY = TimeCardBno08xQ8ToCenti(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 6u]));
            reading->LinearAccelerationZ = TimeCardBno08xQ8ToCenti(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 8u]));
            valid = TRUE;
        } else if (reportId == TIMECARD_BNO08X_ROTATION_VECTOR) {
            reading->QuaternionX = TimeCardSensorReadLeSigned16(
                &payload[cursor + 4u]);
            reading->QuaternionY = TimeCardSensorReadLeSigned16(
                &payload[cursor + 6u]);
            reading->QuaternionZ = TimeCardSensorReadLeSigned16(
                &payload[cursor + 8u]);
            reading->QuaternionW = TimeCardSensorReadLeSigned16(
                &payload[cursor + 10u]);
            reading->Calibration =
                (reading->Calibration & ~(3u << 6)) | (accuracy << 6);
            valid = TRUE;
        } else if (reportId == TIMECARD_BNO08X_GRAVITY) {
            reading->GravityX = TimeCardBno08xQ8ToCenti(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 4u]));
            reading->GravityY = TimeCardBno08xQ8ToCenti(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 6u]));
            reading->GravityZ = TimeCardBno08xQ8ToCenti(
                (SHORT)TimeCardSensorReadLe16(&payload[cursor + 8u]));
            valid = TRUE;
        } else if (reportId == TIMECARD_BNO08X_TEMPERATURE) {
            /*
             * SH-2 temperature report 0x0e is signed Q7 degrees Celsius.
             * Whether it is implemented depends on the BNO08x firmware and
             * its environmental-sensor configuration.
             */
            reading->Temperature = TimeCardSensorReadLeSigned16(
                &payload[cursor + 4u]);
            reading->Flags |= TIMECARD_SENSOR_FLAG_TEMPERATURE |
                              TIMECARD_SENSOR_FLAG_TEMPERATURE_Q7;
            valid = TRUE;
        }
        cursor += reportLength;
    }

    if (valid)
        reading->Flags |= TIMECARD_SENSOR_FLAG_VALID;
}

static BOOLEAN
TimeCardBno08xDrainReportsLocked(PDEVICE_CONTEXT context, ULONG address,
                                 TIMECARD_BNO055_READING *reading,
                                 ULONG *controllerStatus,
                                 ULONG *interruptStatus)
{
    UCHAR packet[TIMECARD_I2C_MAX_TRANSFER];
    ULONG packetLength;
    ULONG i;
    NTSTATUS status;

    for (i = 0; i < 32u; ++i) {
        status = TimeCardBno08xReadTransferLocked(
            context, address, packet, &packetLength,
            controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            break;
        if (packetLength >= 4u &&
            packet[2] == TIMECARD_BNO08X_CHANNEL_REPORTS) {
            /*
             * I2C first returns a four-byte header.  The required second read
             * repeats that header with SHTP's continuation bit set and carries
             * the complete payload, so continuation is expected here.
             */
            TimeCardBno08xParseReports(
                reading, &packet[4], packetLength - 4u);
        }
    }

    return (reading->Flags & TIMECARD_SENSOR_FLAG_VALID) != 0;
}

static VOID
TimeCardBno08xReadLocked(PDEVICE_CONTEXT context,
                         TIMECARD_BNO055_READING *reading,
                         ULONG *controllerStatus, ULONG *interruptStatus)
{
    ULONG address = 0;
    NTSTATUS status;

    reading->Size = sizeof(*reading);
    status = TimeCardBno08xResolveAddressLocked(
        context, &address, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;

    reading->Flags = TIMECARD_SENSOR_FLAG_PRESENT;
    reading->ChipId = TIMECARD_SENSOR_BNO08X_CHIP_ID;
    reading->OperationMode = TIMECARD_BNO08X_CHANNEL_REPORTS;
    reading->UnitSelection = 0;
    reading->SystemStatus = 1;
    reading->SystemError = 0;

    if (context->I2cBno08xConfigured == 0) {
        status = TimeCardBno08xConfigureLocked(
            context, address, controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
    }
    reading->Flags |= TIMECARD_SENSOR_FLAG_CONFIGURED;

    if (TimeCardBno08xDrainReportsLocked(
            context, address, reading,
            controllerStatus, interruptStatus))
        return;

    /*
     * A BNO08x can reset internally or stop delivering reports after its
     * finite output queue fills while the mux branch is not being sampled.
     * The sensor still acknowledges at 0x4a/0x4b in that state, so presence
     * alone is not a sufficient liveness check.  Re-establish all SH-2
     * feature subscriptions and retry immediately instead of leaving the
     * Control Center permanently stuck at INITIALIZING until a driver reload.
     *
     * Reset the host control-channel sequence as well: after an IMU reset the
     * sensor's SHTP receiver has also returned to its initial sequence state.
     */
    context->I2cBno08xConfigured = 0;
    context->I2cBno08xSequence = 0;
    reading->Flags &= ~TIMECARD_SENSOR_FLAG_CONFIGURED;
    TimeCardSensorDelayMilliseconds(10u);
    status = TimeCardBno08xConfigureLocked(
        context, address, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;

    reading->Flags |= TIMECARD_SENSOR_FLAG_CONFIGURED;
    if (!TimeCardBno08xDrainReportsLocked(
            context, address, reading,
            controllerStatus, interruptStatus)) {
        /*
         * Keep the watchdog armed.  A sensor that is still completing a boot
         * will be configured and sampled again on the next telemetry query.
         */
        context->I2cBno08xConfigured = 0;
        reading->Flags &= ~TIMECARD_SENSOR_FLAG_CONFIGURED;
    }
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
    ULONG address = 0;
    NTSTATUS status;

    reading->Size = sizeof(*reading);
    status = TimeCardImuResolveAddressLocked(
        context, &address, &chipId, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    reading->ChipId = chipId;
    reading->Flags = TIMECARD_SENSOR_FLAG_PRESENT;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x3du,
        &operationMode, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    operationMode &= 0x0fu;

    if (operationMode != 0x0cu) {
        if (operationMode != 0) {
            writeBuffer[0] = 0x3du;
            writeBuffer[1] = 0x00u;
            status = TimeCardI2cWriteLocked(
                context, address,
                writeBuffer, 2, controllerStatus, interruptStatus);
            if (!NT_SUCCESS(status))
                return;
            TimeCardSensorDelayMilliseconds(20u);
        }
        /* Celsius, degrees, dps, m/s2, and Android orientation convention. */
        writeBuffer[0] = 0x3bu;
        writeBuffer[1] = 0x80u;
        status = TimeCardI2cWriteLocked(
            context, address,
            writeBuffer, 2, controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        /* V9 fits Y1, so select the external 32.768 kHz timebase. */
        writeBuffer[0] = 0x3fu;
        writeBuffer[1] = 0x80u;
        status = TimeCardI2cWriteLocked(
            context, address,
            writeBuffer, 2, controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        TimeCardSensorDelayMilliseconds(650u);
        writeBuffer[0] = 0x3du;
        writeBuffer[1] = 0x0cu;
        status = TimeCardI2cWriteLocked(
            context, address,
            writeBuffer, 2, controllerStatus, interruptStatus);
        if (!NT_SUCCESS(status))
            return;
        TimeCardSensorDelayMilliseconds(10u);
        operationMode = 0x0cu;
    }

    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x08u,
        data, sizeof(data), controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x35u,
        &calibration, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x36u,
        &selfTest, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x38u,
        &clockStatus, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x39u,
        &systemStatus, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x3au,
        &systemError, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x3bu,
        &unitSelection, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x3eu,
        &powerMode, 1, controllerStatus, interruptStatus);
    if (!NT_SUCCESS(status))
        return;
    status = TimeCardI2cReadBytesLocked(
        context, address, 1, 0x3fu,
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
                      TIMECARD_SENSOR_FLAG_CONVERSION_READY |
                      TIMECARD_SENSOR_FLAG_TEMPERATURE;
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
    UCHAR imuMask = TIMECARD_I2C_MUX_CHANNEL_SENSORS;
    UCHAR environmentMask;
    UCHAR powerMask;
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
    telemetry->BoardProfile = context->BoardProfile;
    telemetry->BoardTemperature[0].Size =
        sizeof(telemetry->BoardTemperature[0]);
    telemetry->BoardTemperature[0].Address =
        TIMECARD_SENSOR_LM75B_1_ADDRESS;
    telemetry->BoardTemperature[1].Size =
        sizeof(telemetry->BoardTemperature[1]);
    telemetry->BoardTemperature[1].Address =
        TIMECARD_SENSOR_LM75B_2_ADDRESS;
    telemetry->BoardTemperature[2].Size =
        sizeof(telemetry->BoardTemperature[2]);
    telemetry->BoardTemperature[2].Address =
        TIMECARD_SENSOR_LM75B_3_ADDRESS;
    telemetry->Humidity.Size = sizeof(telemetry->Humidity);
    telemetry->Humidity.Address = TIMECARD_SENSOR_SHT3X_ADDRESS;
    telemetry->Pressure.Size = sizeof(telemetry->Pressure);
    telemetry->Pressure.Address = TIMECARD_SENSOR_ICP10100_ADDRESS;
    if (context->BoardProfile == TIMECARD_BOARD_CELESTICA) {
        telemetry->Capabilities =
            TIMECARD_SENSOR_CAP_LM75B | TIMECARD_SENSOR_CAP_SHT3X |
            TIMECARD_SENSOR_CAP_ICP10100 | TIMECARD_SENSOR_CAP_BNO08X;
    } else if (context->BoardProfile == TIMECARD_BOARD_ART) {
        telemetry->Capabilities = 0u;
    } else {
        telemetry->Capabilities =
            TIMECARD_SENSOR_CAP_BME280 | TIMECARD_SENSOR_CAP_INA219 |
            TIMECARD_SENSOR_CAP_BNO055 | TIMECARD_SENSOR_CAP_BNO08X;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    if (context->BoardProfile == TIMECARD_BOARD_CELESTICA) {
        status = TimeCardI2cMuxReadLocked(
            context, &savedMask, &controllerStatus, &interruptStatus);
        telemetry->MuxChannelMask = savedMask;
        if (!NT_SUCCESS(status))
            goto Exit;
        telemetry->Flags = TIMECARD_SENSOR_FLAG_PRESENT;

        status = TimeCardI2cMuxWriteLocked(
            context, TIMECARD_I2C_MUX_CELESTICA_LM75B,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        KeStallExecutionProcessor(10u);
        TimeCardLm75bReadLocked(
            context, TIMECARD_SENSOR_LM75B_1_ADDRESS,
            &telemetry->BoardTemperature[0],
            &controllerStatus, &interruptStatus);
        TimeCardLm75bReadLocked(
            context, TIMECARD_SENSOR_LM75B_2_ADDRESS,
            &telemetry->BoardTemperature[1],
            &controllerStatus, &interruptStatus);
        TimeCardLm75bReadLocked(
            context, TIMECARD_SENSOR_LM75B_3_ADDRESS,
            &telemetry->BoardTemperature[2],
            &controllerStatus, &interruptStatus);

        status = TimeCardI2cMuxWriteLocked(
            context, TIMECARD_I2C_MUX_CELESTICA_SHT3X,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        KeStallExecutionProcessor(10u);
        TimeCardSht3xReadLocked(
            context, &telemetry->Humidity,
            &controllerStatus, &interruptStatus);

        status = TimeCardI2cMuxWriteLocked(
            context, TIMECARD_I2C_MUX_CELESTICA_ICP10100,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        KeStallExecutionProcessor(10u);
        TimeCardIcp10100ReadLocked(
            context, &telemetry->Pressure,
            &controllerStatus, &interruptStatus);

        status = TimeCardI2cMuxWriteLocked(
            context, TIMECARD_I2C_MUX_CELESTICA_BNO08X,
            &controllerStatus, &interruptStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        KeStallExecutionProcessor(10u);
        context->I2cSensorMuxMask =
            TIMECARD_I2C_MUX_CELESTICA_BNO08X;
        context->I2cImuType = TIMECARD_IMU_TYPE_BNO08X;
        context->I2cImuAddress = TIMECARD_SENSOR_BNO08X_ADDRESS;
        TimeCardBno08xReadLocked(
            context, &telemetry->Imu,
            &controllerStatus, &interruptStatus);
        telemetry->Flags |= TIMECARD_SENSOR_FLAG_VALID;
        goto Exit;
    }

    status = TimeCardSensorBranchSelectLocked(
        context, &savedMask, &changed,
        &controllerStatus, &interruptStatus);
    telemetry->MuxChannelMask = savedMask;
    if (!NT_SUCCESS(status))
        goto Exit;
    telemetry->Flags = TIMECARD_SENSOR_FLAG_PRESENT;

    /*
     * V9 normally places BME/BMP280 and INA219 on SENS_I2C, but supported
     * vendor/revision variants may route those optional parts differently.
     * Resolve and cache the environment, power, and IMU routes independently.
     */
    if (context->I2cSensorMuxMask != 0)
        imuMask = (UCHAR)context->I2cSensorMuxMask;
    environmentMask = TimeCardMonitorBranchResolveLocked(
        context, TRUE, &controllerStatus, &interruptStatus);
    powerMask = TimeCardMonitorBranchResolveLocked(
        context, FALSE, &controllerStatus, &interruptStatus);
    status = TimeCardI2cMuxWriteLocked(
        context, environmentMask,
        &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    KeStallExecutionProcessor(10u);

    TimeCardBme280ReadLocked(
        context, &telemetry->Environment,
        &controllerStatus, &interruptStatus);

    status = TimeCardI2cMuxWriteLocked(
        context, powerMask, &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    KeStallExecutionProcessor(10u);
    TimeCardIna219ReadLocked(
        context, TIMECARD_SENSOR_INA219_12V_ADDRESS,
        &telemetry->Rail12V, &controllerStatus, &interruptStatus);
    TimeCardIna219ReadLocked(
        context, TIMECARD_SENSOR_INA219_5V_ADDRESS,
        &telemetry->Rail5V, &controllerStatus, &interruptStatus);
    TimeCardIna219ReadLocked(
        context, TIMECARD_SENSOR_INA219_3V3_ADDRESS,
        &telemetry->Rail3V3, &controllerStatus, &interruptStatus);

    status = TimeCardI2cMuxWriteLocked(
        context, imuMask, &controllerStatus, &interruptStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    KeStallExecutionProcessor(10u);

    if (context->I2cImuType == TIMECARD_IMU_TYPE_BNO08X) {
        TimeCardBno08xReadLocked(
            context, &telemetry->Imu,
            &controllerStatus, &interruptStatus);
    } else {
        TimeCardBno055ReadLocked(
            context, &telemetry->Imu,
            &controllerStatus, &interruptStatus);
        if ((telemetry->Imu.Flags &
             TIMECARD_SENSOR_FLAG_PRESENT) == 0) {
            TimeCardBno08xReadLocked(
                context, &telemetry->Imu,
                &controllerStatus, &interruptStatus);
        }
    }
    telemetry->Flags |= TIMECARD_SENSOR_FLAG_VALID;

Exit:
    status = TimeCardSensorBranchRestoreLocked(
        context, savedMask, TRUE, status,
        &controllerStatus, &interruptStatus);
    telemetry->ControllerStatus = controllerStatus;
    telemetry->InterruptStatus = interruptStatus;
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardGetIdentity(PDEVICE_CONTEXT context, TIMECARD_IDENTITY *identity)
{
    static const UCHAR artConfigurationHeader[] = "oscillator=";
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
    if (context->BoardProfile == TIMECARD_BOARD_ART) {
        /*
         * Some production ART images use the entire 24c08 for the mRO-50
         * disciplining configuration.  In that layout offset 0x263 is text
         * inside "ctrl_drift_coeffs_factory", not a six-byte serial.  Detect
         * the configuration header so we never present those text bytes as a
         * plausible card identity.
         */
        request.Address = 0x50u;
        request.Subaddress = 0u;
        request.SubaddressLength = 1u;
        request.Length = sizeof(artConfigurationHeader) - 1u;
        request.TimeoutMilliseconds = TIMECARD_I2C_DEFAULT_TIMEOUT_MS;
        status = TimeCardI2cRead(context, &request, &transfer);
        if (NT_SUCCESS(status) &&
            transfer.Length == sizeof(artConfigurationHeader) - 1u &&
            RtlCompareMemory(transfer.Data, artConfigurationHeader,
                             sizeof(artConfigurationHeader) - 1u) ==
                sizeof(artConfigurationHeader) - 1u) {
            identity->Flags = TIMECARD_IDENTITY_FLAG_PRESENT;
            return STATUS_SUCCESS;
        }

        RtlZeroMemory(&request, sizeof(request));
        request.Size = sizeof(request);
        /*
         * Linux's ART EEPROM map stores the serial at absolute 24c08
         * offset 0x263. The high address block selects slave 0x52.
         */
        request.Address = 0x52u;
        request.Subaddress = 0x63u;
    } else {
        request.Address = TIMECARD_IDENTITY_ADDRESS;
        request.Subaddress = TIMECARD_IDENTITY_EUI48_OFFSET;
    }
    request.SubaddressLength = 1u;
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
