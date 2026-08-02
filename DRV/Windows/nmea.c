/* SPDX-License-Identifier: BSD-3-Clause */
/* FPGA NMEA sentence-generator configuration, matching Linux ptp_ocp. */

#include "timecard.h"

#define TIMECARD_NMEA_ENABLE 0x00000001u
#define TIMECARD_NMEA_STATUS_ERROR 0x00000001u
#define TIMECARD_NMEA_BAUD_MASK 0x0000000fu
#define TIMECARD_NMEA_CONTROL_GNSS_SHIFT 24u
#define TIMECARD_NMEA_CONTROL_GNSS_MASK \
    (0x0fu << TIMECARD_NMEA_CONTROL_GNSS_SHIFT)
#define TIMECARD_NMEA_CONTROL_MESSAGE_SHIFT 16u
#define TIMECARD_NMEA_CONTROL_MESSAGE_MASK \
    (0x07u << TIMECARD_NMEA_CONTROL_MESSAGE_SHIFT)
#define TIMECARD_NMEA_CORRECTION_SIGN 0x80000000u
#define TIMECARD_NMEA_CORRECTION_MAGNITUDE 0x7fffffffu
#define TIMECARD_NMEA_LOCAL_SIGN 0x80000000u
#define TIMECARD_NMEA_LOCAL_HOUR_SHIFT 16u
#define TIMECARD_NMEA_LOCAL_HOUR_MASK \
    (0x0fu << TIMECARD_NMEA_LOCAL_HOUR_SHIFT)
#define TIMECARD_NMEA_LOCAL_MINUTE_MASK 0x3fu
#define TIMECARD_NMEA_LOCAL_MAXIMUM_MINUTES ((13 * 60) + 59)
#define TIMECARD_NMEA_UTC_READ_REQUEST (1u << 30)
#define TIMECARD_NMEA_UTC_READ_DONE    (1u << 31)
#define TIMECARD_NMEA_UTC_WRITE_REQUEST (1u << 0)
#define TIMECARD_NMEA_UTC_LEAP61       (1u << 11)
#define TIMECARD_NMEA_UTC_LEAP59       (1u << 12)
#define TIMECARD_NMEA_UTC_OFFSET_VALID (1u << 13)
#define TIMECARD_NMEA_UTC_OFFSET_SHIFT 16u

C_ASSERT(sizeof(TIMECARD_NMEA_UTC_CONTROL) == 64);

static const ULONG TimeCardNmeaBaudRates[] = {
    1200u, 2400u, 4800u, 9600u, 19200u, 38400u, 57600u,
    115200u, 230400u, 460800u, 921600u, 1000000u, 2000000u
};

static BOOLEAN
TimeCardNmeaVersionAtLeast(ULONG version, ULONG major, ULONG minor)
{
    ULONG actualMajor;
    ULONG actualMinor;

    if (version == 0u || version == MAXULONG)
        return FALSE;
    actualMajor = version >> 24;
    actualMinor = (version >> 16) & 0xffu;
    return actualMajor > major ||
           (actualMajor == major && actualMinor >= minor);
}

static NTSTATUS
TimeCardNmeaSelectorFromBaud(ULONG baud, PULONG selector)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(TimeCardNmeaBaudRates); ++i) {
        if (TimeCardNmeaBaudRates[i] == baud) {
            *selector = i;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INVALID_PARAMETER;
}

static BOOLEAN
TimeCardNmeaGnssValid(ULONG gnss)
{
    return gnss <= TIMECARD_NMEA_GNSS_BEIDOU ||
           gnss == TIMECARD_NMEA_GNSS_PROPRIETARY;
}

static ULONG
TimeCardNmeaSupportedMessageMask(ULONG version)
{
    ULONG mask = TIMECARD_NMEA_DISABLE_ZDA;

    if (TimeCardNmeaVersionAtLeast(version, 1u, 4u))
        mask |= TIMECARD_NMEA_DISABLE_RMC;
    if (TimeCardNmeaVersionAtLeast(version, 1u, 6u))
        mask |= TIMECARD_NMEA_DISABLE_UTC;
    return mask;
}

static NTSTATUS
TimeCardNmeaEncodeSignedMagnitude(LONG value, PULONG encoded)
{
    ULONG magnitude;

    /* -2^31 cannot be represented by a 31-bit sign-magnitude value. */
    if ((ULONG)value == 0x80000000u)
        return STATUS_INVALID_PARAMETER;
    magnitude = value < 0 ? (ULONG)(-value) : (ULONG)value;
    *encoded = magnitude |
        (value < 0 ? TIMECARD_NMEA_CORRECTION_SIGN : 0u);
    return STATUS_SUCCESS;
}
static LONG
TimeCardNmeaDecodeSignedMagnitude(ULONG value)
{
    LONG magnitude =
        (LONG)(value & TIMECARD_NMEA_CORRECTION_MAGNITUDE);

    return (value & TIMECARD_NMEA_CORRECTION_SIGN) != 0u ?
        -magnitude : magnitude;
}

static NTSTATUS
TimeCardNmeaEncodeLocalOffset(LONG minutes, PULONG encoded)
{
    ULONG magnitude;
    ULONG hours;

    if (minutes < -TIMECARD_NMEA_LOCAL_MAXIMUM_MINUTES ||
        minutes > TIMECARD_NMEA_LOCAL_MAXIMUM_MINUTES)
        return STATUS_INVALID_PARAMETER;
    magnitude = minutes < 0 ? (ULONG)(-minutes) : (ULONG)minutes;
    hours = magnitude / 60u;
    *encoded = (hours << TIMECARD_NMEA_LOCAL_HOUR_SHIFT) |
               (magnitude % 60u) |
               (minutes < 0 ? TIMECARD_NMEA_LOCAL_SIGN : 0u);
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardNmeaDecodeLocalOffset(ULONG value, PLONG minutes)
{
    ULONG hours = (value & TIMECARD_NMEA_LOCAL_HOUR_MASK) >>
                  TIMECARD_NMEA_LOCAL_HOUR_SHIFT;
    ULONG remainder = value & TIMECARD_NMEA_LOCAL_MINUTE_MASK;
    LONG result;

    if (hours > 13u || remainder > 59u)
        return STATUS_DEVICE_DATA_ERROR;
    result = (LONG)(hours * 60u + remainder);
    *minutes = (value & TIMECARD_NMEA_LOCAL_SIGN) != 0u ?
        -result : result;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardNmeaQuery(PDEVICE_CONTEXT context, TIMECARD_NMEA_CONTROL *control)
{
    ULONG correction = 0;
    LONG decodedLocalOffset;
    ULONG localOffset = 0;
    ULONG selector = 0;
    ULONG version;
    NTSTATUS status = STATUS_SUCCESS;

    if (!context->HardwareReady || context->NmeaOut == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Flags = TIMECARD_NMEA_FLAG_PRESENT |
                     TIMECARD_NMEA_FLAG_ADVANCED_VALID;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    version = READ_REGISTER_ULONG((PULONG)&context->NmeaOut->Version);
    if (!TimeCardNmeaVersionAtLeast(version, 1u, 0u)) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    control->Version = version;
    control->Control = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->Ctrl);
    correction = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->Correction);
    localOffset = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->LocalOffset);
    if (TimeCardNmeaVersionAtLeast(version, 1u, 1u)) {
        control->Status = READ_REGISTER_ULONG(
            (PULONG)&context->NmeaOut->Status);
    }
    if (TimeCardNmeaVersionAtLeast(version, 1u, 2u)) {
        /* Gateware: zero=inverted, one=normal. Public ABI: one=inverted. */
        control->Polarity =
            (READ_REGISTER_ULONG(
                (PULONG)&context->NmeaOut->UartPolarity) & 1u) == 0u ?
                    1u : 0u;
    }
    selector = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->UartBaud) & TIMECARD_NMEA_BAUD_MASK;
done:
    WdfWaitLockRelease(context->RegisterLock);
    if (!NT_SUCCESS(status))
        return status;

    control->BaudSelector = selector;
    control->CorrectionSeconds =
        TimeCardNmeaDecodeSignedMagnitude(correction);
    status = TimeCardNmeaDecodeLocalOffset(localOffset,
                                            &decodedLocalOffset);
    if (!NT_SUCCESS(status))
        return status;
    control->LocalOffsetMinutes = decodedLocalOffset;
    if (TimeCardNmeaVersionAtLeast(version, 1u, 3u)) {
        control->Gnss =
            (control->Control & TIMECARD_NMEA_CONTROL_GNSS_MASK) >>
            TIMECARD_NMEA_CONTROL_GNSS_SHIFT;
    }
    control->MessageDisableMask =
        ((control->Control & TIMECARD_NMEA_CONTROL_MESSAGE_MASK) >>
         TIMECARD_NMEA_CONTROL_MESSAGE_SHIFT) &
        TimeCardNmeaSupportedMessageMask(version);
    if (selector < ARRAYSIZE(TimeCardNmeaBaudRates))
        control->Baud = TimeCardNmeaBaudRates[selector];
    if ((control->Control & TIMECARD_NMEA_ENABLE) != 0)
        control->Flags |= TIMECARD_NMEA_FLAG_ENABLED;
    if ((control->Status & TIMECARD_NMEA_STATUS_ERROR) != 0u)
        control->Flags |= TIMECARD_NMEA_FLAG_ERROR;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardNmeaSet(PDEVICE_CONTEXT context,
                const TIMECARD_NMEA_CONTROL *request,
                TIMECARD_NMEA_CONTROL *response)
{
    TIMECARD_NMEA_CONTROL current;
    TIMECARD_UART_CONFIG uartConfig;
    TIMECARD_UART_HARDWARE_STATE uartState;
    BOOLEAN configured = FALSE;
    BOOLEAN uartStateCaptured = FALSE;
    ULONG selector;
    ULONG control;
    ULONG correction = 0;
    ULONG localOffset = 0;
    ULONG supportedControlMessageMask;
    ULONG supportedMessageMask;
    ULONG value;
    ULONG version;
    ULONG oldBaud = 0u;
    ULONG oldControl = 0u;
    ULONG oldCorrection = 0u;
    ULONG oldLocalOffset = 0u;
    ULONG oldPolarity = 0u;
    BOOLEAN enabled;
    NTSTATUS status;

    if (!context->HardwareReady || context->NmeaOut == NULL)
        return STATUS_NOT_SUPPORTED;
    if (request->Size < sizeof(*request) || request->Polarity > 1u ||
        (request->Flags & ~(TIMECARD_NMEA_FLAG_PRESENT |
                            TIMECARD_NMEA_FLAG_ENABLED |
                            TIMECARD_NMEA_FLAG_ADVANCED_VALID |
                            TIMECARD_NMEA_FLAG_ERROR |
                            TIMECARD_NMEA_FLAG_CLEAR_ERROR)) != 0)
        return STATUS_INVALID_PARAMETER;

    status = TimeCardNmeaSelectorFromBaud(request->Baud, &selector);
    if (!NT_SUCCESS(status))
        return status;

    /* Confirm the optional block is implemented before touching its MMIO. */
    status = TimeCardNmeaQuery(context, &current);
    if (!NT_SUCCESS(status))
        return status;
    if (request->Polarity != current.Polarity &&
        !TimeCardNmeaVersionAtLeast(current.Version, 1u, 2u))
        return STATUS_NOT_SUPPORTED;
    if ((request->Flags & TIMECARD_NMEA_FLAG_CLEAR_ERROR) != 0u &&
        !TimeCardNmeaVersionAtLeast(current.Version, 1u, 1u))
        return STATUS_NOT_SUPPORTED;
    supportedMessageMask = TimeCardNmeaSupportedMessageMask(current.Version);
    supportedControlMessageMask =
        supportedMessageMask << TIMECARD_NMEA_CONTROL_MESSAGE_SHIFT;
    if ((request->Flags & TIMECARD_NMEA_FLAG_ADVANCED_VALID) != 0u) {
        if (!TimeCardNmeaGnssValid(request->Gnss) ||
            request->LocalOffsetMinutes <
                -TIMECARD_NMEA_LOCAL_MAXIMUM_MINUTES ||
            request->LocalOffsetMinutes >
                TIMECARD_NMEA_LOCAL_MAXIMUM_MINUTES ||
            (request->MessageDisableMask & ~supportedMessageMask) != 0u)
            return STATUS_INVALID_PARAMETER;
        if (request->Gnss != current.Gnss &&
            !TimeCardNmeaVersionAtLeast(current.Version, 1u, 3u))
            return STATUS_NOT_SUPPORTED;
        status = TimeCardNmeaEncodeSignedMagnitude(
            request->CorrectionSeconds, &correction);
        if (!NT_SUCCESS(status))
            return status;
        status = TimeCardNmeaEncodeLocalOffset(
            request->LocalOffsetMinutes, &localOffset);
        if (!NT_SUCCESS(status))
            return status;
    }

    uartConfig.Port = TIMECARD_UART_NMEA;
    uartConfig.Baud = request->Baud;
    status = TimeCardUartSnapshotHardware(
        context, TIMECARD_UART_NMEA, &uartState);
    if (!NT_SUCCESS(status))
        return status;
    uartStateCaptured = TRUE;
    status = TimeCardUartConfigure(context, &uartConfig);
    if (!NT_SUCCESS(status)) {
        (VOID)TimeCardUartRestoreHardware(
            context, TIMECARD_UART_NMEA, &uartState);
        return status;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    version = READ_REGISTER_ULONG((PULONG)&context->NmeaOut->Version);
    if (!TimeCardNmeaVersionAtLeast(version, 1u, 0u) ||
        version != current.Version) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    control = READ_REGISTER_ULONG((PULONG)&context->NmeaOut->Ctrl);
    oldControl = control;
    oldBaud = READ_REGISTER_ULONG((PULONG)&context->NmeaOut->UartBaud);
    oldPolarity = TimeCardNmeaVersionAtLeast(version, 1u, 2u) ?
        READ_REGISTER_ULONG((PULONG)&context->NmeaOut->UartPolarity) : 0u;
    oldCorrection = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->Correction);
    oldLocalOffset = READ_REGISTER_ULONG(
        (PULONG)&context->NmeaOut->LocalOffset);
    configured = TRUE;
    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->Ctrl,
                         control & ~TIMECARD_NMEA_ENABLE);
    value = READ_REGISTER_ULONG((PULONG)&context->NmeaOut->UartBaud);
    value = (value & ~TIMECARD_NMEA_BAUD_MASK) | selector;
    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UartBaud, value);
    if (TimeCardNmeaVersionAtLeast(version, 1u, 2u)) {
        value = READ_REGISTER_ULONG((PULONG)&context->NmeaOut->UartPolarity);
        value = (value & ~1u) | (request->Polarity != 0u ? 0u : 1u);
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UartPolarity, value);
    }
    if ((request->Flags & TIMECARD_NMEA_FLAG_ADVANCED_VALID) != 0u) {
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->Correction,
                             correction);
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->LocalOffset,
                             localOffset);
        control = (control & ~supportedControlMessageMask) |
            ((request->MessageDisableMask <<
              TIMECARD_NMEA_CONTROL_MESSAGE_SHIFT) &
             supportedControlMessageMask);
        if (TimeCardNmeaVersionAtLeast(version, 1u, 3u)) {
            control = (control & ~TIMECARD_NMEA_CONTROL_GNSS_MASK) |
                ((request->Gnss << TIMECARD_NMEA_CONTROL_GNSS_SHIFT) &
                 TIMECARD_NMEA_CONTROL_GNSS_MASK);
        }
    }
    if ((request->Flags & TIMECARD_NMEA_FLAG_CLEAR_ERROR) != 0u)
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->Status,
                             TIMECARD_NMEA_STATUS_ERROR);
    if ((request->Flags & TIMECARD_NMEA_FLAG_ENABLED) != 0)
        control |= TIMECARD_NMEA_ENABLE;
    else
        control &= ~TIMECARD_NMEA_ENABLE;
    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->Ctrl, control);
    status = STATUS_SUCCESS;
done:
    WdfWaitLockRelease(context->RegisterLock);
    if (!NT_SUCCESS(status))
        goto restore;
    status = TimeCardNmeaQuery(context, response);
    if (!NT_SUCCESS(status))
        goto restore;
    enabled = (request->Flags & TIMECARD_NMEA_FLAG_ENABLED) != 0;
    if (((response->Flags & TIMECARD_NMEA_FLAG_ENABLED) != 0) != enabled ||
        response->Baud != request->Baud ||
        response->Polarity != request->Polarity) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto restore;
    }
    if ((request->Flags & TIMECARD_NMEA_FLAG_ADVANCED_VALID) != 0u &&
        (response->CorrectionSeconds != request->CorrectionSeconds ||
         response->LocalOffsetMinutes != request->LocalOffsetMinutes ||
          response->Gnss != request->Gnss ||
          response->MessageDisableMask != request->MessageDisableMask)) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto restore;
    }
    return STATUS_SUCCESS;

restore:
    if (configured) {
        WdfWaitLockAcquire(context->RegisterLock, NULL);
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->Ctrl,
                             oldControl & ~TIMECARD_NMEA_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UartBaud,
                             oldBaud);
        if (TimeCardNmeaVersionAtLeast(version, 1u, 2u))
            WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UartPolarity,
                                 oldPolarity);
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->Correction,
                             oldCorrection);
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->LocalOffset,
                             oldLocalOffset);
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->Ctrl, oldControl);
        WdfWaitLockRelease(context->RegisterLock);
    }
    if (uartStateCaptured)
        (VOID)TimeCardUartRestoreHardware(
            context, TIMECARD_UART_NMEA, &uartState);
    return status;
}

static NTSTATUS
TimeCardNmeaUtcQueryLocked(PDEVICE_CONTEXT context,
                           TIMECARD_NMEA_UTC_CONTROL *control,
                           BOOLEAN writeAllowed)
{
    ULONG handshake = 0u;
    ULONG raw;
    ULONG version;
    ULONG attempt;

    if (context->NmeaOut == NULL)
        return STATUS_NOT_SUPPORTED;
    version = READ_REGISTER_ULONG((PULONG)&context->NmeaOut->Version);
    if (!TimeCardNmeaVersionAtLeast(version, 1u, 6u))
        return STATUS_NOT_SUPPORTED;

    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UtcInfoControl,
                         TIMECARD_NMEA_UTC_READ_REQUEST);
    for (attempt = 0; attempt < 100u; ++attempt) {
        handshake = READ_REGISTER_ULONG(
            (PULONG)&context->NmeaOut->UtcInfoControl);
        if ((handshake & TIMECARD_NMEA_UTC_READ_DONE) != 0u)
            break;
        KeStallExecutionProcessor(1u);
    }
    if (attempt == 100u)
        return STATUS_IO_TIMEOUT;
    raw = READ_REGISTER_ULONG((PULONG)&context->NmeaOut->UtcInfo);

    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Version = version;
    control->RawUtcInfo = raw;
    control->UtcOffsetSeconds = raw >> TIMECARD_NMEA_UTC_OFFSET_SHIFT;
    control->HandshakeControl = handshake;
    control->Flags = TIMECARD_NMEA_UTC_FLAG_PRESENT |
        TIMECARD_NMEA_UTC_FLAG_READ_SUPPORTED;
    if (writeAllowed)
        control->Flags |= TIMECARD_NMEA_UTC_FLAG_WRITE_SUPPORTED;
    if ((raw & TIMECARD_NMEA_UTC_LEAP61) != 0u)
        control->Flags |= TIMECARD_NMEA_UTC_FLAG_LEAP61;
    if ((raw & TIMECARD_NMEA_UTC_LEAP59) != 0u)
        control->Flags |= TIMECARD_NMEA_UTC_FLAG_LEAP59;
    if ((raw & TIMECARD_NMEA_UTC_OFFSET_VALID) != 0u)
        control->Flags |= TIMECARD_NMEA_UTC_FLAG_OFFSET_VALID;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardNmeaUtcQuery(PDEVICE_CONTEXT context,
                     TIMECARD_NMEA_UTC_CONTROL *control)
{
    BOOLEAN writeAllowed;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (!TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ))
        return STATUS_NOT_SUPPORTED;
    writeAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ |
                 TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_WRITE);
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardNmeaUtcQueryLocked(context, control, writeAllowed);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardNmeaUtcSet(PDEVICE_CONTEXT context,
                   const TIMECARD_NMEA_UTC_CONTROL *request,
                   TIMECARD_NMEA_UTC_CONTROL *response)
{
    TIMECARD_NMEA_UTC_CONTROL previous;
    ULONG allowedFlags;
    ULONG index;
    ULONG raw;
    NTSTATUS status;

    allowedFlags = TIMECARD_NMEA_UTC_FLAG_PRESENT |
        TIMECARD_NMEA_UTC_FLAG_READ_SUPPORTED |
        TIMECARD_NMEA_UTC_FLAG_WRITE_SUPPORTED |
        TIMECARD_NMEA_UTC_FLAG_LEAP61 |
        TIMECARD_NMEA_UTC_FLAG_LEAP59 |
        TIMECARD_NMEA_UTC_FLAG_OFFSET_VALID;
    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        request->UtcOffsetSeconds > 0xffffu ||
        request->RawUtcInfo != 0u || request->HandshakeControl != 0u ||
        (request->Flags & ~allowedFlags) != 0u ||
        (request->Flags & (TIMECARD_NMEA_UTC_FLAG_LEAP61 |
                           TIMECARD_NMEA_UTC_FLAG_LEAP59)) ==
            (TIMECARD_NMEA_UTC_FLAG_LEAP61 |
             TIMECARD_NMEA_UTC_FLAG_LEAP59))
        return STATUS_INVALID_PARAMETER;
    for (index = 0; index < ARRAYSIZE(request->Reserved); ++index) {
        if (request->Reserved[index] != 0u)
            return STATUS_INVALID_PARAMETER;
    }
    if (!TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ |
                     TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_WRITE))
        return STATUS_NOT_SUPPORTED;

    raw = request->UtcOffsetSeconds << TIMECARD_NMEA_UTC_OFFSET_SHIFT;
    if ((request->Flags & TIMECARD_NMEA_UTC_FLAG_LEAP61) != 0u)
        raw |= TIMECARD_NMEA_UTC_LEAP61;
    if ((request->Flags & TIMECARD_NMEA_UTC_FLAG_LEAP59) != 0u)
        raw |= TIMECARD_NMEA_UTC_LEAP59;
    if ((request->Flags & TIMECARD_NMEA_UTC_FLAG_OFFSET_VALID) != 0u)
        raw |= TIMECARD_NMEA_UTC_OFFSET_VALID;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardNmeaUtcQueryLocked(context, &previous, TRUE);
    if (!NT_SUCCESS(status))
        goto done;
    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UtcInfo, raw);
    /* Current manual reserves bit 1; only bit 0 commits UTC information. */
    WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UtcInfoControl,
                         TIMECARD_NMEA_UTC_WRITE_REQUEST);
    status = TimeCardNmeaUtcQueryLocked(context, response, TRUE);
    if (!NT_SUCCESS(status) || response->RawUtcInfo != raw) {
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UtcInfo,
                             previous.RawUtcInfo);
        WRITE_REGISTER_ULONG((PULONG)&context->NmeaOut->UtcInfoControl,
                             TIMECARD_NMEA_UTC_WRITE_REQUEST);
        if (NT_SUCCESS(status))
            status = STATUS_DEVICE_DATA_ERROR;
    }
done:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}
