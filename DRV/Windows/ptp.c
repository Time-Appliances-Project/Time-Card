/* SPDX-License-Identifier: BSD-3-Clause */
/* OCP TimeCard PHC access, ported from the Linux ptp_ocp driver. */

#include "timecard.h"

NTSTATUS
TimeCardGetTime(PDEVICE_CONTEXT context, TIMECARD_TIME *time)
{
    ULONG ctrl = 0;
    ULONG i;
    NTSTATUS status = STATUS_IO_TIMEOUT;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl,
                         OCP_CTRL_READ_TIME_REQ | OCP_CTRL_ENABLE);

    for (i = 0; i < 100; ++i) {
        ctrl = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl);
        if ((ctrl & OCP_CTRL_READ_TIME_DONE) != 0) {
            status = STATUS_SUCCESS;
            break;
        }
        KeStallExecutionProcessor(1);
    }

    time->Nanoseconds = READ_REGISTER_ULONG((PULONG)&context->Regs->TimeNs);
    time->Seconds = READ_REGISTER_ULONG((PULONG)&context->Regs->TimeSec);
    time->Reserved = 0;
    WdfWaitLockRelease(context->RegisterLock);

    if (time->Nanoseconds >= 1000000000u)
        return STATUS_DEVICE_DATA_ERROR;
    return status;
}

NTSTATUS
TimeCardGetCrossTimestamp(PDEVICE_CONTEXT context,
                          TIMECARD_CROSSTIMESTAMP *timestamp)
{
    LARGE_INTEGER before;
    LARGE_INTEGER after;
    NTSTATUS status;

    KeQuerySystemTimePrecise(&before);
    status = TimeCardGetTime(context, &timestamp->CardTime);
    KeQuerySystemTimePrecise(&after);
    timestamp->SystemTimeBefore100ns = (unsigned __int64)before.QuadPart;
    timestamp->SystemTimeAfter100ns = (unsigned __int64)after.QuadPart;
    return status;
}

NTSTATUS
TimeCardSetTime(PDEVICE_CONTEXT context, const TIMECARD_TIME *time)
{
    ULONG select;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (time->Seconds > MAXULONG || time->Nanoseconds >= 1000000000u)
        return STATUS_INVALID_PARAMETER;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    select = READ_REGISTER_ULONG((PULONG)&context->Regs->Select);

    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select,
                         OCP_SELECT_CLOCK_REG);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->AdjustNs,
                         time->Nanoseconds);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->AdjustSec,
                         (ULONG)time->Seconds);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl,
                         OCP_CTRL_ADJUST_TIME | OCP_CTRL_ENABLE);

    /* Reads report the selected source in bits 31:16; writes consume 15:0. */
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select, select >> 16);
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}

static BOOLEAN
TimeCardClockSourceValid(ULONG source)
{
    return source <= TIMECARD_CLOCK_SOURCE_DCF ||
           source == TIMECARD_CLOCK_SOURCE_REGS ||
           source == TIMECARD_CLOCK_SOURCE_EXT;
}

NTSTATUS
TimeCardSetClockSource(PDEVICE_CONTEXT context,
                       const TIMECARD_CLOCK_SOURCE_CONTROL *request,
                       TIMECARD_CLOCK_SOURCE_CONTROL *response)
{
    ULONG active;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        !TimeCardClockSourceValid(request->Source))
        return STATUS_INVALID_PARAMETER;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select, request->Source);
    active = READ_REGISTER_ULONG((PULONG)&context->Regs->Select) >> 16;
    WdfWaitLockRelease(context->RegisterLock);

    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Source = request->Source;
    response->ActiveSource = active;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardGetInfo(PDEVICE_CONTEXT context, TIMECARD_INFO *info)
{
    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(info, sizeof(*info));
    info->AbiVersion = TIMECARD_ABI_VERSION;
    info->DriverVersion = 0x00010018u;
    info->Layout = context->Layout;
    info->InterruptMessages = context->InterruptMessages;
    info->BarLength = context->Bar0Length;
    info->ClockOffset = context->ClockOffset;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    info->ClockVersion = READ_REGISTER_ULONG((PULONG)&context->Regs->Version);
    info->ClockStatus = READ_REGISTER_ULONG((PULONG)&context->Regs->Status);
    info->ClockSelect = READ_REGISTER_ULONG((PULONG)&context->Regs->Select);
    if (context->Tod != NULL) {
        info->TodVersion = READ_REGISTER_ULONG((PULONG)&context->Tod->Version);
        info->TodStatus = READ_REGISTER_ULONG((PULONG)&context->Tod->Status);
        info->UtcStatus = READ_REGISTER_ULONG((PULONG)&context->Tod->UtcStatus);
        info->Leap = READ_REGISTER_ULONG((PULONG)&context->Tod->Leap);
        info->GnssStatus =
            READ_REGISTER_ULONG((PULONG)&context->Tod->GnssStatus);
        info->Satellites =
            READ_REGISTER_ULONG((PULONG)&context->Tod->NumSat);
    } else {
        info->TodVersion = MAXULONG;
        info->TodStatus = MAXULONG;
        info->UtcStatus = MAXULONG;
        info->Leap = MAXULONG;
        info->GnssStatus = MAXULONG;
        info->Satellites = MAXULONG;
    }
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}
