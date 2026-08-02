/* SPDX-License-Identifier: BSD-3-Clause */
/* OCP TimeCard PHC access, ported from the Linux ptp_ocp driver. */

#include "timecard.h"

NTSTATUS
TimeCardGetTime(PDEVICE_CONTEXT context, TIMECARD_TIME *time)
{
    ULONG ctrl = 0;
    ULONG oldControl;
    ULONG requestControl;
    ULONG i;
    NTSTATUS status = STATUS_IO_TIMEOUT;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    oldControl = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl) &
                 ~OCP_CTRL_TRANSIENT_MASK;
    requestControl = oldControl |
        OCP_CTRL_READ_TIME_REQ | OCP_CTRL_ENABLE;
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, requestControl);

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
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, oldControl);
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
    ULONG oldControl;
    ULONG requestControl;
    ULONG select;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (time->Seconds > MAXULONG || time->Nanoseconds >= 1000000000u ||
        time->Reserved != 0u)
        return STATUS_INVALID_PARAMETER;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    select = READ_REGISTER_ULONG((PULONG)&context->Regs->Select);
    oldControl = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl) &
                 ~OCP_CTRL_TRANSIENT_MASK;

    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select,
                         OCP_SELECT_CLOCK_REG);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->AdjustNs,
                         time->Nanoseconds);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->AdjustSec,
                         (ULONG)time->Seconds);
    requestControl = oldControl |
        OCP_CTRL_ADJUST_TIME | OCP_CTRL_ENABLE;
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, requestControl);

    /* Preserve the requested source in bits 7:0, not the active source. */
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select, select & 0xffu);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, oldControl);
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}

static BOOLEAN
TimeCardClockSourceValid(ULONG source)
{
    return source <= TIMECARD_CLOCK_SOURCE_SYNCE ||
           source == TIMECARD_CLOCK_SOURCE_DYN ||
           source == TIMECARD_CLOCK_SOURCE_REGS ||
           source == TIMECARD_CLOCK_SOURCE_EXT;
}

NTSTATUS
TimeCardSetClockSource(PDEVICE_CONTEXT context,
                       const TIMECARD_CLOCK_SOURCE_CONTROL *request,
                       TIMECARD_CLOCK_SOURCE_CONTROL *response)
{
    ULONG active;
    ULONG index;
    ULONG oldSelect;
    ULONG readback;
    ULONG version;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        !TimeCardClockSourceValid(request->Source) ||
        request->ActiveSource != 0u)
        return STATUS_INVALID_PARAMETER;
    for (index = 0; index < ARRAYSIZE(request->Reserved); ++index) {
        if (request->Reserved[index] != 0u)
            return STATUS_INVALID_PARAMETER;
    }
    /* NTP additionally requires an exact-image synthesis contract. */
    if (request->Source == TIMECARD_CLOCK_SOURCE_NTP &&
        !TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_NTP_SOURCE))
        return STATUS_NOT_SUPPORTED;
    if (request->Source == TIMECARD_CLOCK_SOURCE_SYNCE &&
        !TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_SYNCE_SOURCE))
        return STATUS_NOT_SUPPORTED;
    if (request->Source == TIMECARD_CLOCK_SOURCE_DYN &&
        !TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_DYNAMIC_SOURCE))
        return STATUS_NOT_SUPPORTED;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    version = READ_REGISTER_ULONG((PULONG)&context->Regs->Version);
    if ((request->Source == TIMECARD_CLOCK_SOURCE_DCF ||
         request->Source == TIMECARD_CLOCK_SOURCE_NTP) &&
        (version == 0u || version == MAXULONG || version < 0x01080000u)) {
        WdfWaitLockRelease(context->RegisterLock);
        return STATUS_NOT_SUPPORTED;
    }
    if ((request->Source == TIMECARD_CLOCK_SOURCE_SYNCE ||
         request->Source == TIMECARD_CLOCK_SOURCE_DYN) &&
        (version == 0u || version == MAXULONG ||
         version < 0x02070000u)) {
        WdfWaitLockRelease(context->RegisterLock);
        return STATUS_NOT_SUPPORTED;
    }
    oldSelect = READ_REGISTER_ULONG((PULONG)&context->Regs->Select);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select, request->Source);
    readback = READ_REGISTER_ULONG((PULONG)&context->Regs->Select);
    if ((readback & 0xffu) != request->Source) {
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select,
                             oldSelect & 0xffu);
        WdfWaitLockRelease(context->RegisterLock);
        return STATUS_DEVICE_DATA_ERROR;
    }
    active = readback >> 16;
    WdfWaitLockRelease(context->RegisterLock);

    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Source = request->Source;
    response->ActiveSource = active;
    return STATUS_SUCCESS;
}

static BOOLEAN
TimeCardCoreVersionAtLeast(ULONG version, ULONG major, ULONG minor)
{
    ULONG actualMajor = version >> 24;
    ULONG actualMinor = (version >> 16) & 0xffu;

    return version != 0u && version != MAXULONG &&
           (actualMajor > major ||
            (actualMajor == major && actualMinor >= minor));
}

NTSTATUS
TimeCardGetInfo(PDEVICE_CONTEXT context, TIMECARD_INFO *info)
{
    TIMECARD_TOD_CONTROL tod;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(info, sizeof(*info));
    info->AbiVersion = TIMECARD_ABI_VERSION;
    info->DriverVersion = 0x0001002au;
    info->Layout = context->Layout;
    info->InterruptMessages = context->InterruptMessages;
    info->BarLength = context->Bar0Length;
    info->ClockOffset = context->ClockOffset;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    info->ClockVersion = READ_REGISTER_ULONG((PULONG)&context->Regs->Version);
    info->ClockStatus = MAXULONG;
    if (TimeCardCoreVersionAtLeast(info->ClockVersion, 1u, 2u)) {
        info->ClockStatus = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->Status);
    }
    info->ClockSelect = READ_REGISTER_ULONG((PULONG)&context->Regs->Select);
    info->TodVersion = MAXULONG;
    info->TodStatus = MAXULONG;
    info->UtcStatus = MAXULONG;
    info->Leap = MAXULONG;
    info->GnssStatus = MAXULONG;
    info->Satellites = MAXULONG;
    if (context->Tod != NULL) {
        info->TodVersion = READ_REGISTER_ULONG((PULONG)&context->Tod->Version);
        if (TimeCardCoreVersionAtLeast(info->TodVersion, 1u, 2u)) {
            info->TodStatus = READ_REGISTER_ULONG(
                (PULONG)&context->Tod->Status);
        }
    }
    WdfWaitLockRelease(context->RegisterLock);
    if (NT_SUCCESS(TimeCardTodQuery(context, &tod))) {
        if ((tod.Flags & TIMECARD_TOD_FLAG_UTC_TELEMETRY_VALID) != 0u) {
            info->UtcStatus = tod.UtcStatus;
            info->Leap = (ULONG)tod.TimeToLeapSeconds;
        }
        if ((tod.Flags & TIMECARD_TOD_FLAG_GNSS_TELEMETRY_VALID) != 0u) {
            info->GnssStatus = tod.GnssStatus;
            info->Satellites = tod.Satellites;
        }
    }
    return STATUS_SUCCESS;
}
