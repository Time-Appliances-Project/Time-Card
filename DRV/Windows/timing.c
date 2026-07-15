/* SPDX-License-Identifier: BSD-3-Clause */
/* Bounded signal-generator and frequency-counter control. */

#include "timecard.h"

#define TIMECARD_NANOSECONDS_PER_SECOND 1000000000ull
#define TIMECARD_SIGNAL_ENABLE_VALID    0x00000003u
#define TIMECARD_FREQUENCY_ENABLE       0x00000001u
#define TIMECARD_FREQUENCY_VALID        (1u << 31)
#define TIMECARD_FREQUENCY_ERROR        (1u << 30)
#define TIMECARD_FREQUENCY_OVERRUN      (1u << 29)
#define TIMECARD_FREQUENCY_VALUE_MASK   ((1u << 24) - 1u)

C_ASSERT(sizeof(TIMECARD_SIGNAL_CONTROL) == 64);
C_ASSERT(sizeof(TIMECARD_FREQUENCY_CONTROL) == 32);
C_ASSERT(FIELD_OFFSET(TIMECARD_SIGNAL_REG, StartNanoseconds) == 0x40);
C_ASSERT(FIELD_OFFSET(TIMECARD_SIGNAL_REG, RepeatCount) == 0x58);

static ULONGLONG
TimeCardJoinTime(ULONG seconds, ULONG nanoseconds)
{
    return (ULONGLONG)seconds * TIMECARD_NANOSECONDS_PER_SECOND +
           nanoseconds;
}

static NTSTATUS
TimeCardReadClockLocked(PDEVICE_CONTEXT context, PULONGLONG nanoseconds)
{
    ULONG ctrl;
    ULONG seconds;
    ULONG subsecond;
    ULONG i;

    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl,
                         OCP_CTRL_READ_TIME_REQ | OCP_CTRL_ENABLE);
    for (i = 0; i < 100; ++i) {
        ctrl = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl);
        if ((ctrl & OCP_CTRL_READ_TIME_DONE) != 0)
            break;
        KeStallExecutionProcessor(1);
    }
    if (i == 100)
        return STATUS_IO_TIMEOUT;

    subsecond = READ_REGISTER_ULONG((PULONG)&context->Regs->TimeNs);
    seconds = READ_REGISTER_ULONG((PULONG)&context->Regs->TimeSec);
    if (subsecond >= TIMECARD_NANOSECONDS_PER_SECOND)
        return STATUS_DEVICE_DATA_ERROR;
    *nanoseconds = TimeCardJoinTime(seconds, subsecond);
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardSignalQueryLocked(PDEVICE_CONTEXT context, ULONG generator,
                          TIMECARD_SIGNAL_CONTROL *control)
{
    volatile TIMECARD_SIGNAL_REG *reg;
    ULONG enable;
    ULONG polarity;
    ULONG periodNanoseconds;
    ULONG pulseNanoseconds;
    ULONG startNanoseconds;

    reg = context->Signal[generator - 1];
    if (reg == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Generator = generator;
    enable = READ_REGISTER_ULONG((PULONG)&reg->Enable);
    control->Status = READ_REGISTER_ULONG((PULONG)&reg->Status);
    polarity = READ_REGISTER_ULONG((PULONG)&reg->Polarity);
    control->Version = READ_REGISTER_ULONG((PULONG)&reg->Version);
    control->RepeatCount = READ_REGISTER_ULONG((PULONG)&reg->RepeatCount);
    startNanoseconds = READ_REGISTER_ULONG((PULONG)&reg->StartNanoseconds);
    control->StartSeconds = READ_REGISTER_ULONG((PULONG)&reg->StartSeconds);
    pulseNanoseconds = READ_REGISTER_ULONG((PULONG)&reg->PulseNanoseconds);
    periodNanoseconds = READ_REGISTER_ULONG((PULONG)&reg->PeriodNanoseconds);

    if (startNanoseconds >= TIMECARD_NANOSECONDS_PER_SECOND ||
        pulseNanoseconds >= TIMECARD_NANOSECONDS_PER_SECOND ||
        periodNanoseconds >= TIMECARD_NANOSECONDS_PER_SECOND)
        return STATUS_DEVICE_DATA_ERROR;

    control->StartNanoseconds = startNanoseconds;
    control->PulseNanoseconds = TimeCardJoinTime(
        READ_REGISTER_ULONG((PULONG)&reg->PulseSeconds), pulseNanoseconds);
    control->PeriodNanoseconds = TimeCardJoinTime(
        READ_REGISTER_ULONG((PULONG)&reg->PeriodSeconds),
        periodNanoseconds);
    control->Flags = TIMECARD_SIGNAL_FLAG_PRESENT;
    if ((enable & 1u) != 0)
        control->Flags |= TIMECARD_SIGNAL_FLAG_ENABLED;
    if (polarity != 0)
        control->Flags |= TIMECARD_SIGNAL_FLAG_INVERTED;
    if (control->PeriodNanoseconds != 0) {
        ULONGLONG start = TimeCardJoinTime(
            (ULONG)control->StartSeconds, control->StartNanoseconds);
        control->PhaseNanoseconds = start % control->PeriodNanoseconds;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardSignalQuery(PDEVICE_CONTEXT context, ULONG generator,
                    TIMECARD_SIGNAL_CONTROL *control)
{
    NTSTATUS status;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (generator == 0 || generator > TIMECARD_SIGNAL_COUNT)
        return STATUS_INVALID_PARAMETER;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardSignalQueryLocked(context, generator, control);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardSignalSet(PDEVICE_CONTEXT context,
                  const TIMECARD_SIGNAL_CONTROL *request,
                  TIMECARD_SIGNAL_CONTROL *response)
{
    volatile TIMECARD_SIGNAL_REG *reg;
    ULONGLONG period;
    ULONGLONG pulse;
    ULONGLONG phase;
    ULONGLONG now;
    ULONGLONG start;
    ULONGLONG remainder;
    ULONGLONG maximum = ~(ULONGLONG)0;
    BOOLEAN enabled;
    NTSTATUS status;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) || request->Generator == 0 ||
        request->Generator > TIMECARD_SIGNAL_COUNT)
        return STATUS_INVALID_PARAMETER;
    reg = context->Signal[request->Generator - 1];
    if (reg == NULL)
        return STATUS_NOT_SUPPORTED;

    enabled = (request->Flags & TIMECARD_SIGNAL_FLAG_ENABLED) != 0;
    period = request->PeriodNanoseconds;
    pulse = request->PulseNanoseconds;
    phase = request->PhaseNanoseconds;
    if (enabled &&
        (period < 2 || pulse == 0 || pulse >= period || phase >= period ||
         period / TIMECARD_NANOSECONDS_PER_SECOND > MAXULONG))
        return STATUS_INVALID_PARAMETER;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask, 0);
    WRITE_REGISTER_ULONG((PULONG)&reg->Enable, 0);
    WRITE_REGISTER_ULONG((PULONG)&reg->Polarity,
        (request->Flags & TIMECARD_SIGNAL_FLAG_INVERTED) != 0 ? 1u : 0u);

    if (!enabled) {
        status = TimeCardSignalQueryLocked(context, request->Generator,
                                           response);
        WdfWaitLockRelease(context->RegisterLock);
        return status;
    }

    status = TimeCardReadClockLocked(context, &now);
    if (!NT_SUCCESS(status))
        goto done;
    if (maximum - now < 1000000ull) {
        status = STATUS_INTEGER_OVERFLOW;
        goto done;
    }
    now += 1000000ull;
    remainder = now % period;
    start = now - remainder;
    if (remainder != 0) {
        if (maximum - start < period) {
            status = STATUS_INTEGER_OVERFLOW;
            goto done;
        }
        start += period;
    }
    if (maximum - start < phase) {
        status = STATUS_INTEGER_OVERFLOW;
        goto done;
    }
    start += phase;
    if (start <= now) {
        if (maximum - start < period) {
            status = STATUS_INTEGER_OVERFLOW;
            goto done;
        }
        start += period;
    }
    if (start / TIMECARD_NANOSECONDS_PER_SECOND > MAXULONG) {
        status = STATUS_INTEGER_OVERFLOW;
        goto done;
    }

    WRITE_REGISTER_ULONG((PULONG)&reg->StartSeconds,
        (ULONG)(start / TIMECARD_NANOSECONDS_PER_SECOND));
    WRITE_REGISTER_ULONG((PULONG)&reg->StartNanoseconds,
        (ULONG)(start % TIMECARD_NANOSECONDS_PER_SECOND));
    WRITE_REGISTER_ULONG((PULONG)&reg->PeriodSeconds,
        (ULONG)(period / TIMECARD_NANOSECONDS_PER_SECOND));
    WRITE_REGISTER_ULONG((PULONG)&reg->PeriodNanoseconds,
        (ULONG)(period % TIMECARD_NANOSECONDS_PER_SECOND));
    WRITE_REGISTER_ULONG((PULONG)&reg->PulseSeconds,
        (ULONG)(pulse / TIMECARD_NANOSECONDS_PER_SECOND));
    WRITE_REGISTER_ULONG((PULONG)&reg->PulseNanoseconds,
        (ULONG)(pulse % TIMECARD_NANOSECONDS_PER_SECOND));
    WRITE_REGISTER_ULONG((PULONG)&reg->RepeatCount, 0);
    WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt, 0);
    WRITE_REGISTER_ULONG((PULONG)&reg->Enable,
                         TIMECARD_SIGNAL_ENABLE_VALID);
    status = TimeCardSignalQueryLocked(context, request->Generator, response);

done:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

static NTSTATUS
TimeCardFrequencyQueryLocked(PDEVICE_CONTEXT context, ULONG counter,
                             TIMECARD_FREQUENCY_CONTROL *control)
{
    volatile TIMECARD_FREQUENCY_REG *reg;
    ULONG rawControl;
    ULONG status;

    reg = context->Frequency[counter - 1];
    if (reg == NULL)
        return STATUS_NOT_SUPPORTED;
    rawControl = READ_REGISTER_ULONG((PULONG)&reg->Control);
    status = READ_REGISTER_ULONG((PULONG)&reg->Status);

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Counter = counter;
    control->Control = rawControl;
    control->Status = status;
    control->Flags = TIMECARD_FREQUENCY_FLAG_PRESENT;
    if ((rawControl & TIMECARD_FREQUENCY_ENABLE) != 0) {
        control->Flags |= TIMECARD_FREQUENCY_FLAG_ENABLED;
        control->IntegrationSeconds = (rawControl >> 8) & 0xffu;
    }
    if ((status & TIMECARD_FREQUENCY_VALID) != 0) {
        control->Flags |= TIMECARD_FREQUENCY_FLAG_VALID;
        control->FrequencyHz = status & TIMECARD_FREQUENCY_VALUE_MASK;
    }
    if ((status & TIMECARD_FREQUENCY_ERROR) != 0)
        control->Flags |= TIMECARD_FREQUENCY_FLAG_ERROR;
    if ((status & TIMECARD_FREQUENCY_OVERRUN) != 0)
        control->Flags |= TIMECARD_FREQUENCY_FLAG_OVERRUN;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardFrequencyQuery(PDEVICE_CONTEXT context, ULONG counter,
                       TIMECARD_FREQUENCY_CONTROL *control)
{
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (counter == 0 || counter > TIMECARD_FREQUENCY_COUNT)
        return STATUS_INVALID_PARAMETER;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardFrequencyQueryLocked(context, counter, control);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardFrequencySet(PDEVICE_CONTEXT context,
                     const TIMECARD_FREQUENCY_CONTROL *request,
                     TIMECARD_FREQUENCY_CONTROL *response)
{
    volatile TIMECARD_FREQUENCY_REG *reg;
    ULONG control;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) || request->Counter == 0 ||
        request->Counter > TIMECARD_FREQUENCY_COUNT ||
        request->IntegrationSeconds > 0xffu)
        return STATUS_INVALID_PARAMETER;
    reg = context->Frequency[request->Counter - 1];
    if (reg == NULL)
        return STATUS_NOT_SUPPORTED;

    control = request->IntegrationSeconds == 0 ? 0 :
        (request->IntegrationSeconds << 8) | TIMECARD_FREQUENCY_ENABLE;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    WRITE_REGISTER_ULONG((PULONG)&reg->Control, control);
    status = TimeCardFrequencyQueryLocked(context, request->Counter,
                                          response);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}
