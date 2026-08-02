/* SPDX-License-Identifier: BSD-3-Clause */
/* Bounded signal-generator and frequency-counter control. */

#include "timecard.h"

#define TIMECARD_NANOSECONDS_PER_SECOND 1000000000ull
#define TIMECARD_SIGNAL_ENABLE_VALID    0x00000003u
#define TIMECARD_SIGNAL_STATUS_ERROR    0x00000001u
#define TIMECARD_SIGNAL_STATUS_TIME_JUMP 0x00000002u
#define TIMECARD_SIGNAL_LAYOUT_MAJOR    1u
#define TIMECARD_SIGNAL_LAYOUT_MINOR    3u
#define TIMECARD_SIGNAL_START_GUARD_NS  1000000ull
#define TIMECARD_FREQUENCY_ENABLE       0x00000001u
#define TIMECARD_FREQUENCY_VALID        (1u << 31)
#define TIMECARD_FREQUENCY_ERROR        (1u << 30)
#define TIMECARD_FREQUENCY_OVERRUN      (1u << 29)
#define TIMECARD_FREQUENCY_VALUE_MASK   ((1u << 24) - 1u)

C_ASSERT(sizeof(TIMECARD_SIGNAL_CONTROL) == 64);
C_ASSERT(sizeof(TIMECARD_SIGNAL_EVENT) == 32);
C_ASSERT(sizeof(TIMECARD_SIGNAL_EVENT_BATCH) == 544);
C_ASSERT(sizeof(TIMECARD_FREQUENCY_CONTROL) == 32);
C_ASSERT((TIMECARD_SIGNAL_EVENT_QUEUE_LENGTH &
          (TIMECARD_SIGNAL_EVENT_QUEUE_LENGTH - 1u)) == 0u);
C_ASSERT(FIELD_OFFSET(TIMECARD_SIGNAL_REG, StartNanoseconds) == 0x40);
C_ASSERT(FIELD_OFFSET(TIMECARD_SIGNAL_REG, RepeatCount) == 0x58);
C_ASSERT(FIELD_OFFSET(TIMECARD_SIGNAL_CONTROL,
                      CableDelayNanoseconds) == 0x1c);

static ULONGLONG
TimeCardJoinTime(ULONG seconds, ULONG nanoseconds)
{
    return (ULONGLONG)seconds * TIMECARD_NANOSECONDS_PER_SECOND +
           nanoseconds;
}

static BOOLEAN
TimeCardSignalLayoutSupported(ULONG version)
{
    ULONG major;
    ULONG minor;

    if (version == 0u || version == MAXULONG)
        return FALSE;
    major = version >> 24;
    minor = (version >> 16) & 0xffu;
    return major > TIMECARD_SIGNAL_LAYOUT_MAJOR ||
           (major == TIMECARD_SIGNAL_LAYOUT_MAJOR &&
            minor >= TIMECARD_SIGNAL_LAYOUT_MINOR);
}

static ULONG
TimeCardSignalMessageForGenerator(PDEVICE_CONTEXT context, ULONG generator)
{
    if (generator >= TIMECARD_SIGNAL_COUNT ||
        context->Layout == TIMECARD_LAYOUT_ART)
        return MAXULONG;
    return (context->Layout == TIMECARD_LAYOUT_MSIX ? 43u : 11u) +
        generator;
}

BOOLEAN
TimeCardSignalMessageRangeRelevant(PDEVICE_CONTEXT context, ULONG first,
                                   ULONG count)
{
    ULONG generator;

    for (generator = 0; generator < TIMECARD_SIGNAL_COUNT; ++generator) {
        ULONG message = TimeCardSignalMessageForGenerator(
            context, generator);

        if (context->Signal[generator] != NULL && message != MAXULONG &&
            message >= first && message - first < count)
            return TRUE;
    }
    return FALSE;
}

VOID
TimeCardSignalMarkInterruptRange(PDEVICE_CONTEXT context, ULONG first,
                                 ULONG count)
{
    ULONG generator;

    for (generator = 0; generator < TIMECARD_SIGNAL_COUNT; ++generator) {
        ULONG message = TimeCardSignalMessageForGenerator(
            context, generator);

        if (context->Signal[generator] != NULL && message != MAXULONG &&
            message >= first && message - first < count)
            context->SignalInterruptMask |= 1u << generator;
    }
}

static VOID
TimeCardSignalEventQueueClear(PDEVICE_CONTEXT context, ULONG generator,
                              BOOLEAN clearDropped)
{
    LONG head = InterlockedCompareExchange(
        &context->SignalEventHead[generator], 0, 0);

    InterlockedExchange(&context->SignalEventTail[generator], head);
    if (clearDropped)
        InterlockedExchange(&context->SignalEventDropped[generator], 0);
}

static VOID
TimeCardSignalEventQueuePush(PDEVICE_CONTEXT context, ULONG generator,
                             const TIMECARD_SIGNAL_EVENT *event)
{
    ULONG head = (ULONG)context->SignalEventHead[generator];
    ULONG next = (head + 1u) &
        (TIMECARD_SIGNAL_EVENT_QUEUE_LENGTH - 1u);

    if (next == (ULONG)context->SignalEventTail[generator]) {
        InterlockedIncrement(&context->SignalEventDropped[generator]);
        return;
    }
    context->SignalEventRing[generator][head] = *event;
    KeMemoryBarrier();
    InterlockedExchange(&context->SignalEventHead[generator], (LONG)next);
}

BOOLEAN
TimeCardHandleSignalInterrupt(PDEVICE_CONTEXT context, ULONG messageId)
{
    TIMECARD_SIGNAL_EVENT event;
    volatile TIMECARD_SIGNAL_REG *reg;
    ULONG generator;

    for (generator = 0; generator < TIMECARD_SIGNAL_COUNT; ++generator) {
        if (TimeCardSignalMessageForGenerator(context, generator) ==
            messageId)
            break;
    }
    if (generator == TIMECARD_SIGNAL_COUNT ||
        (context->SignalInterruptMask & (1u << generator)) == 0u)
        return FALSE;
    reg = context->Signal[generator];
    if (reg == NULL || !TimeCardSignalLayoutSupported(
            READ_REGISTER_ULONG((PULONG)&reg->Version)) ||
        (READ_REGISTER_ULONG((PULONG)&reg->Interrupt) & 1u) == 0u)
        return FALSE;

    RtlZeroMemory(&event, sizeof(event));
    event.SystemInterruptTime100ns = KeQueryInterruptTime();
    event.Sequence = (ULONGLONG)InterlockedIncrement64(
        &context->SignalEventSequence[generator]);
    event.Generator = generator + 1u;
    event.Flags = TIMECARD_SIGNAL_EVENT_FLAG_COMPLETED;
    event.Status = READ_REGISTER_ULONG((PULONG)&reg->Status);
    if ((event.Status & TIMECARD_SIGNAL_STATUS_ERROR) != 0u)
        event.Flags |= TIMECARD_SIGNAL_EVENT_FLAG_ERROR;
    if ((event.Status & TIMECARD_SIGNAL_STATUS_TIME_JUMP) != 0u)
        event.Flags |= TIMECARD_SIGNAL_EVENT_FLAG_TIME_JUMP;
    TimeCardSignalEventQueuePush(context, generator, &event);
    WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt, 1u);
    return TRUE;
}

VOID
TimeCardSignalInterruptInitialize(PDEVICE_CONTEXT context)
{
    ULONG generator;

    context->SignalInterruptMask = 0u;
    context->SignalRequestedInterruptMask = 0u;
    for (generator = 0; generator < TIMECARD_SIGNAL_COUNT; ++generator) {
        volatile TIMECARD_SIGNAL_REG *reg = context->Signal[generator];

        TimeCardSignalEventQueueClear(context, generator, TRUE);
        InterlockedExchange64(&context->SignalEventSequence[generator], 0);
        if (reg == NULL || !TimeCardSignalLayoutSupported(
                READ_REGISTER_ULONG((PULONG)&reg->Version)))
            continue;
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask, 0u);
        WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt, 1u);
    }
}

VOID
TimeCardSignalInterruptPowerDown(PDEVICE_CONTEXT context)
{
    ULONG generator;

    if (!context->HardwareReady)
        return;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    for (generator = 0; generator < TIMECARD_SIGNAL_COUNT; ++generator) {
        if (context->Signal[generator] != NULL &&
            TimeCardSignalLayoutSupported(READ_REGISTER_ULONG(
                (PULONG)&context->Signal[generator]->Version)))
            WRITE_REGISTER_ULONG(
                (PULONG)&context->Signal[generator]->InterruptMask, 0u);
    }
    WdfWaitLockRelease(context->RegisterLock);
}

VOID
TimeCardSignalInterruptPowerUp(PDEVICE_CONTEXT context)
{
    ULONG generator;

    if (!context->HardwareReady)
        return;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    for (generator = 0; generator < TIMECARD_SIGNAL_COUNT; ++generator) {
        volatile TIMECARD_SIGNAL_REG *reg = context->Signal[generator];

        if (reg == NULL || !TimeCardSignalLayoutSupported(
                READ_REGISTER_ULONG((PULONG)&reg->Version)) ||
            (context->SignalRequestedInterruptMask &
             context->SignalInterruptMask & (1u << generator)) == 0u)
            continue;
        WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt, 1u);
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask, 1u);
    }
    WdfWaitLockRelease(context->RegisterLock);
}

NTSTATUS
TimeCardSignalEventRead(PDEVICE_CONTEXT context,
                        const TIMECARD_SIGNAL_EVENT_BATCH *request,
                        TIMECARD_SIGNAL_EVENT_BATCH *response)
{
    ULONG generator;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) || request->Generator == 0u ||
        request->Generator > TIMECARD_SIGNAL_COUNT ||
        request->MaximumEvents == 0u ||
        request->MaximumEvents > TIMECARD_SIGNAL_EVENT_MAX_BATCH ||
        request->Count != 0u || request->DroppedEvents != 0u ||
        request->Flags != 0u || request->Reserved[0] != 0u ||
        request->Reserved[1] != 0u)
        return STATUS_INVALID_PARAMETER;
    generator = request->Generator - 1u;
    if (context->Signal[generator] == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Generator = request->Generator;
    response->MaximumEvents = request->MaximumEvents;
    while (response->Count < request->MaximumEvents) {
        ULONG tail = (ULONG)context->SignalEventTail[generator];

        if (tail == (ULONG)context->SignalEventHead[generator])
            break;
        KeMemoryBarrier();
        response->Events[response->Count++] =
            context->SignalEventRing[generator][tail];
        InterlockedExchange(
            &context->SignalEventTail[generator],
            (LONG)((tail + 1u) &
                   (TIMECARD_SIGNAL_EVENT_QUEUE_LENGTH - 1u)));
    }
    response->DroppedEvents = (ULONG)InterlockedCompareExchange(
        &context->SignalEventDropped[generator], 0, 0);
    if (response->DroppedEvents != 0u)
        response->Flags |= TIMECARD_SIGNAL_EVENT_FLAG_OVERFLOW;
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardReadClockLocked(PDEVICE_CONTEXT context, PULONGLONG nanoseconds)
{
    ULONG ctrl;
    ULONG oldControl;
    ULONG seconds;
    ULONG subsecond;
    ULONG i;

    oldControl = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl) &
                 ~OCP_CTRL_TRANSIENT_MASK;
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl,
                         oldControl | OCP_CTRL_READ_TIME_REQ |
                         OCP_CTRL_ENABLE);
    for (i = 0; i < 100; ++i) {
        ctrl = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl);
        if ((ctrl & OCP_CTRL_READ_TIME_DONE) != 0)
            break;
        KeStallExecutionProcessor(1);
    }
    if (i == 100) {
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, oldControl);
        return STATUS_IO_TIMEOUT;
    }

    subsecond = READ_REGISTER_ULONG((PULONG)&context->Regs->TimeNs);
    seconds = READ_REGISTER_ULONG((PULONG)&context->Regs->TimeSec);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, oldControl);
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
    ULONG version;

    reg = context->Signal[generator - 1];
    if (reg == NULL)
        return STATUS_NOT_SUPPORTED;
    version = READ_REGISTER_ULONG((PULONG)&reg->Version);
    if (!TimeCardSignalLayoutSupported(version))
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Generator = generator;
    control->Version = version;
    enable = READ_REGISTER_ULONG((PULONG)&reg->Enable);
    control->Status = READ_REGISTER_ULONG((PULONG)&reg->Status);
    polarity = READ_REGISTER_ULONG((PULONG)&reg->Polarity);
    control->RepeatCount = READ_REGISTER_ULONG((PULONG)&reg->RepeatCount);
    control->CableDelayNanoseconds =
        READ_REGISTER_ULONG((PULONG)&reg->CableDelay) & 0xffffu;
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
        control->Flags |= TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH;
    if ((control->Status & TIMECARD_SIGNAL_STATUS_ERROR) != 0)
        control->Flags |= TIMECARD_SIGNAL_FLAG_ERROR;
    if ((control->Status & TIMECARD_SIGNAL_STATUS_TIME_JUMP) != 0)
        control->Flags |= TIMECARD_SIGNAL_FLAG_TIME_JUMP;
    if ((context->SignalInterruptMask & (1u << (generator - 1u))) != 0u)
        control->Flags |= TIMECARD_SIGNAL_FLAG_COMPLETION_IRQ_AVAILABLE;
    if ((READ_REGISTER_ULONG((PULONG)&reg->Interrupt) & 1u) != 0u)
        control->Flags |= TIMECARD_SIGNAL_FLAG_COMPLETION_PENDING;
    if (InterlockedCompareExchange(
            &context->SignalEventDropped[generator - 1u], 0, 0) != 0)
        control->Flags |= TIMECARD_SIGNAL_FLAG_COMPLETION_OVERFLOW;
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
    ULONGLONG earliestStart;
    ULONGLONG start;
    ULONGLONG remainder;
    ULONGLONG maximum = ~(ULONGLONG)0;
    ULONG oldCableDelay;
    ULONG oldInterruptMask;
    ULONG oldPolarity;
    ULONG oldControl;
    ULONG oldRepeatCount;
    ULONG oldStartNanoseconds;
    ULONG oldStartSeconds;
    ULONG oldPulseNanoseconds;
    ULONG oldPulseSeconds;
    ULONG oldPeriodNanoseconds;
    ULONG oldPeriodSeconds;
    ULONG rawCableDelay;
    ULONG rawControl;
    ULONG rawPolarity;
    ULONG startNanoseconds = 0;
    ULONG startSeconds = 0;
    ULONG pulseNanoseconds = 0;
    ULONG pulseSeconds = 0;
    ULONG periodNanoseconds = 0;
    ULONG periodSeconds = 0;
    ULONG version;
    ULONG allowedFlags;
    BOOLEAN absoluteStart;
    BOOLEAN clearOnly;
    BOOLEAN clearStatus;
    BOOLEAN enabled;
    BOOLEAN activeHigh;
    BOOLEAN transactionModified = FALSE;
    BOOLEAN updateTiming;
    NTSTATUS status;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    allowedFlags = TIMECARD_SIGNAL_FLAG_PRESENT |
        TIMECARD_SIGNAL_FLAG_ENABLED | TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH |
        TIMECARD_SIGNAL_FLAG_ERROR | TIMECARD_SIGNAL_FLAG_TIME_JUMP |
        TIMECARD_SIGNAL_FLAG_ABSOLUTE_START |
        TIMECARD_SIGNAL_FLAG_CLEAR_STATUS |
        TIMECARD_SIGNAL_FLAG_COMPLETION_IRQ_AVAILABLE |
        TIMECARD_SIGNAL_FLAG_COMPLETION_PENDING |
        TIMECARD_SIGNAL_FLAG_COMPLETION_OVERFLOW;
    if (request->Size < sizeof(*request) || request->Generator == 0 ||
        request->Generator > TIMECARD_SIGNAL_COUNT ||
        request->CableDelayNanoseconds > 0xffffu ||
        (request->Flags & ~allowedFlags) != 0)
        return STATUS_INVALID_PARAMETER;
    reg = context->Signal[request->Generator - 1];
    if (reg == NULL)
        return STATUS_NOT_SUPPORTED;

    absoluteStart =
        (request->Flags & TIMECARD_SIGNAL_FLAG_ABSOLUTE_START) != 0;
    clearStatus =
        (request->Flags & TIMECARD_SIGNAL_FLAG_CLEAR_STATUS) != 0;
    enabled = (request->Flags & TIMECARD_SIGNAL_FLAG_ENABLED) != 0;
    period = request->PeriodNanoseconds;
    pulse = request->PulseNanoseconds;
    phase = request->PhaseNanoseconds;
    updateTiming = absoluteStart || enabled || period != 0 || pulse != 0 ||
                   phase != 0;
    clearOnly = clearStatus &&
        (request->Flags & (TIMECARD_SIGNAL_FLAG_ENABLED |
                           TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH |
                           TIMECARD_SIGNAL_FLAG_ABSOLUTE_START)) == 0 &&
        request->RepeatCount == 0 &&
        request->CableDelayNanoseconds == 0 &&
        request->StartSeconds == 0 && request->StartNanoseconds == 0 &&
        period == 0 && pulse == 0 && phase == 0;
    if (absoluteStart &&
        (request->StartSeconds > MAXULONG ||
         request->StartNanoseconds >= TIMECARD_NANOSECONDS_PER_SECOND ||
         phase != 0))
        return STATUS_INVALID_PARAMETER;
    if (updateTiming &&
        (period < 2 || pulse == 0 || pulse >= period ||
         (!absoluteStart && phase >= period) ||
         period / TIMECARD_NANOSECONDS_PER_SECOND > MAXULONG))
        return STATUS_INVALID_PARAMETER;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    version = READ_REGISTER_ULONG((PULONG)&reg->Version);
    if (!TimeCardSignalLayoutSupported(version)) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    if (clearOnly) {
        WRITE_REGISTER_ULONG((PULONG)&reg->Status,
                             TIMECARD_SIGNAL_STATUS_ERROR |
                             TIMECARD_SIGNAL_STATUS_TIME_JUMP);
        status = TimeCardSignalQueryLocked(context, request->Generator,
                                           response);
        goto done;
    }

    /*
     * Complete every fallible PHC read and start-time calculation before
     * disabling a running generator. A timeout or overflow therefore leaves
     * both Enable and InterruptMask exactly as they were.
     */
    if (updateTiming) {
        status = TimeCardReadClockLocked(context, &now);
        if (!NT_SUCCESS(status))
            goto done;
        if (maximum - now < TIMECARD_SIGNAL_START_GUARD_NS) {
            status = STATUS_INTEGER_OVERFLOW;
            goto done;
        }
        earliestStart = now + TIMECARD_SIGNAL_START_GUARD_NS;
        if (absoluteStart) {
            start = TimeCardJoinTime((ULONG)request->StartSeconds,
                                     request->StartNanoseconds);
            if (start <= earliestStart) {
                status = STATUS_INVALID_PARAMETER;
                goto done;
            }
        } else {
            remainder = earliestStart % period;
            start = earliestStart - remainder;
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
            if (start <= earliestStart) {
                if (maximum - start < period) {
                    status = STATUS_INTEGER_OVERFLOW;
                    goto done;
                }
                start += period;
            }
        }
        if (start / TIMECARD_NANOSECONDS_PER_SECOND > MAXULONG) {
            status = STATUS_INTEGER_OVERFLOW;
            goto done;
        }
        startSeconds = (ULONG)(start / TIMECARD_NANOSECONDS_PER_SECOND);
        startNanoseconds = (ULONG)(start % TIMECARD_NANOSECONDS_PER_SECOND);
        periodSeconds =
            (ULONG)(period / TIMECARD_NANOSECONDS_PER_SECOND);
        periodNanoseconds =
            (ULONG)(period % TIMECARD_NANOSECONDS_PER_SECOND);
        pulseSeconds =
            (ULONG)(pulse / TIMECARD_NANOSECONDS_PER_SECOND);
        pulseNanoseconds =
            (ULONG)(pulse % TIMECARD_NANOSECONDS_PER_SECOND);
    }

    oldInterruptMask = READ_REGISTER_ULONG((PULONG)&reg->InterruptMask);
    oldControl = READ_REGISTER_ULONG((PULONG)&reg->Enable);
    oldPolarity = READ_REGISTER_ULONG((PULONG)&reg->Polarity);
    oldCableDelay = READ_REGISTER_ULONG((PULONG)&reg->CableDelay);
    oldRepeatCount = READ_REGISTER_ULONG((PULONG)&reg->RepeatCount);
    oldStartSeconds = READ_REGISTER_ULONG((PULONG)&reg->StartSeconds);
    oldStartNanoseconds =
        READ_REGISTER_ULONG((PULONG)&reg->StartNanoseconds);
    oldPeriodSeconds = READ_REGISTER_ULONG((PULONG)&reg->PeriodSeconds);
    oldPeriodNanoseconds =
        READ_REGISTER_ULONG((PULONG)&reg->PeriodNanoseconds);
    oldPulseSeconds = READ_REGISTER_ULONG((PULONG)&reg->PulseSeconds);
    oldPulseNanoseconds =
        READ_REGISTER_ULONG((PULONG)&reg->PulseNanoseconds);
    rawControl = oldControl;
    WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask,
                         oldInterruptMask & ~1u);
    WRITE_REGISTER_ULONG((PULONG)&reg->Enable, rawControl & ~3u);
    transactionModified = TRUE;
    rawPolarity = (oldPolarity & ~1u) |
        ((request->Flags & TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH) != 0 ? 1u : 0u);
    WRITE_REGISTER_ULONG((PULONG)&reg->Polarity, rawPolarity);
    rawCableDelay = (oldCableDelay & ~0xffffu) |
                    request->CableDelayNanoseconds;
    WRITE_REGISTER_ULONG((PULONG)&reg->CableDelay, rawCableDelay);
    WRITE_REGISTER_ULONG((PULONG)&reg->RepeatCount, request->RepeatCount);

    if (!updateTiming)
        goto verify;

    WRITE_REGISTER_ULONG((PULONG)&reg->StartSeconds,
                         startSeconds);
    WRITE_REGISTER_ULONG((PULONG)&reg->StartNanoseconds,
                         startNanoseconds);
    WRITE_REGISTER_ULONG((PULONG)&reg->PeriodSeconds,
                         periodSeconds);
    WRITE_REGISTER_ULONG((PULONG)&reg->PeriodNanoseconds,
                         periodNanoseconds);
    WRITE_REGISTER_ULONG((PULONG)&reg->PulseSeconds,
                         pulseSeconds);
    WRITE_REGISTER_ULONG((PULONG)&reg->PulseNanoseconds,
                         pulseNanoseconds);
    WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt, 1u);
    if (enabled &&
        (context->SignalInterruptMask &
         (1u << (request->Generator - 1u))) != 0u) {
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask, 1u);
    }
    if (enabled) {
        WRITE_REGISTER_ULONG((PULONG)&reg->Enable,
                             (rawControl & ~3u) |
                             TIMECARD_SIGNAL_ENABLE_VALID);
    }
verify:
    status = TimeCardSignalQueryLocked(context, request->Generator, response);
    if (!NT_SUCCESS(status))
        goto rollback_runtime;
    activeHigh = (request->Flags & TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH) != 0;
    if ((((response->Flags & TIMECARD_SIGNAL_FLAG_ENABLED) != 0) !=
         enabled) ||
        (((response->Flags & TIMECARD_SIGNAL_FLAG_ACTIVE_HIGH) != 0) !=
         activeHigh) ||
        response->CableDelayNanoseconds != request->CableDelayNanoseconds ||
        response->RepeatCount != request->RepeatCount ||
        (updateTiming &&
         (response->PeriodNanoseconds != request->PeriodNanoseconds ||
          response->PulseNanoseconds != request->PulseNanoseconds ||
          (absoluteStart ?
           (response->StartSeconds != request->StartSeconds ||
            response->StartNanoseconds != request->StartNanoseconds) :
           response->PhaseNanoseconds != request->PhaseNanoseconds)))) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto rollback_runtime;
    }
    if (clearStatus) {
        WRITE_REGISTER_ULONG((PULONG)&reg->Status,
                             TIMECARD_SIGNAL_STATUS_ERROR |
                             TIMECARD_SIGNAL_STATUS_TIME_JUMP);
        status = TimeCardSignalQueryLocked(context, request->Generator,
                                           response);
        if (!NT_SUCCESS(status))
            goto rollback_runtime;
    }
    if (enabled)
        context->SignalRequestedInterruptMask |=
            1u << (request->Generator - 1u);
    else
        context->SignalRequestedInterruptMask &=
            ~(1u << (request->Generator - 1u));
    transactionModified = FALSE;
    goto done;

rollback_runtime:
    /* Restore every writable register before returning to the prior run state. */
    if (transactionModified) {
        WRITE_REGISTER_ULONG((PULONG)&reg->Enable, oldControl & ~3u);
        WRITE_REGISTER_ULONG((PULONG)&reg->Polarity, oldPolarity);
        WRITE_REGISTER_ULONG((PULONG)&reg->CableDelay, oldCableDelay);
        WRITE_REGISTER_ULONG((PULONG)&reg->RepeatCount, oldRepeatCount);
        WRITE_REGISTER_ULONG((PULONG)&reg->StartSeconds, oldStartSeconds);
        WRITE_REGISTER_ULONG((PULONG)&reg->StartNanoseconds,
                             oldStartNanoseconds);
        WRITE_REGISTER_ULONG((PULONG)&reg->PeriodSeconds, oldPeriodSeconds);
        WRITE_REGISTER_ULONG((PULONG)&reg->PeriodNanoseconds,
                             oldPeriodNanoseconds);
        WRITE_REGISTER_ULONG((PULONG)&reg->PulseSeconds, oldPulseSeconds);
        WRITE_REGISTER_ULONG((PULONG)&reg->PulseNanoseconds,
                             oldPulseNanoseconds);
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask,
                             oldInterruptMask);
        WRITE_REGISTER_ULONG((PULONG)&reg->Enable, oldControl);
    }

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
