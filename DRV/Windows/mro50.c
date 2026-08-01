/* SPDX-License-Identifier: BSD-3-Clause */
/* Orolia/Safran ART mRO-50 FPGA bridge, matched to Linux ptp_ocp. */

#include "timecard.h"

#define MRO50_CTRL_ENABLE        (1u << 0)
#define MRO50_CTRL_LOCK          (1u << 1)
#define MRO50_CTRL_READ_CMD      (1u << 2)
#define MRO50_CTRL_READ_COARSE   (1u << 3)
#define MRO50_CTRL_READ_DONE     (1u << 4)
#define MRO50_CTRL_ADJUST_CMD    (1u << 5)
#define MRO50_CTRL_ADJUST_COARSE (1u << 6)
#define MRO50_CTRL_SAVE_COARSE   (1u << 7)

#define MRO50_OP_READ_FINE \
    (MRO50_CTRL_ENABLE | MRO50_CTRL_READ_CMD)
#define MRO50_OP_READ_COARSE \
    (MRO50_OP_READ_FINE | MRO50_CTRL_READ_COARSE)
#define MRO50_OP_ADJUST_FINE \
    (MRO50_CTRL_ENABLE | MRO50_CTRL_ADJUST_CMD)
#define MRO50_OP_ADJUST_COARSE \
    (MRO50_OP_ADJUST_FINE | MRO50_CTRL_ADJUST_COARSE)
#define MRO50_OP_SAVE_COARSE \
    (MRO50_CTRL_ENABLE | MRO50_CTRL_SAVE_COARSE)

static NTSTATUS
TimeCardMro50Wait(PDEVICE_CONTEXT context, ULONG mask)
{
    LARGE_INTEGER interval;
    ULONG i;

    interval.QuadPart = -1000; /* 100 microseconds, relative */
    for (i = 0; i < 100u; ++i) {
        ULONG control = READ_REGISTER_ULONG(
            (PULONG)&context->Mro50->Control);

        if ((control & mask) != 0)
            return STATUS_SUCCESS;
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS
TimeCardMro50ReadLocked(PDEVICE_CONTEXT context, ULONG operation,
                        PULONG value)
{
    NTSTATUS status;

    WRITE_REGISTER_ULONG((PULONG)&context->Mro50->Control, operation);
    status = TimeCardMro50Wait(context, MRO50_CTRL_READ_DONE);
    if (NT_SUCCESS(status)) {
        *value = READ_REGISTER_ULONG((PULONG)&context->Mro50->Value);
    }
    return status;
}

static NTSTATUS
TimeCardMro50AdjustLocked(PDEVICE_CONTEXT context, ULONG operation,
                          ULONG value)
{
    WRITE_REGISTER_ULONG((PULONG)&context->Mro50->Adjust, value);
    WRITE_REGISTER_ULONG((PULONG)&context->Mro50->Control, operation);
    return TimeCardMro50Wait(context, MRO50_CTRL_ADJUST_CMD);
}

NTSTATUS
TimeCardMro50Query(PDEVICE_CONTEXT context, TIMECARD_MRO50_STATUS *status)
{
    NTSTATUS fineStatus;
    NTSTATUS coarseStatus;
    ULONG control;
    ULONG fine = 0;
    ULONG coarse = 0;

    if (!context->HardwareReady ||
        context->BoardProfile != TIMECARD_BOARD_ART ||
        context->Mro50 == NULL) {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(status, sizeof(*status));
    status->Size = sizeof(*status);
    status->Flags = TIMECARD_MRO50_FLAG_PRESENT;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status->Temperature = READ_REGISTER_ULONG(
        (PULONG)&context->Mro50->Temperature);
    status->BoardConfig = context->ArtBoardConfig != NULL ?
        READ_REGISTER_ULONG((PULONG)context->ArtBoardConfig) : 0u;
    fineStatus = TimeCardMro50ReadLocked(
        context, MRO50_OP_READ_FINE, &fine);
    coarseStatus = TimeCardMro50ReadLocked(
        context, MRO50_OP_READ_COARSE, &coarse);
    control = READ_REGISTER_ULONG((PULONG)&context->Mro50->Control);
    WdfWaitLockRelease(context->RegisterLock);

    status->Control = control;
    status->FineAdjustment = fine;
    status->CoarseAdjustment = coarse;
    if ((control & MRO50_CTRL_ENABLE) != 0)
        status->Flags |= TIMECARD_MRO50_FLAG_ENABLED;
    if ((control & MRO50_CTRL_LOCK) != 0)
        status->Flags |= TIMECARD_MRO50_FLAG_LOCKED;
    if (NT_SUCCESS(fineStatus))
        status->Flags |= TIMECARD_MRO50_FLAG_FINE_VALID;
    if (NT_SUCCESS(coarseStatus))
        status->Flags |= TIMECARD_MRO50_FLAG_COARSE_VALID;
    if (status->BoardConfig != 0)
        status->Flags |= TIMECARD_MRO50_FLAG_SERIAL_ENABLED;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardMro50Control(PDEVICE_CONTEXT context,
                     const TIMECARD_MRO50_CONTROL *control,
                     TIMECARD_MRO50_STATUS *status)
{
    NTSTATUS operationStatus = STATUS_SUCCESS;

    if (!context->HardwareReady ||
        context->BoardProfile != TIMECARD_BOARD_ART ||
        context->Mro50 == NULL) {
        return STATUS_NOT_SUPPORTED;
    }
    if (control->Size < sizeof(*control) ||
        control->Reserved != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    switch (control->Action) {
    case TIMECARD_MRO50_ACTION_QUERY:
        break;
    case TIMECARD_MRO50_ACTION_ADJUST_FINE:
        if (control->Value > TIMECARD_MRO50_FINE_MAXIMUM) {
            operationStatus = STATUS_INVALID_PARAMETER;
            break;
        }
        operationStatus = TimeCardMro50AdjustLocked(
            context, MRO50_OP_ADJUST_FINE, control->Value);
        break;
    case TIMECARD_MRO50_ACTION_ADJUST_COARSE:
        if (control->Value > TIMECARD_MRO50_COARSE_MAXIMUM) {
            operationStatus = STATUS_INVALID_PARAMETER;
            break;
        }
        operationStatus = TimeCardMro50AdjustLocked(
            context, MRO50_OP_ADJUST_COARSE, control->Value);
        break;
    case TIMECARD_MRO50_ACTION_SAVE_COARSE:
        WRITE_REGISTER_ULONG((PULONG)&context->Mro50->Control,
                             MRO50_OP_SAVE_COARSE);
        break;
    case TIMECARD_MRO50_ACTION_SERIAL_ENABLE:
        if (context->ArtBoardConfig == NULL) {
            operationStatus = STATUS_NOT_SUPPORTED;
        } else {
            WRITE_REGISTER_ULONG((PULONG)context->ArtBoardConfig,
                                 control->Value != 0 ? 1u : 0u);
        }
        break;
    default:
        operationStatus = STATUS_INVALID_PARAMETER;
        break;
    }
    WdfWaitLockRelease(context->RegisterLock);

    if (!NT_SUCCESS(operationStatus))
        return operationStatus;
    return TimeCardMro50Query(context, status);
}
