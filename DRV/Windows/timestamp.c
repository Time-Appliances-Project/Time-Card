/* SPDX-License-Identifier: BSD-3-Clause */
/* Interrupt-backed access to the documented Signal Timestamper cores. */

#include "timecard.h"

#define TIMECARD_TIMESTAMP_ENABLE_MASK 0x00000001u
#define TIMECARD_TIMESTAMP_ERROR_MASK  0x00000001u
#define TIMECARD_TIMESTAMP_IRQ_MASK    0x00000001u
#define TIMECARD_TIMESTAMP_CABLE_MAX   0x0000ffffu
#define TIMECARD_TIMESTAMP_DATA_WORDS \
    (TIMECARD_TIMESTAMP_MAX_DATA_BYTES / sizeof(ULONG))

C_ASSERT((TIMECARD_TIMESTAMP_QUEUE_LENGTH &
          (TIMECARD_TIMESTAMP_QUEUE_LENGTH - 1u)) == 0u);
C_ASSERT(sizeof(TIMECARD_TIMESTAMP_EVENT) == 80u);
C_ASSERT(sizeof(TIMECARD_TIMESTAMP_CONTROL) == 80u);
C_ASSERT(sizeof(TIMECARD_TIMESTAMP_BATCH) == 1312u);
C_ASSERT(FIELD_OFFSET(TIMECARD_TIMESTAMP_REG, CableDelay) == 0x20u);
C_ASSERT(FIELD_OFFSET(TIMECARD_TIMESTAMP_REG, Interrupt) == 0x30u);
C_ASSERT(FIELD_OFFSET(TIMECARD_TIMESTAMP_REG, EventCount) == 0x38u);
C_ASSERT(FIELD_OFFSET(TIMECARD_TIMESTAMP_REG, TimestampCount) == 0x40u);
C_ASSERT(FIELD_OFFSET(TIMECARD_TIMESTAMP_REG, Data) == 0x50u);

static BOOLEAN
TimeCardTimestampVersionPresent(ULONG version)
{
    return version != 0u && version != MAXULONG && (version >> 24) >= 1u;
}

static BOOLEAN
TimeCardTimestampVersionAtLeast(ULONG version, ULONG major, ULONG minor)
{
    ULONG actualMajor = version >> 24;
    ULONG actualMinor = (version >> 16) & 0xffu;

    return actualMajor > major ||
           (actualMajor == major && actualMinor >= minor);
}

/*
 * The upstream ART resource table reserves only 0x20 bytes for each
 * timestamper even though its established interrupt path reads the timestamp
 * at 0x44/0x48.  Keep ART on that proven basic surface.  The standard
 * Meta/Celestica apertures cover the complete current-layout register block.
 */
static BOOLEAN
TimeCardTimestampExtendedSurface(PDEVICE_CONTEXT context)
{
    return context->BoardProfile != TIMECARD_BOARD_ART;
}

static ULONG
TimeCardTimestampMessageForChannel(PDEVICE_CONTEXT context, ULONG channel)
{
    static const ULONG msiMessages[TIMECARD_TIMESTAMP_COUNT] = {
        1u, 2u, 6u, 15u, 16u, 0u
    };
    static const ULONG msixMessages[TIMECARD_TIMESTAMP_COUNT] = {
        33u, 34u, 38u, 47u, 48u, 32u
    };
    static const ULONG artMessages[TIMECARD_TIMESTAMP_COUNT] = {
        12u, 8u, 10u, 14u, 15u, 11u
    };
    const ULONG *messages;

    if (channel >= TIMECARD_TIMESTAMP_COUNT)
        return MAXULONG;
    messages = context->Layout == TIMECARD_LAYOUT_MSIX ?
        msixMessages : context->Layout == TIMECARD_LAYOUT_ART ?
        artMessages : msiMessages;
    return messages[channel];
}

static ULONG
TimeCardTimestampChannelForMessage(PDEVICE_CONTEXT context, ULONG messageId)
{
    ULONG channel;

    for (channel = 0; channel < TIMECARD_TIMESTAMP_COUNT; ++channel) {
        if (TimeCardTimestampMessageForChannel(context, channel) == messageId)
            return channel;
    }
    return MAXULONG;
}

BOOLEAN
TimeCardTimestampMessageRangeRelevant(PDEVICE_CONTEXT context, ULONG first,
                                      ULONG count)
{
    ULONG channel;

    for (channel = 0; channel < TIMECARD_TIMESTAMP_COUNT; ++channel) {
        ULONG message = TimeCardTimestampMessageForChannel(context, channel);

        if (context->Timestamp[channel] != NULL && message >= first &&
            message - first < count) {
            return TRUE;
        }
    }
    return FALSE;
}

VOID
TimeCardTimestampMarkInterruptRange(PDEVICE_CONTEXT context, ULONG first,
                                    ULONG count)
{
    ULONG channel;

    for (channel = 0; channel < TIMECARD_TIMESTAMP_COUNT; ++channel) {
        ULONG message = TimeCardTimestampMessageForChannel(context, channel);

        if (context->Timestamp[channel] != NULL && message >= first &&
            message - first < count) {
            context->TimestampInterruptMask |= 1u << channel;
        }
    }
}

static ULONG
TimeCardTimestampQueueDepth(PDEVICE_CONTEXT context, ULONG channel)
{
    ULONG head = (ULONG)context->TimestampHead[channel];
    ULONG tail = (ULONG)context->TimestampTail[channel];

    return (head - tail) & (TIMECARD_TIMESTAMP_QUEUE_LENGTH - 1u);
}

static VOID
TimeCardTimestampQueueClear(PDEVICE_CONTEXT context, ULONG channel,
                            BOOLEAN clearDropped)
{
    LONG head = InterlockedCompareExchange(
        &context->TimestampHead[channel], 0, 0);

    InterlockedExchange(&context->TimestampTail[channel], head);
    if (clearDropped)
        InterlockedExchange(&context->TimestampDropped[channel], 0);
}

static BOOLEAN
TimeCardTimestampReadLatched(volatile TIMECARD_TIMESTAMP_REG *reg,
                             TIMECARD_TIME *time, PULONG count)
{
    ULONG before;
    ULONG after;
    ULONG attempt;

    for (attempt = 0; attempt < 4u; ++attempt) {
        before = READ_REGISTER_ULONG((PULONG)&reg->TimestampCount);
        time->Nanoseconds = READ_REGISTER_ULONG((PULONG)&reg->TimeNanoseconds);
        time->Seconds = READ_REGISTER_ULONG((PULONG)&reg->TimeSeconds);
        after = READ_REGISTER_ULONG((PULONG)&reg->TimestampCount);
        if (before == after) {
            time->Reserved = 0u;
            *count = after;
            return after != 0u && time->Nanoseconds < 1000000000u;
        }
    }
    RtlZeroMemory(time, sizeof(*time));
    *count = 0u;
    return FALSE;
}

static NTSTATUS
TimeCardTimestampQueryLocked(PDEVICE_CONTEXT context, ULONG channel,
                             TIMECARD_TIMESTAMP_CONTROL *control)
{
    volatile TIMECARD_TIMESTAMP_REG *reg;
    ULONG rawPolarity;
    ULONG timestampCount;
    ULONG version;
    BOOLEAN valid;

    if (channel >= TIMECARD_TIMESTAMP_COUNT)
        return STATUS_INVALID_PARAMETER;
    reg = context->Timestamp[channel];
    if (!context->HardwareReady || reg == NULL)
        return STATUS_NOT_SUPPORTED;
    version = READ_REGISTER_ULONG((PULONG)&reg->Version);
    if (!TimeCardTimestampVersionPresent(version) ||
        !TimeCardTimestampVersionAtLeast(version, 1u, 3u))
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Channel = channel;
    control->Version = version;
    control->Flags = TIMECARD_TIMESTAMP_FLAG_PRESENT;
    if ((context->TimestampInterruptMask & (1u << channel)) != 0u)
        control->Flags |= TIMECARD_TIMESTAMP_FLAG_IRQ_AVAILABLE;
    if ((READ_REGISTER_ULONG((PULONG)&reg->Enable) &
         TIMECARD_TIMESTAMP_ENABLE_MASK) != 0u) {
        control->Flags |= TIMECARD_TIMESTAMP_FLAG_ENABLED;
    }
    if (TimeCardTimestampVersionAtLeast(version, 1u, 2u)) {
        control->Status = READ_REGISTER_ULONG((PULONG)&reg->Error);
        if ((control->Status & TIMECARD_TIMESTAMP_ERROR_MASK) != 0u)
            control->Flags |= TIMECARD_TIMESTAMP_FLAG_DROP_ERROR;
    }
    rawPolarity = READ_REGISTER_ULONG((PULONG)&reg->Polarity) & 1u;
    control->Polarity = rawPolarity != 0u ?
        TIMECARD_TIMESTAMP_POLARITY_RISING :
        TIMECARD_TIMESTAMP_POLARITY_FALLING;
    control->Interrupt = READ_REGISTER_ULONG((PULONG)&reg->Interrupt) & 1u;
    control->InterruptMask = READ_REGISTER_ULONG(
        (PULONG)&reg->InterruptMask) & 1u;
    if (TimeCardTimestampExtendedSurface(context)) {
        control->Flags |=
            TIMECARD_TIMESTAMP_FLAG_CABLE_DELAY_WRITABLE |
            TIMECARD_TIMESTAMP_FLAG_COUNTERS_AVAILABLE |
            TIMECARD_TIMESTAMP_FLAG_DATA_AVAILABLE;
        control->CableDelayNanoseconds = READ_REGISTER_ULONG(
            (PULONG)&reg->CableDelay) & TIMECARD_TIMESTAMP_CABLE_MAX;
        control->EventCount = READ_REGISTER_ULONG((PULONG)&reg->EventCount);
        valid = TimeCardTimestampReadLatched(
            reg, &control->Time, &timestampCount);
        control->TimestampCount = timestampCount;
        control->DataWidth = READ_REGISTER_ULONG((PULONG)&reg->DataWidth);
        if (control->DataWidth != 0u) {
            control->Data = READ_REGISTER_ULONG((PULONG)&reg->Data);
            control->Flags |= TIMECARD_TIMESTAMP_FLAG_DATA_VALID;
            if (control->DataWidth > TIMECARD_TIMESTAMP_MAX_DATA_BYTES * 8u)
                control->Flags |= TIMECARD_TIMESTAMP_FLAG_DATA_TRUNCATED;
        }
    } else {
        control->Time.Nanoseconds = READ_REGISTER_ULONG(
            (PULONG)&reg->TimeNanoseconds);
        control->Time.Seconds = READ_REGISTER_ULONG(
            (PULONG)&reg->TimeSeconds);
        control->Time.Reserved = 0u;
        valid = control->Time.Nanoseconds < 1000000000u;
    }
    if (valid)
        control->Flags |= TIMECARD_TIMESTAMP_FLAG_EVENT_VALID;
    control->QueueDepth = TimeCardTimestampQueueDepth(context, channel);
    control->DroppedEvents = (ULONG)InterlockedCompareExchange(
        &context->TimestampDropped[channel], 0, 0);
    if (control->DroppedEvents != 0u)
        control->Flags |= TIMECARD_TIMESTAMP_FLAG_QUEUE_OVERFLOW;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardTimestampQuery(PDEVICE_CONTEXT context, ULONG channel,
                       TIMECARD_TIMESTAMP_CONTROL *control)
{
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardTimestampQueryLocked(context, channel, control);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardTimestampSet(PDEVICE_CONTEXT context,
                     const TIMECARD_TIMESTAMP_CONTROL *request,
                     TIMECARD_TIMESTAMP_CONTROL *response)
{
    volatile TIMECARD_TIMESTAMP_REG *reg;
    ULONG allowedFlags;
    ULONG oldCable = 0u;
    ULONG oldEnable;
    ULONG oldMask;
    ULONG oldPolarity;
    ULONG rawPolarity;
    ULONG version;
    BOOLEAN enabled;
    NTSTATUS status;

    allowedFlags = TIMECARD_TIMESTAMP_FLAG_PRESENT |
        TIMECARD_TIMESTAMP_FLAG_ENABLED |
        TIMECARD_TIMESTAMP_FLAG_DROP_ERROR |
        TIMECARD_TIMESTAMP_FLAG_EVENT_VALID |
        TIMECARD_TIMESTAMP_FLAG_DATA_VALID |
        TIMECARD_TIMESTAMP_FLAG_IRQ_AVAILABLE |
        TIMECARD_TIMESTAMP_FLAG_QUEUE_OVERFLOW |
        TIMECARD_TIMESTAMP_FLAG_DATA_TRUNCATED |
        TIMECARD_TIMESTAMP_FLAG_CABLE_DELAY_WRITABLE |
        TIMECARD_TIMESTAMP_FLAG_COUNTERS_AVAILABLE |
        TIMECARD_TIMESTAMP_FLAG_DATA_AVAILABLE |
        TIMECARD_TIMESTAMP_FLAG_CLEAR_ERROR |
        TIMECARD_TIMESTAMP_FLAG_CLEAR_QUEUE;
    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        request->Channel >= TIMECARD_TIMESTAMP_COUNT ||
        request->Polarity > TIMECARD_TIMESTAMP_POLARITY_FALLING ||
        request->CableDelayNanoseconds > TIMECARD_TIMESTAMP_CABLE_MAX ||
        request->Reserved != 0u || (request->Flags & ~allowedFlags) != 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    enabled = (request->Flags & TIMECARD_TIMESTAMP_FLAG_ENABLED) != 0u;
    if (enabled &&
        (context->TimestampInterruptMask & (1u << request->Channel)) == 0u) {
        return STATUS_NOT_SUPPORTED;
    }
    if (context->BoardProfile == TIMECARD_BOARD_ART &&
        context->PhaseCaptureEnabled &&
        (request->Channel == TIMECARD_TIMESTAMP_GNSS1 ||
         request->Channel == TIMECARD_TIMESTAMP_PHC)) {
        return STATUS_DEVICE_BUSY;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    reg = context->Timestamp[request->Channel];
    if (reg == NULL) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    version = READ_REGISTER_ULONG((PULONG)&reg->Version);
    if (!TimeCardTimestampVersionPresent(version) ||
        !TimeCardTimestampVersionAtLeast(version, 1u, 3u)) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    if (request->CableDelayNanoseconds != 0u &&
        (!TimeCardTimestampVersionAtLeast(version, 1u, 3u) ||
         !TimeCardTimestampExtendedSurface(context))) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    if ((request->Flags & TIMECARD_TIMESTAMP_FLAG_CLEAR_ERROR) != 0u &&
        !TimeCardTimestampVersionAtLeast(version, 1u, 2u)) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }

    oldEnable = READ_REGISTER_ULONG((PULONG)&reg->Enable);
    oldMask = READ_REGISTER_ULONG((PULONG)&reg->InterruptMask);
    oldPolarity = READ_REGISTER_ULONG((PULONG)&reg->Polarity);
    if (TimeCardTimestampExtendedSurface(context))
        oldCable = READ_REGISTER_ULONG((PULONG)&reg->CableDelay);

    WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask, 0u);
    WRITE_REGISTER_ULONG((PULONG)&reg->Enable, 0u);
    rawPolarity = request->Polarity == TIMECARD_TIMESTAMP_POLARITY_RISING ?
        1u : 0u;
    WRITE_REGISTER_ULONG((PULONG)&reg->Polarity,
        (oldPolarity & ~1u) | rawPolarity);
    if (TimeCardTimestampExtendedSurface(context)) {
        WRITE_REGISTER_ULONG((PULONG)&reg->CableDelay,
            (oldCable & ~TIMECARD_TIMESTAMP_CABLE_MAX) |
            request->CableDelayNanoseconds);
    }
    if (enabled) {
        WRITE_REGISTER_ULONG((PULONG)&reg->Enable,
                             TIMECARD_TIMESTAMP_ENABLE_MASK);
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask,
                             TIMECARD_TIMESTAMP_IRQ_MASK);
    }

    status = TimeCardTimestampQueryLocked(
        context, request->Channel, response);
    if (!NT_SUCCESS(status) ||
        (((response->Flags & TIMECARD_TIMESTAMP_FLAG_ENABLED) != 0u) !=
         enabled) || response->Polarity != request->Polarity ||
        (TimeCardTimestampExtendedSurface(context) &&
         response->CableDelayNanoseconds !=
             request->CableDelayNanoseconds)) {
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask, 0u);
        WRITE_REGISTER_ULONG((PULONG)&reg->Enable, 0u);
        WRITE_REGISTER_ULONG((PULONG)&reg->Polarity, oldPolarity);
        if (TimeCardTimestampExtendedSurface(context))
            WRITE_REGISTER_ULONG((PULONG)&reg->CableDelay, oldCable);
        WRITE_REGISTER_ULONG((PULONG)&reg->Enable, oldEnable);
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask, oldMask);
        if (NT_SUCCESS(status))
            status = STATUS_DEVICE_DATA_ERROR;
        goto done;
    }

    /* Clear latched state only after every reversible setting read back. */
    if ((request->Flags & TIMECARD_TIMESTAMP_FLAG_CLEAR_ERROR) != 0u)
        WRITE_REGISTER_ULONG((PULONG)&reg->Error,
                             TIMECARD_TIMESTAMP_ERROR_MASK);
    if ((request->Flags & TIMECARD_TIMESTAMP_FLAG_CLEAR_QUEUE) != 0u)
        TimeCardTimestampQueueClear(context, request->Channel, TRUE);
    WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt,
                         TIMECARD_TIMESTAMP_IRQ_MASK);
    if ((request->Flags & TIMECARD_TIMESTAMP_FLAG_CLEAR_ERROR) != 0u) {
        response->Status = READ_REGISTER_ULONG((PULONG)&reg->Error);
        if ((response->Status & TIMECARD_TIMESTAMP_ERROR_MASK) == 0u)
            response->Flags &= ~TIMECARD_TIMESTAMP_FLAG_DROP_ERROR;
    }
    if ((request->Flags & TIMECARD_TIMESTAMP_FLAG_CLEAR_QUEUE) != 0u) {
        response->QueueDepth = TimeCardTimestampQueueDepth(
            context, request->Channel);
        response->DroppedEvents = (ULONG)InterlockedCompareExchange(
            &context->TimestampDropped[request->Channel], 0, 0);
        if (response->DroppedEvents == 0u)
            response->Flags &= ~TIMECARD_TIMESTAMP_FLAG_QUEUE_OVERFLOW;
    }
    response->Interrupt = READ_REGISTER_ULONG(
        (PULONG)&reg->Interrupt) & TIMECARD_TIMESTAMP_IRQ_MASK;

    if (enabled)
        context->TimestampRequestedMask |= 1u << request->Channel;
    else
        context->TimestampRequestedMask &= ~(1u << request->Channel);
    context->TimestampRequestedPolarity[request->Channel] =
        (UCHAR)request->Polarity;
    context->TimestampRequestedCableDelay[request->Channel] =
        (USHORT)request->CableDelayNanoseconds;
done:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardTimestampRead(PDEVICE_CONTEXT context,
                      const TIMECARD_TIMESTAMP_BATCH *request,
                      TIMECARD_TIMESTAMP_BATCH *response)
{
    const UCHAR *eventBytes;
    ULONG channel;
    ULONG index;
    ULONG maximum;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        request->Channel >= TIMECARD_TIMESTAMP_COUNT ||
        request->MaximumEvents == 0u ||
        request->MaximumEvents > TIMECARD_TIMESTAMP_MAX_BATCH ||
        request->Count != 0u || request->DroppedEvents != 0u ||
        request->Flags != 0u || request->Reserved[0] != 0u ||
        request->Reserved[1] != 0u)
        return STATUS_INVALID_PARAMETER;
    eventBytes = (const UCHAR *)request->Events;
    for (index = 0u; index < sizeof(request->Events); ++index) {
        if (eventBytes[index] != 0u)
            return STATUS_INVALID_PARAMETER;
    }
    channel = request->Channel;
    if (context->Timestamp[channel] == NULL)
        return STATUS_NOT_SUPPORTED;

    maximum = request->MaximumEvents;
    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Channel = channel;
    response->MaximumEvents = maximum;
    while (response->Count < maximum) {
        ULONG tail = (ULONG)context->TimestampTail[channel];

        if (tail == (ULONG)context->TimestampHead[channel])
            break;
        KeMemoryBarrier();
        response->Events[response->Count] =
            context->TimestampRing[channel][tail];
        ++response->Count;
        InterlockedExchange(
            &context->TimestampTail[channel],
            (LONG)((tail + 1u) &
                   (TIMECARD_TIMESTAMP_QUEUE_LENGTH - 1u)));
    }
    response->DroppedEvents = (ULONG)InterlockedCompareExchange(
        &context->TimestampDropped[channel], 0, 0);
    if (response->DroppedEvents != 0u)
        response->Flags |= TIMECARD_TIMESTAMP_FLAG_QUEUE_OVERFLOW;
    return STATUS_SUCCESS;
}

static VOID
TimeCardTimestampQueuePush(PDEVICE_CONTEXT context, ULONG channel,
                           const TIMECARD_TIMESTAMP_EVENT *event)
{
    ULONG head = (ULONG)context->TimestampHead[channel];
    ULONG next = (head + 1u) &
        (TIMECARD_TIMESTAMP_QUEUE_LENGTH - 1u);

    if (next == (ULONG)context->TimestampTail[channel]) {
        InterlockedIncrement(&context->TimestampDropped[channel]);
        return;
    }
    context->TimestampRing[channel][head] = *event;
    KeMemoryBarrier();
    InterlockedExchange(&context->TimestampHead[channel], (LONG)next);
}

BOOLEAN
TimeCardHandleTimestampInterrupt(PDEVICE_CONTEXT context, ULONG messageId)
{
    TIMECARD_TIMESTAMP_EVENT event;
    volatile TIMECARD_TIMESTAMP_REG *reg;
    ULONG channel;
    ULONG dataWords;
    ULONG index;

    channel = TimeCardTimestampChannelForMessage(context, messageId);
    if (channel == MAXULONG ||
        (context->TimestampInterruptMask & (1u << channel)) == 0u)
        return FALSE;
    reg = context->Timestamp[channel];
    if (reg == NULL ||
        (READ_REGISTER_ULONG((PULONG)&reg->Interrupt) & 1u) == 0u)
        return FALSE;

    RtlZeroMemory(&event, sizeof(event));
    event.Flags = TIMECARD_TIMESTAMP_FLAG_PRESENT |
                  TIMECARD_TIMESTAMP_FLAG_EVENT_VALID;
    event.Error = READ_REGISTER_ULONG((PULONG)&reg->Error);
    if ((event.Error & TIMECARD_TIMESTAMP_ERROR_MASK) != 0u)
        event.Flags |= TIMECARD_TIMESTAMP_FLAG_DROP_ERROR;
    if (TimeCardTimestampExtendedSurface(context)) {
        event.EventCount = READ_REGISTER_ULONG((PULONG)&reg->EventCount);
        event.TimestampCount = READ_REGISTER_ULONG(
            (PULONG)&reg->TimestampCount);
    }
    event.Time.Nanoseconds = READ_REGISTER_ULONG(
        (PULONG)&reg->TimeNanoseconds);
    event.Time.Seconds = READ_REGISTER_ULONG((PULONG)&reg->TimeSeconds);
    event.Time.Reserved = 0u;
    if (event.Time.Nanoseconds >= 1000000000u)
        event.Flags &= ~TIMECARD_TIMESTAMP_FLAG_EVENT_VALID;

    if (TimeCardTimestampExtendedSurface(context)) {
        event.DataWidth = READ_REGISTER_ULONG((PULONG)&reg->DataWidth);
        if (event.DataWidth > TIMECARD_TIMESTAMP_MAX_DATA_BYTES * 8u) {
            dataWords = TIMECARD_TIMESTAMP_DATA_WORDS;
            event.Flags |= TIMECARD_TIMESTAMP_FLAG_DATA_TRUNCATED;
        } else {
            dataWords = (event.DataWidth + 31u) / 32u;
        }
        if (dataWords != 0u) {
            volatile ULONG *data = (volatile ULONG *)&reg->Data;

            for (index = 0; index < dataWords; ++index)
                event.Data[index] = READ_REGISTER_ULONG((PULONG)&data[index]);
            event.Flags |= TIMECARD_TIMESTAMP_FLAG_DATA_VALID;
        }
    }
    TimeCardTimestampQueuePush(context, channel, &event);
    WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt,
                         TIMECARD_TIMESTAMP_IRQ_MASK);
    return TRUE;
}

VOID
TimeCardTimestampInitialize(PDEVICE_CONTEXT context)
{
    ULONG channel;

    context->TimestampInterruptMask = 0u;
    for (channel = 0; channel < TIMECARD_TIMESTAMP_COUNT; ++channel) {
        volatile TIMECARD_TIMESTAMP_REG *reg = context->Timestamp[channel];

        TimeCardTimestampQueueClear(context, channel, TRUE);
        if (reg == NULL)
            continue;
        {
            ULONG version = READ_REGISTER_ULONG((PULONG)&reg->Version);

            if (!TimeCardTimestampVersionPresent(version) ||
                !TimeCardTimestampVersionAtLeast(version, 1u, 3u)) {
                context->Timestamp[channel] = NULL;
                continue;
            }
        }
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask, 0u);
        WRITE_REGISTER_ULONG((PULONG)&reg->Enable, 0u);
        WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt,
                             TIMECARD_TIMESTAMP_IRQ_MASK);
    }
}

VOID
TimeCardTimestampPowerDown(PDEVICE_CONTEXT context)
{
    ULONG channel;

    if (!context->HardwareReady)
        return;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    for (channel = 0; channel < TIMECARD_TIMESTAMP_COUNT; ++channel) {
        volatile TIMECARD_TIMESTAMP_REG *reg = context->Timestamp[channel];

        if (reg == NULL)
            continue;
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask, 0u);
        WRITE_REGISTER_ULONG((PULONG)&reg->Enable, 0u);
        WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt,
                             TIMECARD_TIMESTAMP_IRQ_MASK);
        TimeCardTimestampQueueClear(context, channel, FALSE);
    }
    WdfWaitLockRelease(context->RegisterLock);
}

VOID
TimeCardTimestampPowerUp(PDEVICE_CONTEXT context)
{
    ULONG channel;

    if (!context->HardwareReady)
        return;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    for (channel = 0; channel < TIMECARD_TIMESTAMP_COUNT; ++channel) {
        volatile TIMECARD_TIMESTAMP_REG *reg = context->Timestamp[channel];
        ULONG version;
        ULONG polarity;

        if (reg == NULL ||
            (context->TimestampRequestedMask & (1u << channel)) == 0u ||
            (context->TimestampInterruptMask & (1u << channel)) == 0u) {
            continue;
        }
        if (context->BoardProfile == TIMECARD_BOARD_ART &&
            context->PhaseCaptureEnabled &&
            (channel == TIMECARD_TIMESTAMP_GNSS1 ||
             channel == TIMECARD_TIMESTAMP_PHC)) {
            continue;
        }
        version = READ_REGISTER_ULONG((PULONG)&reg->Version);
        if (!TimeCardTimestampVersionPresent(version) ||
            !TimeCardTimestampVersionAtLeast(version, 1u, 3u))
            continue;
        polarity = context->TimestampRequestedPolarity[channel] ==
            TIMECARD_TIMESTAMP_POLARITY_RISING ? 1u : 0u;
        WRITE_REGISTER_ULONG((PULONG)&reg->Polarity, polarity);
        if (TimeCardTimestampExtendedSurface(context)) {
            WRITE_REGISTER_ULONG(
                (PULONG)&reg->CableDelay,
                context->TimestampRequestedCableDelay[channel]);
        }
        TimeCardTimestampQueueClear(context, channel, FALSE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt,
                             TIMECARD_TIMESTAMP_IRQ_MASK);
        WRITE_REGISTER_ULONG((PULONG)&reg->Enable,
                             TIMECARD_TIMESTAMP_ENABLE_MASK);
        WRITE_REGISTER_ULONG((PULONG)&reg->InterruptMask,
                             TIMECARD_TIMESTAMP_IRQ_MASK);
    }
    WdfWaitLockRelease(context->RegisterLock);
}
