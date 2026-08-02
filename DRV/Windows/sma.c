/* SPDX-License-Identifier: BSD-3-Clause */
/* Validated SMA connector routing based on the Linux ptp_ocp driver. */

#include "timecard.h"

#define TIMECARD_SMA_ENABLE      0x8000u
#define TIMECARD_SMA_SELECT_MASK 0x7fffu

static ULONG
TimeCardArtSmaDefaultFunction(ULONG connector)
{
    static const ULONG defaults[TIMECARD_SMA_COUNT] = {
        TIMECARD_SMA_INPUT_TS2,
        TIMECARD_SMA_INPUT_PPS1,
        TIMECARD_SMA_OUTPUT_IRIG,
        TIMECARD_SMA_OUTPUT_MAC
    };

    return defaults[connector - 1u];
}

static ULONG
TimeCardArtSmaFunctionDirection(ULONG function)
{
    return function == TIMECARD_SMA_INPUT_PPS1 ||
           function == TIMECARD_SMA_INPUT_TS2 ?
           TIMECARD_SMA_DIRECTION_INPUT :
           TIMECARD_SMA_DIRECTION_OUTPUT;
}

static BOOLEAN
TimeCardArtSmaFunctionValid(ULONG direction, ULONG function)
{
    if (direction == TIMECARD_SMA_DIRECTION_INPUT) {
        return function == TIMECARD_SMA_INPUT_PPS1 ||
               function == TIMECARD_SMA_INPUT_TS2;
    }
    if (direction == TIMECARD_SMA_DIRECTION_OUTPUT) {
        return function == TIMECARD_SMA_OUTPUT_MAC ||
               function == TIMECARD_SMA_OUTPUT_GNSS1 ||
               function == TIMECARD_SMA_OUTPUT_IRIG;
    }
    return FALSE;
}

static NTSTATUS
TimeCardArtSmaQueryLocked(PDEVICE_CONTEXT context, ULONG connector,
                          TIMECARD_SMA_CONTROL *control)
{
    volatile ULONG *gpio = &context->ArtSma->Map[connector - 1u].Gpio;
    ULONG raw = READ_REGISTER_ULONG((PULONG)gpio);
    ULONG function = raw & 0xffu;
    BOOLEAN fixed = function == 0;
    ULONG direction;

    if (fixed)
        function = TimeCardArtSmaDefaultFunction(connector);
    direction = TimeCardArtSmaFunctionDirection(function);

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Connector = connector;
    control->Direction = direction;
    control->Function = function;
    control->Flags = TIMECARD_SMA_FLAG_PRESENT;
    if (fixed)
        control->Flags |= TIMECARD_SMA_FLAG_FIXED_DIRECTION;
    if (direction == TIMECARD_SMA_DIRECTION_INPUT)
        control->InputMap = function;
    else
        control->OutputMap = function;
    control->Reserved = raw;
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardArtSmaSetLocked(PDEVICE_CONTEXT context,
                        const TIMECARD_SMA_CONTROL *request,
                        TIMECARD_SMA_CONTROL *response)
{
    volatile ULONG *gpio =
        &context->ArtSma->Map[request->Connector - 1u].Gpio;
    ULONG raw = READ_REGISTER_ULONG((PULONG)gpio);
    ULONG supported = raw >> 16;

    if ((raw & 0xffu) == 0)
        return STATUS_NOT_SUPPORTED;
    if (request->Direction == TIMECARD_SMA_DIRECTION_DISABLED ||
        !TimeCardArtSmaFunctionValid(request->Direction,
                                     request->Function)) {
        return STATUS_INVALID_PARAMETER;
    }
    if ((supported & request->Function) == 0)
        return STATUS_NOT_SUPPORTED;

    /*
     * The ART gateware encodes direction in the selected function. Its
     * writable field is the low byte, while bits 31:16 report the choices
     * implemented by the connector.
     */
    raw = (raw & 0xff00u) | (request->Function & 0xffu);
    WRITE_REGISTER_ULONG((PULONG)gpio, raw);
    TimeCardArtSmaQueryLocked(context, request->Connector, response);
    if (response->Direction != request->Direction ||
        response->Function != request->Function) {
        return STATUS_DEVICE_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}

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

static BOOLEAN
TimeCardSetRoutedCoreLocked(volatile ULONG *controlRegister,
                            volatile ULONG *versionRegister,
                            BOOLEAN enable)
{
    ULONG control;
    ULONG version;

    if (controlRegister == NULL || versionRegister == NULL)
        return FALSE;
    version = READ_REGISTER_ULONG((PULONG)versionRegister);
    if (version == 0u || version == MAXULONG)
        return FALSE;
    control = READ_REGISTER_ULONG((PULONG)controlRegister);
    if (control == MAXULONG)
        return FALSE;
    if (enable)
        control |= 1u;
    else
        control &= ~1u;
    WRITE_REGISTER_ULONG((PULONG)controlRegister, control);
    return TRUE;
}

VOID
TimeCardRefreshRoutedCoresLocked(PDEVICE_CONTEXT context,
                                 ULONG previousDirection,
                                 ULONG previousFunction,
                                 ULONG currentDirection,
                                 ULONG currentFunction)
{
    BOOLEAN updateIrigInput = FALSE;
    BOOLEAN updateIrigOutput = FALSE;
    BOOLEAN updateDcfInput = FALSE;
    BOOLEAN updateDcfOutput = FALSE;
    BOOLEAN irigInput = FALSE;
    BOOLEAN irigOutput = FALSE;
    BOOLEAN dcfInput = FALSE;
    BOOLEAN dcfOutput = FALSE;
    ULONG connector;

    if (context->BoardProfile == TIMECARD_BOARD_ART ||
        context->SmaMap1 == NULL || context->SmaMap2 == NULL)
        return;

    updateIrigInput =
        (previousDirection == TIMECARD_SMA_DIRECTION_INPUT &&
         previousFunction == TIMECARD_SMA_INPUT_IRIG) ||
        (currentDirection == TIMECARD_SMA_DIRECTION_INPUT &&
         currentFunction == TIMECARD_SMA_INPUT_IRIG);
    updateIrigOutput =
        (previousDirection == TIMECARD_SMA_DIRECTION_OUTPUT &&
         previousFunction == TIMECARD_SMA_OUTPUT_IRIG) ||
        (currentDirection == TIMECARD_SMA_DIRECTION_OUTPUT &&
         currentFunction == TIMECARD_SMA_OUTPUT_IRIG);
    updateDcfInput =
        (previousDirection == TIMECARD_SMA_DIRECTION_INPUT &&
         previousFunction == TIMECARD_SMA_INPUT_DCF) ||
        (currentDirection == TIMECARD_SMA_DIRECTION_INPUT &&
         currentFunction == TIMECARD_SMA_INPUT_DCF);
    updateDcfOutput =
        (previousDirection == TIMECARD_SMA_DIRECTION_OUTPUT &&
         previousFunction == TIMECARD_SMA_OUTPUT_DCF) ||
        (currentDirection == TIMECARD_SMA_DIRECTION_OUTPUT &&
         currentFunction == TIMECARD_SMA_OUTPUT_DCF);
    if (!updateIrigInput && !updateIrigOutput &&
        !updateDcfInput && !updateDcfOutput)
        return;

    for (connector = 1; connector <= TIMECARD_SMA_COUNT; ++connector) {
        TIMECARD_SMA_CONTROL route;

        if (!NT_SUCCESS(TimeCardSmaQueryLocked(
                context, connector, &route)))
            continue;
        if (route.Direction == TIMECARD_SMA_DIRECTION_INPUT) {
            if (route.Function == TIMECARD_SMA_INPUT_IRIG)
                irigInput = TRUE;
            else if (route.Function == TIMECARD_SMA_INPUT_DCF)
                dcfInput = TRUE;
        } else if (route.Direction == TIMECARD_SMA_DIRECTION_OUTPUT) {
            if (route.Function == TIMECARD_SMA_OUTPUT_IRIG)
                irigOutput = TRUE;
            else if (route.Function == TIMECARD_SMA_OUTPUT_DCF)
                dcfOutput = TRUE;
        }
    }

    /* Aggregate all four routes before changing a shared core enable bit. */
    if (updateIrigInput && context->IrigSlave != NULL) {
        if (irigInput) {
            context->IrigSlaveRouteManaged =
                TimeCardSetRoutedCoreLocked(&context->IrigSlave->Control,
                                            &context->IrigSlave->Version,
                                            TRUE);
        } else if (context->IrigSlaveRouteManaged) {
            TimeCardSetRoutedCoreLocked(&context->IrigSlave->Control,
                                        &context->IrigSlave->Version, FALSE);
            context->IrigSlaveRouteManaged = FALSE;
        }
    }
    if (updateIrigOutput && context->IrigMaster != NULL) {
        if (irigOutput) {
            context->IrigMasterRouteManaged =
                TimeCardSetRoutedCoreLocked(&context->IrigMaster->Control,
                                            &context->IrigMaster->Version,
                                            TRUE);
        } else if (context->IrigMasterRouteManaged) {
            TimeCardSetRoutedCoreLocked(&context->IrigMaster->Control,
                                        &context->IrigMaster->Version, FALSE);
            context->IrigMasterRouteManaged = FALSE;
        }
    }
    if (updateDcfInput && context->DcfSlave != NULL) {
        if (dcfInput) {
            context->DcfSlaveRouteManaged =
                TimeCardSetRoutedCoreLocked(&context->DcfSlave->Control,
                                            &context->DcfSlave->Version,
                                            TRUE);
        } else if (context->DcfSlaveRouteManaged) {
            TimeCardSetRoutedCoreLocked(&context->DcfSlave->Control,
                                        &context->DcfSlave->Version, FALSE);
            context->DcfSlaveRouteManaged = FALSE;
        }
    }
    if (updateDcfOutput && context->DcfMaster != NULL) {
        if (dcfOutput) {
            context->DcfMasterRouteManaged =
                TimeCardSetRoutedCoreLocked(&context->DcfMaster->Control,
                                            &context->DcfMaster->Version,
                                            TRUE);
        } else if (context->DcfMasterRouteManaged) {
            TimeCardSetRoutedCoreLocked(&context->DcfMaster->Control,
                                        &context->DcfMaster->Version, FALSE);
            context->DcfMasterRouteManaged = FALSE;
        }
    }
}

NTSTATUS
TimeCardSmaQuery(PDEVICE_CONTEXT context, ULONG connector,
                 TIMECARD_SMA_CONTROL *control)
{
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (connector == 0 || connector > TIMECARD_SMA_COUNT)
        return STATUS_INVALID_PARAMETER;
    if (context->BoardProfile == TIMECARD_BOARD_ART) {
        if (context->ArtSma == NULL)
            return STATUS_NOT_SUPPORTED;
        WdfWaitLockAcquire(context->RegisterLock, NULL);
        status = TimeCardArtSmaQueryLocked(context, connector, control);
        WdfWaitLockRelease(context->RegisterLock);
        return status;
    }
    if (context->SmaMap1 == NULL || context->SmaMap2 == NULL)
        return STATUS_NOT_SUPPORTED;

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
    ULONG previousFunction;
    ULONG value;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) || request->Connector == 0 ||
        request->Connector > TIMECARD_SMA_COUNT ||
        request->Direction > TIMECARD_SMA_DIRECTION_DISABLED)
        return STATUS_INVALID_PARAMETER;
    if (context->BoardProfile == TIMECARD_BOARD_ART) {
        if (context->ArtSma == NULL)
            return STATUS_NOT_SUPPORTED;
        WdfWaitLockAcquire(context->RegisterLock, NULL);
        status = TimeCardArtSmaSetLocked(context, request, response);
        WdfWaitLockRelease(context->RegisterLock);
        return status;
    }
    if (context->SmaMap1 == NULL || context->SmaMap2 == NULL)
        return STATUS_NOT_SUPPORTED;
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
    previousFunction = response->Function;
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
    if (NT_SUCCESS(status)) {
        TimeCardRefreshRoutedCoresLocked(
            context, previousDirection, previousFunction,
            response->Direction, response->Function);
    }

done:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}
