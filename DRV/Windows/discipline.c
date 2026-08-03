/* SPDX-License-Identifier: BSD-3-Clause */
/* Capability discovery and safe timing primitives for user-mode discipline. */

#include "timecard.h"

#define TIMECARD_ART_REFERENCE_PPS_INDEX  0u
#define TIMECARD_ART_OSCILLATOR_PPS_INDEX 5u
#define TIMECARD_NANOSECONDS_PER_SECOND 1000000000ll
#define TIMECARD_MAX_PHASE_ADJUST       499999999ll

static BOOLEAN
TimeCardPhaseAvailable(PDEVICE_CONTEXT context)
{
    return context->HardwareReady &&
           context->BoardProfile == TIMECARD_BOARD_ART &&
           context->PhaseReference != NULL &&
           context->PhaseOscillator != NULL;
}

NTSTATUS
TimeCardGetCapabilities(PDEVICE_CONTEXT context,
                        TIMECARD_CAPABILITIES *capabilities)
{
    unsigned __int64 flags = 0;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(capabilities, sizeof(*capabilities));
    capabilities->Size = sizeof(*capabilities);
    capabilities->AbiVersion = TIMECARD_ABI_VERSION;
    capabilities->BoardProfile = context->BoardProfile;
    flags |= TIMECARD_CAP_PHC | TIMECARD_CAP_PHC_PHASE_ADJUST;

    if (context->Uart[TIMECARD_UART_GNSS] != NULL)
        flags |= TIMECARD_CAP_GNSS_UART;
    if (context->Uart[TIMECARD_UART_MAC] != NULL)
        flags |= TIMECARD_CAP_ATOMIC_UART;

    if (context->BoardProfile == TIMECARD_BOARD_ART &&
        context->Mro50 != NULL) {
        capabilities->OscillatorType = TIMECARD_OSCILLATOR_MRO50;
        capabilities->FineMinimum = TIMECARD_MRO50_FINE_MINIMUM;
        capabilities->FineMaximum = TIMECARD_MRO50_FINE_MAXIMUM;
        capabilities->CoarseMinimum = TIMECARD_MRO50_COARSE_MINIMUM;
        capabilities->CoarseMaximum = TIMECARD_MRO50_COARSE_MAXIMUM;
        flags |= TIMECARD_CAP_MRO50_DIRECT |
                 TIMECARD_CAP_TEMPERATURE_TELEMETRY;
        if (context->I2c != NULL)
            flags |= TIMECARD_CAP_DISCIPLINE_PARAMETERS;
    } else if (context->Uart[TIMECARD_UART_MAC] != NULL) {
        /* User mode identifies the UART oscillator protocol before writing. */
        capabilities->OscillatorType = TIMECARD_OSCILLATOR_UART;
        flags |= TIMECARD_CAP_HARDWARE_DISCIPLINE;
    } else {
        capabilities->OscillatorType = TIMECARD_OSCILLATOR_NONE;
    }

    if (TimeCardPhaseAvailable(context)) {
        capabilities->ReferencePpsIndex =
            TIMECARD_ART_REFERENCE_PPS_INDEX;
        capabilities->OscillatorPpsIndex =
            TIMECARD_ART_OSCILLATOR_PPS_INDEX;
        flags |= TIMECARD_CAP_PAIRED_PHASE_METER;
    } else {
        capabilities->ReferencePpsIndex = MAXULONG;
        capabilities->OscillatorPpsIndex = MAXULONG;
    }
    capabilities->Flags = flags;
    return STATUS_SUCCESS;
}

VOID
TimeCardRecordPhaseTimestamp(PDEVICE_CONTEXT context, ULONG channel,
                             ULONG error, ULONG nanoseconds, ULONG seconds)
{
    volatile LONG *sequence;
    PULONG counter;
    TIMECARD_TIME *time;
    PULONG capturedError;

    if (channel == TIMECARD_TIMESTAMP_GNSS1) {
        sequence = &context->PhaseReferenceSequence;
        counter = &context->PhaseReferenceCounter;
        time = &context->PhaseReferenceTime;
        capturedError = &context->PhaseReferenceError;
    } else if (channel == TIMECARD_TIMESTAMP_PHC) {
        sequence = &context->PhaseOscillatorSequence;
        counter = &context->PhaseOscillatorCounter;
        time = &context->PhaseOscillatorTime;
        capturedError = &context->PhaseOscillatorError;
    } else {
        return;
    }

    /* A sequence lock keeps the passive query coherent with the DIRQL ISR. */
    InterlockedIncrement(sequence);
    *capturedError = error;
    time->Nanoseconds = nanoseconds;
    time->Seconds = seconds;
    time->Reserved = 0u;
    ++*counter;
    KeMemoryBarrier();
    InterlockedIncrement(sequence);
}

static VOID
TimeCardPollPhaseTimestampLocked(PDEVICE_CONTEXT context, ULONG channel,
                                 volatile TIMECARD_TIMESTAMP_REG *reg)
{
    if (reg == NULL ||
        (READ_REGISTER_ULONG((PULONG)&reg->Interrupt) & 1u) == 0u) {
        return;
    }

    /*
     * Some ART bitstreams expose the PPS timestamp cores but do not route
     * their MSI-X messages.  Preserve the interrupt path when it exists and
     * consume a pending latch here as a compatibility fallback when it does
     * not.  The register lock serializes this acknowledgement with control
     * changes; the sequence lock serializes the sample with a possible ISR.
     */
    TimeCardRecordPhaseTimestamp(
        context, channel,
        READ_REGISTER_ULONG((PULONG)&reg->Error),
        READ_REGISTER_ULONG((PULONG)&reg->TimeNanoseconds),
        READ_REGISTER_ULONG((PULONG)&reg->TimeSeconds));
    WRITE_REGISTER_ULONG((PULONG)&reg->Interrupt, 1u);
}

static BOOLEAN
TimeCardReadCapturedPhase(PDEVICE_CONTEXT context, BOOLEAN reference,
                          PULONG counter, TIMECARD_TIME *time, PULONG error)
{
    volatile LONG *sequence = reference ?
        &context->PhaseReferenceSequence :
        &context->PhaseOscillatorSequence;
    ULONG before;
    ULONG after;
    ULONG attempt;

    for (attempt = 0; attempt < 4u; ++attempt) {
        before = (ULONG)InterlockedCompareExchange(sequence, 0, 0);
        if ((before & 1u) != 0u)
            continue;
        KeMemoryBarrier();
        if (reference) {
            *counter = context->PhaseReferenceCounter;
            *time = context->PhaseReferenceTime;
            *error = context->PhaseReferenceError;
        } else {
            *counter = context->PhaseOscillatorCounter;
            *time = context->PhaseOscillatorTime;
            *error = context->PhaseOscillatorError;
        }
        KeMemoryBarrier();
        after = (ULONG)InterlockedCompareExchange(sequence, 0, 0);
        if (before == after && (after & 1u) == 0u) {
            return time->Nanoseconds < 1000000000u && *counter != 0u;
        }
    }
    RtlZeroMemory(time, sizeof(*time));
    *counter = 0u;
    *error = 0u;
    return FALSE;
}

NTSTATUS
TimeCardPhaseQuery(PDEVICE_CONTEXT context, TIMECARD_PHASE_SAMPLE *sample)
{
    BOOLEAN referenceValid;
    BOOLEAN oscillatorValid;
    ULONG referenceCounter = 0;
    ULONG oscillatorCounter = 0;
    ULONG referenceError = 0;
    ULONG oscillatorError = 0;
    unsigned __int64 referenceNanoseconds;
    unsigned __int64 oscillatorNanoseconds;
    signed __int64 phase;

    if (!TimeCardPhaseAvailable(context))
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(sample, sizeof(*sample));
    sample->Size = sizeof(*sample);
    sample->Flags = TIMECARD_PHASE_FLAG_PRESENT;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    if ((READ_REGISTER_ULONG((PULONG)&context->PhaseReference->Enable) &
         1u) != 0 &&
        (READ_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Enable) &
         1u) != 0) {
        sample->Flags |= TIMECARD_PHASE_FLAG_ENABLED;
    }
    TimeCardPollPhaseTimestampLocked(
        context, TIMECARD_TIMESTAMP_GNSS1, context->PhaseReference);
    TimeCardPollPhaseTimestampLocked(
        context, TIMECARD_TIMESTAMP_PHC, context->PhaseOscillator);
    referenceValid = TimeCardReadCapturedPhase(
        context, TRUE, &referenceCounter,
        &sample->ReferenceTime, &referenceError);
    oscillatorValid = TimeCardReadCapturedPhase(
        context, FALSE, &oscillatorCounter,
        &sample->OscillatorTime, &oscillatorError);
    WdfWaitLockRelease(context->RegisterLock);

    sample->ReferenceCounter = referenceCounter;
    sample->OscillatorCounter = oscillatorCounter;
    sample->ReferenceError = referenceError;
    sample->OscillatorError = oscillatorError;

    if (referenceValid)
        sample->Flags |= TIMECARD_PHASE_FLAG_REFERENCE_VALID;
    if (oscillatorValid)
        sample->Flags |= TIMECARD_PHASE_FLAG_OSCILLATOR_VALID;
    if (!referenceValid || !oscillatorValid)
        return STATUS_SUCCESS;

    referenceNanoseconds =
        sample->ReferenceTime.Seconds * 1000000000ull +
        sample->ReferenceTime.Nanoseconds;
    oscillatorNanoseconds =
        sample->OscillatorTime.Seconds * 1000000000ull +
        sample->OscillatorTime.Nanoseconds;
    if (oscillatorNanoseconds >= referenceNanoseconds) {
        phase = (signed __int64)(oscillatorNanoseconds -
                                 referenceNanoseconds);
    } else {
        phase = -(signed __int64)(referenceNanoseconds -
                                  oscillatorNanoseconds);
    }
    /* Constant-time normalization also avoids a +/-500 ms tie oscillating. */
    phase %= TIMECARD_NANOSECONDS_PER_SECOND;
    if (phase > TIMECARD_MAX_PHASE_ADJUST)
        phase -= TIMECARD_NANOSECONDS_PER_SECOND;
    else if (phase < -TIMECARD_MAX_PHASE_ADJUST)
        phase += TIMECARD_NANOSECONDS_PER_SECOND;
    if (phase > TIMECARD_MAX_PHASE_ADJUST)
        phase = TIMECARD_MAX_PHASE_ADJUST;
    else if (phase < -TIMECARD_MAX_PHASE_ADJUST)
        phase = -TIMECARD_MAX_PHASE_ADJUST;
    sample->PhaseNanoseconds = phase;
    sample->Flags |= TIMECARD_PHASE_FLAG_PHASE_VALID;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardPhaseControl(PDEVICE_CONTEXT context,
                     const TIMECARD_PHASE_CONTROL *request,
                     TIMECARD_PHASE_CONTROL *response)
{
    ULONG enabled;

    if (!TimeCardPhaseAvailable(context))
        return STATUS_NOT_SUPPORTED;
    if (request->Size < sizeof(*request) ||
        request->Action > TIMECARD_PHASE_CONTROL_ENABLE ||
        request->ReferencePolarity > 1u ||
        request->OscillatorPolarity > 1u ||
        request->Reserved[0] != 0u || request->Reserved[1] != 0u ||
        request->Reserved[2] != 0u) {
        return STATUS_INVALID_PARAMETER;
    }

    enabled = request->Action == TIMECARD_PHASE_CONTROL_ENABLE ? 1u : 0u;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    if (enabled != 0u) {
        if ((context->TimestampRequestedMask &
             ((1u << TIMECARD_TIMESTAMP_GNSS1) |
              (1u << TIMECARD_TIMESTAMP_PHC))) != 0u) {
            WdfWaitLockRelease(context->RegisterLock);
            return STATUS_DEVICE_BUSY;
        }
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->InterruptMask,
                             0u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->InterruptMask,
                             0u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->Enable, 0u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Enable, 0u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->Interrupt, 1u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Interrupt, 1u);
        InterlockedExchange(&context->PhaseReferenceSequence, 0);
        InterlockedExchange(&context->PhaseOscillatorSequence, 0);
        context->PhaseReferenceCounter = 0u;
        context->PhaseOscillatorCounter = 0u;
        RtlZeroMemory(&context->PhaseReferenceTime,
                      sizeof(context->PhaseReferenceTime));
        RtlZeroMemory(&context->PhaseOscillatorTime,
                      sizeof(context->PhaseOscillatorTime));
        context->PhaseReferenceError = 0u;
        context->PhaseOscillatorError = 0u;
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->Polarity,
                             request->ReferencePolarity);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Polarity,
                             request->OscillatorPolarity);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->Interrupt, 1u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Interrupt, 1u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->Enable, 1u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Enable, 1u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->InterruptMask,
                             1u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->InterruptMask,
                             1u);
        context->PhaseCaptureEnabled = TRUE;
        context->PhaseReferencePolarity =
            (UCHAR)request->ReferencePolarity;
        context->PhaseOscillatorPolarity =
            (UCHAR)request->OscillatorPolarity;
    } else {
        TimeCardPhaseDisableLocked(context);
    }
    WdfWaitLockRelease(context->RegisterLock);

    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->Action = request->Action;
    response->ReferencePolarity = request->ReferencePolarity;
    response->OscillatorPolarity = request->OscillatorPolarity;
    response->Enabled = enabled;
    return STATUS_SUCCESS;
}

VOID
TimeCardPhaseDisableLocked(PDEVICE_CONTEXT context)
{
    /*
     * This helper deliberately does not acquire RegisterLock.  Lease release
     * and file cleanup must disarm capture and clear ownership as one atomic
     * transition so a replacement owner cannot be disabled by stale cleanup.
     */
    if (TimeCardPhaseAvailable(context)) {
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->InterruptMask,
                             0u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->InterruptMask,
                             0u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->Enable, 0u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Enable, 0u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->Interrupt, 1u);
        WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Interrupt, 1u);
    }
    context->PhaseCaptureEnabled = FALSE;
}

VOID
TimeCardPhaseSuspend(PDEVICE_CONTEXT context)
{
    if (!TimeCardPhaseAvailable(context))
        return;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->InterruptMask, 0u);
    WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->InterruptMask, 0u);
    WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->Enable, 0u);
    WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Enable, 0u);
    WRITE_REGISTER_ULONG((PULONG)&context->PhaseReference->Interrupt, 1u);
    WRITE_REGISTER_ULONG((PULONG)&context->PhaseOscillator->Interrupt, 1u);
    WdfWaitLockRelease(context->RegisterLock);
}

NTSTATUS
TimeCardAdjustPhc(PDEVICE_CONTEXT context,
                  const TIMECARD_PHC_ADJUST *request,
                  TIMECARD_PHC_ADJUST *response)
{
    TIMECARD_TIME current;
    signed __int64 seconds;
    signed __int64 nanoseconds;
    NTSTATUS status;

    if (request->Size < sizeof(*request) || request->Flags != 0u ||
        request->Reserved[0] != 0u || request->Reserved[1] != 0u ||
        request->Reserved[2] != 0u || request->Reserved[3] != 0u ||
        request->OffsetNanoseconds > TIMECARD_MAX_PHASE_ADJUST ||
        request->OffsetNanoseconds < -TIMECARD_MAX_PHASE_ADJUST) {
        return STATUS_INVALID_PARAMETER;
    }

    status = TimeCardGetTime(context, &current);
    if (!NT_SUCCESS(status))
        return status;
    seconds = (signed __int64)current.Seconds;
    nanoseconds = (signed __int64)current.Nanoseconds +
                  request->OffsetNanoseconds;
    while (nanoseconds >= TIMECARD_NANOSECONDS_PER_SECOND) {
        nanoseconds -= TIMECARD_NANOSECONDS_PER_SECOND;
        ++seconds;
    }
    while (nanoseconds < 0) {
        nanoseconds += TIMECARD_NANOSECONDS_PER_SECOND;
        --seconds;
    }
    if (seconds < 0 || seconds > MAXULONG)
        return STATUS_INTEGER_OVERFLOW;

    RtlZeroMemory(response, sizeof(*response));
    response->Size = sizeof(*response);
    response->OffsetNanoseconds = request->OffsetNanoseconds;
    response->ResultingTime.Seconds = (unsigned __int64)seconds;
    response->ResultingTime.Nanoseconds = (ULONG)nanoseconds;
    status = TimeCardSetTime(context, &response->ResultingTime);
    return status;
}
