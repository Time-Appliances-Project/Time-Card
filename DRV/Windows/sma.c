/* SPDX-License-Identifier: BSD-3-Clause */
/* Validated SMA connector routing based on the Linux ptp_ocp driver. */

#include "timecard.h"

#define TIMECARD_SMA_ENABLE      0x8000u
#define TIMECARD_SMA_SELECT_MASK 0x7fffu

static volatile ULONG *
TimeCardSmaInputRegister(PDEVICE_CONTEXT context, ULONG connector)
{
    return connector > 2 ? &context->SmaMap2->Gpio1 :
                           &context->SmaMap1->Gpio1;
}

static volatile ULONG *
TimeCardSmaOutputRegister(PDEVICE_CONTEXT context, ULONG connector)
{
    return connector > 2 ? &context->SmaMap1->Gpio2 :
                           &context->SmaMap2->Gpio2;
}

static ULONG
TimeCardSmaReadHalf(volatile ULONG *reg, ULONG connector)
{
    ULONG shift = (connector & 1u) != 0 ? 0u : 16u;

    return (READ_REGISTER_ULONG((PULONG)reg) >> shift) & 0xffffu;
}

static VOID
TimeCardSmaWriteHalf(volatile ULONG *reg, ULONG connector, ULONG value)
{
    ULONG shift = (connector & 1u) != 0 ? 0u : 16u;
    ULONG preserveMask = shift == 0 ? 0xffff0000u : 0x0000ffffu;
    ULONG current = READ_REGISTER_ULONG((PULONG)reg);

    current = (current & preserveMask) | ((value & 0xffffu) << shift);
    WRITE_REGISTER_ULONG((PULONG)reg, current);
}

static BOOLEAN
TimeCardSmaFixedDirection(PDEVICE_CONTEXT context)
{
    return READ_REGISTER_ULONG((PULONG)&context->SmaMap2->Gpio2) ==
           MAXULONG;
}

static BOOLEAN
TimeCardSmaInputFunctionValid(ULONG function)
{
    switch (function) {
    case TIMECARD_SMA_INPUT_10MHZ:
    case TIMECARD_SMA_INPUT_PPS1:
    case TIMECARD_SMA_INPUT_PPS2:
    case TIMECARD_SMA_INPUT_TS1:
    case TIMECARD_SMA_INPUT_TS2:
    case TIMECARD_SMA_INPUT_IRIG:
    case TIMECARD_SMA_INPUT_DCF:
    case TIMECARD_SMA_INPUT_TS3:
    case TIMECARD_SMA_INPUT_TS4:
    case TIMECARD_SMA_INPUT_FREQ1:
    case TIMECARD_SMA_INPUT_FREQ2:
    case TIMECARD_SMA_INPUT_FREQ3:
    case TIMECARD_SMA_INPUT_FREQ4:
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOLEAN
TimeCardSmaOutputFunctionValid(ULONG function)
{
    switch (function) {
    case TIMECARD_SMA_OUTPUT_10MHZ:
    case TIMECARD_SMA_OUTPUT_PHC:
    case TIMECARD_SMA_OUTPUT_MAC:
    case TIMECARD_SMA_OUTPUT_GNSS1:
    case TIMECARD_SMA_OUTPUT_GNSS2:
    case TIMECARD_SMA_OUTPUT_IRIG:
    case TIMECARD_SMA_OUTPUT_DCF:
    case TIMECARD_SMA_OUTPUT_GEN1:
    case TIMECARD_SMA_OUTPUT_GEN2:
    case TIMECARD_SMA_OUTPUT_GEN3:
    case TIMECARD_SMA_OUTPUT_GEN4:
    case TIMECARD_SMA_OUTPUT_GND:
    case TIMECARD_SMA_OUTPUT_VCC:
        return TRUE;
    default:
        return FALSE;
    }
}

static NTSTATUS
TimeCardSmaQueryLocked(PDEVICE_CONTEXT context, ULONG connector,
                       TIMECARD_SMA_CONTROL *control)
{
    ULONG inputMap;
    ULONG outputMap;
    ULONG direction;
    BOOLEAN fixedDirection;
    BOOLEAN inputEnabled;
    BOOLEAN outputEnabled;

    inputMap = TimeCardSmaReadHalf(
        TimeCardSmaInputRegister(context, connector), connector);
    outputMap = TimeCardSmaReadHalf(
        TimeCardSmaOutputRegister(context, connector), connector);
    fixedDirection = TimeCardSmaFixedDirection(context);
    inputEnabled = (inputMap & TIMECARD_SMA_ENABLE) != 0;
    outputEnabled = (outputMap & TIMECARD_SMA_ENABLE) != 0;

    if (fixedDirection) {
        direction = connector <= 2 ? TIMECARD_SMA_DIRECTION_INPUT :
                                     TIMECARD_SMA_DIRECTION_OUTPUT;
    } else if (connector <= 2) {
        direction = inputEnabled ? TIMECARD_SMA_DIRECTION_INPUT :
                    outputEnabled ? TIMECARD_SMA_DIRECTION_OUTPUT :
                                    TIMECARD_SMA_DIRECTION_DISABLED;
    } else {
        direction = outputEnabled ? TIMECARD_SMA_DIRECTION_OUTPUT :
                    inputEnabled ? TIMECARD_SMA_DIRECTION_INPUT :
                                   TIMECARD_SMA_DIRECTION_DISABLED;
    }

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Connector = connector;
    control->Direction = direction;
    control->InputMap = inputMap;
    control->OutputMap = outputMap;
    control->Flags = TIMECARD_SMA_FLAG_PRESENT;
    if (fixedDirection)
        control->Flags |= TIMECARD_SMA_FLAG_FIXED_DIRECTION;
    if (direction == TIMECARD_SMA_DIRECTION_DISABLED)
        control->Flags |= TIMECARD_SMA_FLAG_DISABLED;
    if (direction == TIMECARD_SMA_DIRECTION_INPUT)
        control->Function = inputMap & TIMECARD_SMA_SELECT_MASK;
    else if (direction == TIMECARD_SMA_DIRECTION_OUTPUT)
        control->Function = outputMap & TIMECARD_SMA_SELECT_MASK;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardSmaQuery(PDEVICE_CONTEXT context, ULONG connector,
                 TIMECARD_SMA_CONTROL *control)
{
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (context->SmaMap1 == NULL || context->SmaMap2 == NULL)
        return STATUS_NOT_SUPPORTED;
    if (connector == 0 || connector > TIMECARD_SMA_COUNT)
        return STATUS_INVALID_PARAMETER;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardSmaQueryLocked(context, connector, control);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardSmaSet(PDEVICE_CONTEXT context,
               const TIMECARD_SMA_CONTROL *request,
               TIMECARD_SMA_CONTROL *response)
{
    volatile ULONG *inputRegister;
    volatile ULONG *outputRegister;
    BOOLEAN fixedDirection;
    ULONG fixedMode;
    ULONG previousDirection;
    ULONG value;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (context->SmaMap1 == NULL || context->SmaMap2 == NULL)
        return STATUS_NOT_SUPPORTED;
    if (request->Size < sizeof(*request) || request->Connector == 0 ||
        request->Connector > TIMECARD_SMA_COUNT ||
        request->Direction > TIMECARD_SMA_DIRECTION_DISABLED)
        return STATUS_INVALID_PARAMETER;
    if (request->Direction == TIMECARD_SMA_DIRECTION_INPUT &&
        !TimeCardSmaInputFunctionValid(request->Function))
        return STATUS_INVALID_PARAMETER;
    if (request->Direction == TIMECARD_SMA_DIRECTION_OUTPUT &&
        !TimeCardSmaOutputFunctionValid(request->Function))
        return STATUS_INVALID_PARAMETER;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardSmaQueryLocked(context, request->Connector, response);
    if (!NT_SUCCESS(status))
        goto done;
    previousDirection = response->Direction;
    fixedDirection = TimeCardSmaFixedDirection(context);
    fixedMode = request->Connector <= 2 ? TIMECARD_SMA_DIRECTION_INPUT :
                                          TIMECARD_SMA_DIRECTION_OUTPUT;
    if (fixedDirection && request->Direction != fixedMode) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }

    inputRegister = TimeCardSmaInputRegister(context, request->Connector);
    outputRegister = TimeCardSmaOutputRegister(context, request->Connector);

    if (request->Direction == TIMECARD_SMA_DIRECTION_DISABLED) {
        TimeCardSmaWriteHalf(inputRegister, request->Connector, 0);
        TimeCardSmaWriteHalf(outputRegister, request->Connector, 0);
    } else if (request->Direction == TIMECARD_SMA_DIRECTION_INPUT) {
        if (!fixedDirection &&
            previousDirection == TIMECARD_SMA_DIRECTION_OUTPUT) {
            TimeCardSmaWriteHalf(outputRegister, request->Connector, 0);
        }
        value = request->Function;
        if (!fixedDirection)
            value |= TIMECARD_SMA_ENABLE;
        TimeCardSmaWriteHalf(inputRegister, request->Connector, value);
    } else {
        if (!fixedDirection &&
            previousDirection == TIMECARD_SMA_DIRECTION_INPUT) {
            TimeCardSmaWriteHalf(inputRegister, request->Connector, 0);
        }
        value = request->Function;
        if (!fixedDirection)
            value |= TIMECARD_SMA_ENABLE;
        TimeCardSmaWriteHalf(outputRegister, request->Connector, value);
    }

    status = TimeCardSmaQueryLocked(context, request->Connector, response);
    if (NT_SUCCESS(status) &&
        (response->Direction != request->Direction ||
         (request->Direction != TIMECARD_SMA_DIRECTION_DISABLED &&
          response->Function != request->Function))) {
        status = STATUS_DEVICE_DATA_ERROR;
    }

done:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}
