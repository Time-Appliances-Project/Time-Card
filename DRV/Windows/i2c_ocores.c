/* SPDX-License-Identifier: BSD-3-Clause */
/* Polled OpenCores I2C access for the Orolia ART Time Card. */

#include "timecard.h"

#define OCI2C_PRELOW  0u
#define OCI2C_PREHIGH 1u
#define OCI2C_CONTROL 2u
#define OCI2C_DATA    3u
#define OCI2C_COMMAND 4u
#define OCI2C_STATUS  4u

#define OCI2C_CONTROL_ENABLE 0x80u

#define OCI2C_COMMAND_START     0x91u
#define OCI2C_COMMAND_STOP      0x41u
#define OCI2C_COMMAND_READ_ACK  0x21u
#define OCI2C_COMMAND_READ_NACK 0x29u
#define OCI2C_COMMAND_WRITE     0x11u
#define OCI2C_COMMAND_IACK      0x01u

#define OCI2C_STATUS_INTERRUPT 0x01u
#define OCI2C_STATUS_TIP       0x02u
#define OCI2C_STATUS_ARB_LOST  0x20u
#define OCI2C_STATUS_BUSY      0x40u
#define OCI2C_STATUS_NACK      0x80u

#define OCI2C_PRESCALE_125MHZ_400KHZ 61u
#define OCI2C_POLL_US 5u
#define OCI2C_TIMEOUT_POLLS 20000u

static UCHAR
TimeCardOcoresRead(PDEVICE_CONTEXT context, ULONG reg)
{
    return READ_REGISTER_UCHAR((volatile UCHAR *)(context->I2c + reg));
}

static VOID
TimeCardOcoresWrite(PDEVICE_CONTEXT context, ULONG reg, UCHAR value)
{
    WRITE_REGISTER_UCHAR((volatile UCHAR *)(context->I2c + reg), value);
}

static NTSTATUS
TimeCardOcoresInitialize(PDEVICE_CONTEXT context)
{
    UCHAR control;

    if (context->I2c == NULL)
        return STATUS_DEVICE_NOT_READY;

    control = TimeCardOcoresRead(context, OCI2C_CONTROL);
    TimeCardOcoresWrite(context, OCI2C_CONTROL,
                        control & ~OCI2C_CONTROL_ENABLE);
    TimeCardOcoresWrite(context, OCI2C_PRELOW,
                        (UCHAR)OCI2C_PRESCALE_125MHZ_400KHZ);
    TimeCardOcoresWrite(context, OCI2C_PREHIGH, 0);
    TimeCardOcoresWrite(context, OCI2C_CONTROL,
                        OCI2C_CONTROL_ENABLE);
    TimeCardOcoresWrite(context, OCI2C_COMMAND, OCI2C_COMMAND_IACK);
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardOcoresWaitComplete(PDEVICE_CONTEXT context, PUCHAR finalStatus)
{
    ULONG poll;
    UCHAR status = 0;

    for (poll = 0; poll < OCI2C_TIMEOUT_POLLS; ++poll) {
        status = TimeCardOcoresRead(context, OCI2C_STATUS);
        if ((status & OCI2C_STATUS_TIP) == 0) {
            *finalStatus = status;
            if ((status & OCI2C_STATUS_ARB_LOST) != 0)
                return STATUS_IO_DEVICE_ERROR;
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(OCI2C_POLL_US);
    }
    *finalStatus = status;
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
TimeCardOcoresWaitIdle(PDEVICE_CONTEXT context, PUCHAR finalStatus)
{
    ULONG poll;
    UCHAR status = 0;

    for (poll = 0; poll < OCI2C_TIMEOUT_POLLS; ++poll) {
        status = TimeCardOcoresRead(context, OCI2C_STATUS);
        if ((status & OCI2C_STATUS_BUSY) == 0) {
            *finalStatus = status;
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(OCI2C_POLL_US);
    }
    *finalStatus = status;
    return STATUS_IO_TIMEOUT;
}

static VOID
TimeCardOcoresStop(PDEVICE_CONTEXT context, PUCHAR controllerStatus)
{
    TimeCardOcoresWrite(context, OCI2C_COMMAND, OCI2C_COMMAND_STOP);
    (VOID)TimeCardOcoresWaitIdle(context, controllerStatus);
    TimeCardOcoresWrite(context, OCI2C_COMMAND, OCI2C_COMMAND_IACK);
}

static NTSTATUS
TimeCardOcoresStart(PDEVICE_CONTEXT context, UCHAR address,
                    BOOLEAN read, PUCHAR controllerStatus)
{
    NTSTATUS status;

    TimeCardOcoresWrite(context, OCI2C_DATA,
                        (UCHAR)((address << 1) | (read ? 1u : 0u)));
    TimeCardOcoresWrite(context, OCI2C_COMMAND, OCI2C_COMMAND_START);
    status = TimeCardOcoresWaitComplete(context, controllerStatus);
    if (NT_SUCCESS(status) &&
        (*controllerStatus & OCI2C_STATUS_NACK) != 0) {
        status = STATUS_NO_SUCH_DEVICE;
    }
    return status;
}

static NTSTATUS
TimeCardOcoresWriteByte(PDEVICE_CONTEXT context, UCHAR value,
                        PUCHAR controllerStatus)
{
    NTSTATUS status;

    TimeCardOcoresWrite(context, OCI2C_DATA, value);
    TimeCardOcoresWrite(context, OCI2C_COMMAND, OCI2C_COMMAND_WRITE);
    status = TimeCardOcoresWaitComplete(context, controllerStatus);
    if (NT_SUCCESS(status) &&
        (*controllerStatus & OCI2C_STATUS_NACK) != 0) {
        status = STATUS_NO_SUCH_DEVICE;
    }
    return status;
}

static NTSTATUS
TimeCardOcoresReadByte(PDEVICE_CONTEXT context, BOOLEAN last,
                       PUCHAR value, PUCHAR controllerStatus)
{
    NTSTATUS status;

    TimeCardOcoresWrite(
        context, OCI2C_COMMAND,
        last ? OCI2C_COMMAND_READ_NACK : OCI2C_COMMAND_READ_ACK);
    status = TimeCardOcoresWaitComplete(context, controllerStatus);
    if (NT_SUCCESS(status))
        *value = TimeCardOcoresRead(context, OCI2C_DATA);
    return status;
}

static VOID
TimeCardOcoresRecordTrace(PDEVICE_CONTEXT context, UCHAR status)
{
    context->I2cLastStartTrace =
        (ULONG)TimeCardOcoresRead(context, OCI2C_PRELOW) |
        ((ULONG)TimeCardOcoresRead(context, OCI2C_PREHIGH) << 8) |
        ((ULONG)TimeCardOcoresRead(context, OCI2C_CONTROL) << 16) |
        ((ULONG)status << 24);
    context->I2cLastStartEvents = status;
}

NTSTATUS
TimeCardOcoresI2cGetStatusLocked(PDEVICE_CONTEXT context,
                                 TIMECARD_I2C_STATUS *status)
{
    UCHAR control;
    UCHAR controllerStatus;
    NTSTATUS result = TimeCardOcoresInitialize(context);

    if (!NT_SUCCESS(result))
        return result;
    control = TimeCardOcoresRead(context, OCI2C_CONTROL);
    controllerStatus = TimeCardOcoresRead(context, OCI2C_STATUS);

    RtlZeroMemory(status, sizeof(*status));
    status->Size = sizeof(*status);
    status->Flags = TIMECARD_I2C_FLAG_PRESENT |
                    TIMECARD_I2C_FLAG_RX_EMPTY |
                    TIMECARD_I2C_FLAG_TX_EMPTY;
    if ((control & OCI2C_CONTROL_ENABLE) != 0)
        status->Flags |= TIMECARD_I2C_FLAG_ENABLED;
    if ((controllerStatus & OCI2C_STATUS_BUSY) != 0)
        status->Flags |= TIMECARD_I2C_FLAG_BUS_BUSY;
    status->Offset = context->I2cOffset;
    status->Control = control;
    status->Status = controllerStatus;
    status->InterruptStatus =
        controllerStatus & OCI2C_STATUS_INTERRUPT;
    status->KnownDeviceMask = context->I2cKnownDeviceMask;
    status->Reserved[0] = context->I2cLastStartTrace;
    status->Reserved[1] = context->I2cLastStartEvents;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardOcoresI2cProbeLocked(PDEVICE_CONTEXT context, ULONG address,
                             TIMECARD_I2C_PROBE *probe)
{
    UCHAR controllerStatus = 0;
    NTSTATUS status;

    RtlZeroMemory(probe, sizeof(*probe));
    probe->Size = sizeof(*probe);
    probe->Address = address;

    status = TimeCardOcoresInitialize(context);
    if (!NT_SUCCESS(status))
        return status;
    status = TimeCardOcoresStart(context, (UCHAR)address, FALSE,
                                 &controllerStatus);
    if (status == STATUS_NO_SUCH_DEVICE) {
        probe->Present = 0;
        status = STATUS_SUCCESS;
    } else if (NT_SUCCESS(status)) {
        probe->Present = 1;
    }
    TimeCardOcoresStop(context, &controllerStatus);
    probe->ControllerStatus = controllerStatus;
    probe->InterruptStatus =
        controllerStatus & OCI2C_STATUS_INTERRUPT;
    TimeCardOcoresRecordTrace(context, controllerStatus);
    return status;
}

NTSTATUS
TimeCardOcoresI2cReadLocked(PDEVICE_CONTEXT context,
                            const TIMECARD_I2C_READ_REQUEST *request,
                            TIMECARD_I2C_TRANSFER *transfer)
{
    UCHAR controllerStatus = 0;
    ULONG i;
    NTSTATUS status;

    RtlZeroMemory(transfer, sizeof(*transfer));
    transfer->Size = FIELD_OFFSET(TIMECARD_I2C_TRANSFER, Data) +
                     request->Length;
    transfer->Address = request->Address;

    status = TimeCardOcoresInitialize(context);
    if (!NT_SUCCESS(status))
        return status;

    if (request->SubaddressLength != 0) {
        status = TimeCardOcoresStart(
            context, (UCHAR)request->Address, FALSE, &controllerStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        if (request->SubaddressLength == 2u) {
            status = TimeCardOcoresWriteByte(
                context, (UCHAR)(request->Subaddress >> 8),
                &controllerStatus);
            if (!NT_SUCCESS(status))
                goto Exit;
        }
        status = TimeCardOcoresWriteByte(
            context, (UCHAR)request->Subaddress, &controllerStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
    }

    status = TimeCardOcoresStart(
        context, (UCHAR)request->Address, TRUE, &controllerStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    for (i = 0; i < request->Length; ++i) {
        status = TimeCardOcoresReadByte(
            context, i + 1u == request->Length,
            &transfer->Data[i], &controllerStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
        ++transfer->Length;
    }

Exit:
    TimeCardOcoresStop(context, &controllerStatus);
    transfer->ControllerStatus = controllerStatus;
    transfer->InterruptStatus =
        controllerStatus & OCI2C_STATUS_INTERRUPT;
    TimeCardOcoresRecordTrace(context, controllerStatus);
    return status;
}

NTSTATUS
TimeCardOcoresI2cWriteLocked(PDEVICE_CONTEXT context, ULONG address,
                             const UCHAR *data, ULONG length,
                             ULONG *controllerStatus,
                             ULONG *interruptStatus)
{
    UCHAR finalStatus = 0;
    ULONG i;
    NTSTATUS status;

    status = TimeCardOcoresInitialize(context);
    if (!NT_SUCCESS(status))
        return status;
    status = TimeCardOcoresStart(
        context, (UCHAR)address, FALSE, &finalStatus);
    if (!NT_SUCCESS(status))
        goto Exit;
    for (i = 0; i < length; ++i) {
        status = TimeCardOcoresWriteByte(
            context, data[i], &finalStatus);
        if (!NT_SUCCESS(status))
            goto Exit;
    }

Exit:
    TimeCardOcoresStop(context, &finalStatus);
    *controllerStatus = finalStatus;
    *interruptStatus = finalStatus & OCI2C_STATUS_INTERRUPT;
    TimeCardOcoresRecordTrace(context, finalStatus);
    return status;
}
