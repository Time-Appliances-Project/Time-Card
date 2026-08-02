/* SPDX-License-Identifier: BSD-3-Clause */
/* Safe access to the documented NetTimeLogic FPGA timing cores. */

#include "timecard.h"

#define TIMECARD_CORE_ENABLE             0x00000001u
#define TIMECARD_PPS_MASTER_ERROR_MASK   0x00000001u
#define TIMECARD_PPS_SLAVE_ERROR_MASK    0x00000003u
#define TIMECARD_TIMECODE_ERROR_MASK     0x00000001u
#define TIMECARD_TOD_ERROR_MASK          0x00000007u
#define TIMECARD_PPS_DELAY_SIGN          (1u << 31)
#define TIMECARD_PPS_DELAY_MAGNITUDE     0x3fffffffu
#define TIMECARD_PPS_DELAY_LEGACY_MAGNITUDE 0x0000ffffu
#define TIMECARD_CORRECTION_SIGN         (1u << 31)
#define TIMECARD_CORRECTION_MAGNITUDE    0x7fffffffu
#define TIMECARD_IRIG_DELAY_MAXIMUM      0x0000ffffu
#define TIMECARD_DCF_DELAY_MAXIMUM       0x3fffffffu
#define TIMECARD_IRIG_MASTER_CONTROL_MASK \
    ((3u << 24) | (7u << 16) | TIMECARD_CORE_ENABLE)
#define TIMECARD_IRIG_SLAVE_CONTROL_MASK \
    ((3u << 24) | (7u << 16) | TIMECARD_CORE_ENABLE)
#define TIMECARD_IRIG_AM_CONTROL          (1u << 1)
#define TIMECARD_IRIG_YEAR_VALID_CONTROL  (1u << 8)
#define TIMECARD_TOD_PROTOCOL_CONFIG_MASK 0x70000000u
#define TIMECARD_TOD_GNSS_CONFIG_MASK    0x0f000000u

C_ASSERT(sizeof(TIMECARD_FPGA_CAPABILITIES) == 64);
C_ASSERT(sizeof(TIMECARD_FPGA_IMAGE_INFO) == 64);
C_ASSERT(sizeof(TIMECARD_CLOCK_TELEMETRY) == 80);
C_ASSERT(sizeof(TIMECARD_PPS_CONTROL) == 64);
C_ASSERT(sizeof(TIMECARD_TIMECODE_CONTROL) == 80);
C_ASSERT(sizeof(TIMECARD_TOD_CONTROL) == 80);
C_ASSERT(sizeof(TIMECARD_CLOCK_ADJUSTMENT) == 80);
C_ASSERT(sizeof(TIMECARD_CLOCK_ADVANCED_CONTROL) == 128);
C_ASSERT(sizeof(TIMECARD_CORE_DESCRIPTOR) == 32);
C_ASSERT(sizeof(TIMECARD_CORE_INVENTORY) == 1056);
C_ASSERT(sizeof(TIMECARD_FPGA_IMAGE_CONTRACT) == 64);
C_ASSERT(FIELD_OFFSET(OCP_REG, DriftFractions) == 0x48);
C_ASSERT(FIELD_OFFSET(OCP_REG, InSyncThreshold) == 0x50);
C_ASSERT(FIELD_OFFSET(OCP_REG, ServoOffsetP) == 0x60);
C_ASSERT(FIELD_OFFSET(OCP_REG, StatusOffset) == 0x70);
C_ASSERT(FIELD_OFFSET(OCP_REG, StatusDriftFraction) == 0x7c);
C_ASSERT(sizeof(OCP_REG) == 0x80);
C_ASSERT(FIELD_OFFSET(TIMECARD_TOD_SLAVE_REG, UartPolarity) == 0x08);
C_ASSERT(FIELD_OFFSET(TIMECARD_TOD_SLAVE_REG, Correction) == 0x10);
C_ASSERT(FIELD_OFFSET(TIMECARD_TOD_SLAVE_REG, UartBaud) == 0x20);
C_ASSERT(FIELD_OFFSET(TIMECARD_TOD_SLAVE_REG, GnssStatus) == 0x40);
C_ASSERT(FIELD_OFFSET(TIMECARD_TOD_MASTER_REG, UartPolarity) == 0x08);
C_ASSERT(FIELD_OFFSET(TIMECARD_TOD_MASTER_REG, Correction) == 0x10);
C_ASSERT(FIELD_OFFSET(TIMECARD_TOD_MASTER_REG, LocalOffset) == 0x14);
C_ASSERT(FIELD_OFFSET(TIMECARD_TOD_MASTER_REG, UartBaud) == 0x20);
C_ASSERT(FIELD_OFFSET(TIMECARD_TOD_MASTER_REG, UtcInfoControl) == 0x100);
C_ASSERT(sizeof(TIMECARD_TOD_SLAVE_REG) == 0x48);
C_ASSERT(sizeof(TIMECARD_TOD_MASTER_REG) == 0x108);
C_ASSERT(FIELD_OFFSET(TIMECARD_PPS_REG, PulseWidth) == 0x10);
C_ASSERT(FIELD_OFFSET(TIMECARD_PPS_REG, CableDelay) == 0x20);
C_ASSERT(sizeof(TIMECARD_PPS_REG) == 0x24);
C_ASSERT(FIELD_OFFSET(TIMECARD_IRIG_MASTER_REG, ControlBits) == 0x14);
C_ASSERT(FIELD_OFFSET(TIMECARD_IRIG_SLAVE_REG, CableDelay) == 0x20);
C_ASSERT(sizeof(TIMECARD_IRIG_MASTER_REG) == 0x18);
C_ASSERT(FIELD_OFFSET(TIMECARD_IRIG_SLAVE_REG, ManualYear) == 0x24);
C_ASSERT(sizeof(TIMECARD_IRIG_SLAVE_REG) == 0x28);
C_ASSERT(FIELD_OFFSET(TIMECARD_DCF_SLAVE_REG, AirDelay) == 0x20);
C_ASSERT(FIELD_OFFSET(TIMECARD_DCF_SLAVE_REG, BitPosition) == 0x30);
C_ASSERT(sizeof(TIMECARD_DCF_MASTER_REG) == 0x14);
C_ASSERT(sizeof(TIMECARD_DCF_SLAVE_REG) == 0x34);

static const ULONG TimeCardTodBaudRates[] = {
    1200u, 2400u, 4800u, 9600u, 19200u, 38400u, 57600u,
    115200u, 230400u, 460800u, 921600u, 1000000u, 2000000u
};

static BOOLEAN
TimeCardStandardFpga(PDEVICE_CONTEXT context)
{
    return context->BoardProfile == TIMECARD_BOARD_FB ||
           context->BoardProfile == TIMECARD_BOARD_CELESTICA;
}

static BOOLEAN
TimeCardVersionPresent(ULONG version)
{
    return version != MAXULONG && (version >> 24) >= 1u;
}

static BOOLEAN
TimeCardVersionAtLeast(ULONG version, ULONG major, ULONG minor)
{
    ULONG actualMajor = version >> 24;
    ULONG actualMinor = (version >> 16) & 0xffu;

    return actualMajor > major ||
           (actualMajor == major && actualMinor >= minor);
}

static LONG
TimeCardSignedMagnitudeDecode(ULONG value, ULONG magnitudeMask)
{
    LONG magnitude = (LONG)(value & magnitudeMask);

    return (value & (1u << 31)) != 0 && magnitude != 0 ?
        -magnitude : magnitude;
}

static NTSTATUS
TimeCardSignedMagnitudeEncode(LONG value, ULONG magnitudeMask,
                              PULONG encoded)
{
    ULONGLONG magnitude;

    magnitude = value < 0 ? (ULONGLONG)(-(LONGLONG)value) :
                            (ULONGLONG)value;
    if (magnitude > magnitudeMask)
        return STATUS_INVALID_PARAMETER;
    *encoded = (ULONG)magnitude;
    if (value < 0)
        *encoded |= 1u << 31;
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardTodSelectorFromBaud(ULONG baud, PULONG selector)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(TimeCardTodBaudRates); ++i) {
        if (TimeCardTodBaudRates[i] == baud) {
            *selector = i;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INVALID_PARAMETER;
}

static ULONG
TimeCardTodSupportedMessageMask(ULONG version, ULONG protocol)
{
    switch (protocol) {
    case TIMECARD_TOD_PROTOCOL_NMEA:
        return 0x1bu |
            (TimeCardVersionAtLeast(version, 2u, 0u) ? 0x04u : 0u);
    case TIMECARD_TOD_PROTOCOL_UBX:
        return TimeCardVersionAtLeast(version, 1u, 7u) ? 0x1fu : 0x07u;
    case TIMECARD_TOD_PROTOCOL_TSIP:
        return 0x1fu;
    case TIMECARD_TOD_PROTOCOL_ESIP:
        return 0xffu;
    case TIMECARD_TOD_PROTOCOL_PFEC:
        return TimeCardVersionAtLeast(version, 2u, 3u) ? 0x7fu : 0u;
    default:
        return 0u;
    }
}

static BOOLEAN
TimeCardTodProtocolSupported(ULONG version, ULONG protocol)
{
    switch (protocol) {
    case TIMECARD_TOD_PROTOCOL_NMEA:
        return TRUE;
    case TIMECARD_TOD_PROTOCOL_UBX:
        return TimeCardVersionAtLeast(version, 1u, 6u);
    case TIMECARD_TOD_PROTOCOL_TSIP:
        return TimeCardVersionAtLeast(version, 1u, 9u);
    case TIMECARD_TOD_PROTOCOL_ESIP:
        return TimeCardVersionAtLeast(version, 2u, 1u);
    case TIMECARD_TOD_PROTOCOL_PFEC:
        return TimeCardVersionAtLeast(version, 2u, 3u);
    default:
        return FALSE;
    }
}

static BOOLEAN
TimeCardTodUtcTelemetrySupported(ULONG version, ULONG protocol)
{
    switch (protocol) {
    case TIMECARD_TOD_PROTOCOL_NMEA:
        return TimeCardVersionAtLeast(version, 2u, 0u);
    case TIMECARD_TOD_PROTOCOL_UBX:
        return TimeCardVersionAtLeast(version, 1u, 6u);
    case TIMECARD_TOD_PROTOCOL_TSIP:
        return TimeCardVersionAtLeast(version, 1u, 9u);
    case TIMECARD_TOD_PROTOCOL_ESIP:
        return TimeCardVersionAtLeast(version, 2u, 1u);
    case TIMECARD_TOD_PROTOCOL_PFEC:
        return TimeCardVersionAtLeast(version, 2u, 3u);
    default:
        return FALSE;
    }
}

static BOOLEAN
TimeCardTodGnssTelemetrySupported(ULONG version, ULONG protocol)
{
    switch (protocol) {
    case TIMECARD_TOD_PROTOCOL_NMEA:
        return TimeCardVersionAtLeast(version, 2u, 2u);
    case TIMECARD_TOD_PROTOCOL_UBX:
        return TimeCardVersionAtLeast(version, 1u, 7u);
    case TIMECARD_TOD_PROTOCOL_TSIP:
        return TimeCardVersionAtLeast(version, 1u, 9u);
    case TIMECARD_TOD_PROTOCOL_ESIP:
        return TimeCardVersionAtLeast(version, 2u, 1u);
    case TIMECARD_TOD_PROTOCOL_PFEC:
        return TimeCardVersionAtLeast(version, 2u, 3u);
    default:
        return FALSE;
    }
}

static ULONG
TimeCardCoreMaskForProfile(PDEVICE_CONTEXT context)
{
    if (!TimeCardStandardFpga(context))
        return context->BoardProfile == TIMECARD_BOARD_ART ?
            TIMECARD_FPGA_CORE_SIGNAL_TIMESTAMPER : 0u;
    return TIMECARD_FPGA_CORE_PPS_MASTER |
           TIMECARD_FPGA_CORE_PPS_SLAVE |
           TIMECARD_FPGA_CORE_IRIG_MASTER |
           TIMECARD_FPGA_CORE_IRIG_SLAVE |
           TIMECARD_FPGA_CORE_DCF_MASTER |
           TIMECARD_FPGA_CORE_DCF_SLAVE |
           TIMECARD_FPGA_CORE_TOD_SLAVE |
           TIMECARD_FPGA_CORE_TOD_MASTER |
           TIMECARD_FPGA_CORE_SIGNAL_GENERATOR |
           TIMECARD_FPGA_CORE_FREQUENCY_INPUT |
           TIMECARD_FPGA_CORE_SIGNAL_TIMESTAMPER;
}

NTSTATUS
TimeCardGetFpgaCapabilities(PDEVICE_CONTEXT context,
                            TIMECARD_FPGA_CAPABILITIES *capabilities)
{
    TIMECARD_FPGA_IMAGE_INFO image;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(capabilities, sizeof(*capabilities));
    capabilities->Size = sizeof(*capabilities);
    capabilities->AbiVersion = TIMECARD_ABI_VERSION;
    capabilities->Layout = context->Layout;
    capabilities->BoardProfile = context->BoardProfile;
    capabilities->CoreMask = TimeCardCoreMaskForProfile(context);
    capabilities->FeatureFlags =
        TIMECARD_FPGA_FEATURE_STATIC_CORE_INVENTORY;
    if ((capabilities->CoreMask &
         TIMECARD_FPGA_CORE_SIGNAL_TIMESTAMPER) != 0u &&
        context->TimestampInterruptMask != 0u) {
        capabilities->FeatureFlags |=
            TIMECARD_FPGA_FEATURE_TIMESTAMP_CAPTURE;
    }
    if (!TimeCardStandardFpga(context))
        return STATUS_SUCCESS;

    capabilities->FeatureFlags =
        capabilities->FeatureFlags |
        TIMECARD_FPGA_FEATURE_CLOCK_TELEMETRY |
        TIMECARD_FPGA_FEATURE_CLOCK_SMOOTH_ADJUST;
    if (NT_SUCCESS(TimeCardFpgaImageQuery(context, &image)))
        capabilities->FeatureFlags |=
            TIMECARD_FPGA_FEATURE_EXACT_IMAGE_CONTRACT;
    if ((capabilities->CoreMask &
         (TIMECARD_FPGA_CORE_PPS_MASTER |
          TIMECARD_FPGA_CORE_PPS_SLAVE)) != 0) {
        capabilities->FeatureFlags |=
            TIMECARD_FPGA_FEATURE_PPS_CONFIGURATION;
    }
    if ((capabilities->CoreMask &
         (TIMECARD_FPGA_CORE_IRIG_MASTER |
          TIMECARD_FPGA_CORE_IRIG_SLAVE |
          TIMECARD_FPGA_CORE_DCF_MASTER |
          TIMECARD_FPGA_CORE_DCF_SLAVE)) != 0) {
        capabilities->FeatureFlags |=
            TIMECARD_FPGA_FEATURE_TIMECODE_CONFIGURATION;
    }
    if ((capabilities->CoreMask & TIMECARD_FPGA_CORE_TOD_SLAVE) != 0)
        capabilities->FeatureFlags |=
            TIMECARD_FPGA_FEATURE_TOD_CONFIGURATION;
    if ((capabilities->CoreMask &
         TIMECARD_FPGA_CORE_SIGNAL_GENERATOR) != 0) {
        capabilities->FeatureFlags |=
            TIMECARD_FPGA_FEATURE_SIGNAL_REPEAT_COUNT |
            TIMECARD_FPGA_FEATURE_SIGNAL_CABLE_DELAY |
            TIMECARD_FPGA_FEATURE_SIGNAL_STATUS;
        if (context->SignalInterruptMask != 0u)
            capabilities->FeatureFlags |=
                TIMECARD_FPGA_FEATURE_SIGNAL_COMPLETION_EVENTS;
    }
    capabilities->KnownGaps =
        TIMECARD_FPGA_GAP_CONFIGURATION_SLAVE |
        TIMECARD_FPGA_GAP_OPTIONAL_CLOCK_REGISTERS |
        TIMECARD_FPGA_GAP_TOD_MASTER_UTC_HANDSHAKE |
        TIMECARD_FPGA_GAP_SYNTHESIS_FEATURE_REPORTING;
    if (TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_CLOCK_SERVO_LOG) ||
        TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_CLOCK_ADVANCED)) {
        capabilities->KnownGaps &=
            ~TIMECARD_FPGA_GAP_OPTIONAL_CLOCK_REGISTERS;
        capabilities->FeatureFlags |=
            TIMECARD_FPGA_FEATURE_CLOCK_ADVANCED_CONFIGURATION;
    }
    if (TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ)) {
        capabilities->KnownGaps &=
            ~TIMECARD_FPGA_GAP_TOD_MASTER_UTC_HANDSHAKE;
        capabilities->FeatureFlags |=
            TIMECARD_FPGA_FEATURE_TOD_MASTER_UTC;
    }
    if (TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_IRIG_MASTER_AM) ||
        TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_AM) ||
        TimeCardFpgaContractAllows(
            context, TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_YEAR)) {
        capabilities->FeatureFlags |=
            TIMECARD_FPGA_FEATURE_TIMECODE_ADVANCED;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardFpgaImageQuery(PDEVICE_CONTEXT context,
                       TIMECARD_FPGA_IMAGE_INFO *imageInfo)
{
    ULONG decoded;
    ULONG offset;
    ULONG rawVersion;

    RtlZeroMemory(imageInfo, sizeof(*imageInfo));
    imageInfo->Size = sizeof(*imageInfo);
    imageInfo->AbiVersion = TIMECARD_ABI_VERSION;
    if (!context->HardwareReady || context->Bar0Base == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (!TimeCardStandardFpga(context))
        return STATUS_NOT_SUPPORTED;

    /* These are the only image-version offsets documented by the Linux map. */
    if (context->Layout == TIMECARD_LAYOUT_MSI)
        offset = TIMECARD_IMAGE_OFFSET_MSI;
    else if (context->Layout == TIMECARD_LAYOUT_MSIX)
        offset = TIMECARD_IMAGE_OFFSET_MSIX;
    else
        return STATUS_NOT_SUPPORTED;

    if (offset > context->Bar0Length ||
        sizeof(rawVersion) > context->Bar0Length - offset) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    rawVersion = READ_REGISTER_ULONG(
        (PULONG)(context->Bar0Base + offset));
    WdfWaitLockRelease(context->RegisterLock);
    if (rawVersion == 0u || rawVersion == MAXULONG)
        return STATUS_NOT_SUPPORTED;

    imageInfo->Flags = TIMECARD_FPGA_IMAGE_FLAG_PRESENT;
    imageInfo->RawVersion = rawVersion;
    imageInfo->Layout = context->Layout;
    imageInfo->BoardProfile = context->BoardProfile;
    imageInfo->RegisterOffset = offset;

    decoded = rawVersion;
    if ((decoded & 0xffffu) == 0u) {
        decoded >>= 16;
        imageInfo->Flags |= TIMECARD_FPGA_IMAGE_FLAG_LOADER;
    }
    imageInfo->ImageTag = decoded >> 15;
    imageInfo->ImageVersion = decoded & 0x7fffu;
    if (imageInfo->ImageTag != 0u)
        imageInfo->Flags |= TIMECARD_FPGA_IMAGE_FLAG_FPGA_FIRMWARE;
    return imageInfo->ImageVersion == 0u ?
        STATUS_NOT_SUPPORTED : STATUS_SUCCESS;
}

NTSTATUS
TimeCardFpgaContractQuery(PDEVICE_CONTEXT context,
                          TIMECARD_FPGA_IMAGE_CONTRACT *contract)
{
    TIMECARD_FPGA_IMAGE_INFO image;
    ULONG capabilities;
    ULONG contractedImage;
    NTSTATUS imageStatus;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(contract, sizeof(*contract));
    contract->Size = sizeof(*contract);
    contract->AbiVersion = TIMECARD_ABI_VERSION;
    contract->BoardProfile = context->BoardProfile;
    contract->Layout = context->Layout;
    imageStatus = TimeCardFpgaImageQuery(context, &image);
    if (NT_SUCCESS(imageStatus)) {
        contract->RawImageVersion = image.RawVersion;
        contract->StatusFlags |= TIMECARD_FPGA_CONTRACT_FLAG_IMAGE_PRESENT;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    contractedImage = context->FpgaContractImageVersion;
    capabilities = context->FpgaContractCapabilities;
    WdfWaitLockRelease(context->RegisterLock);
    contract->CapabilityFlags = capabilities;
    if (NT_SUCCESS(imageStatus) &&
        (image.Flags & TIMECARD_FPGA_IMAGE_FLAG_LOADER) == 0u &&
        (image.Flags & TIMECARD_FPGA_IMAGE_FLAG_FPGA_FIRMWARE) != 0u &&
        contractedImage == image.RawVersion && contractedImage != 0u) {
        contract->StatusFlags |= TIMECARD_FPGA_CONTRACT_FLAG_EXACT_MATCH;
        if (capabilities != 0u) {
            contract->StatusFlags |= TIMECARD_FPGA_CONTRACT_FLAG_ACTIVE;
            contract->EffectiveFlags = capabilities;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardFpgaContractSet(PDEVICE_CONTEXT context,
                        const TIMECARD_FPGA_IMAGE_CONTRACT *request,
                        TIMECARD_FPGA_IMAGE_CONTRACT *response)
{
    TIMECARD_FPGA_IMAGE_INFO image;
    ULONG index;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (request->Size < sizeof(*request) ||
        request->AbiVersion != TIMECARD_ABI_VERSION ||
        request->Acknowledgement !=
            TIMECARD_FPGA_CONTRACT_ACKNOWLEDGEMENT ||
        request->BoardProfile != context->BoardProfile ||
        request->Layout != context->Layout ||
        (request->CapabilityFlags &
         ~TIMECARD_FPGA_CONTRACT_ALL_FLAGS) != 0u ||
        request->EffectiveFlags != 0u || request->StatusFlags != 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    for (index = 0; index < ARRAYSIZE(request->Reserved); ++index) {
        if (request->Reserved[index] != 0u)
            return STATUS_INVALID_PARAMETER;
    }
    if ((request->CapabilityFlags &
         TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_WRITE) != 0u &&
        (request->CapabilityFlags &
         TIMECARD_FPGA_CONTRACT_TOD_MASTER_UTC_READ) == 0u) {
        return STATUS_INVALID_PARAMETER;
    }
    status = TimeCardFpgaImageQuery(context, &image);
    if (!NT_SUCCESS(status))
        return STATUS_NOT_SUPPORTED;
    if ((image.Flags & TIMECARD_FPGA_IMAGE_FLAG_LOADER) != 0u ||
        (image.Flags & TIMECARD_FPGA_IMAGE_FLAG_FPGA_FIRMWARE) == 0u)
        return STATUS_NOT_SUPPORTED;
    if (request->RawImageVersion == 0u ||
        request->RawImageVersion != image.RawVersion)
        return STATUS_REVISION_MISMATCH;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    context->FpgaContractImageVersion = image.RawVersion;
    context->FpgaContractCapabilities = request->CapabilityFlags;
    WdfWaitLockRelease(context->RegisterLock);
    return TimeCardFpgaContractQuery(context, response);
}

BOOLEAN
TimeCardFpgaContractAllows(PDEVICE_CONTEXT context, ULONG capability)
{
    TIMECARD_FPGA_IMAGE_INFO image;
    BOOLEAN allowed;

    if (capability == 0u ||
        (capability & ~TIMECARD_FPGA_CONTRACT_ALL_FLAGS) != 0u ||
        !NT_SUCCESS(TimeCardFpgaImageQuery(context, &image))) {
        return FALSE;
    }
    if ((image.Flags & TIMECARD_FPGA_IMAGE_FLAG_LOADER) != 0u ||
        (image.Flags & TIMECARD_FPGA_IMAGE_FLAG_FPGA_FIRMWARE) == 0u)
        return FALSE;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    allowed = context->FpgaContractImageVersion == image.RawVersion &&
        (context->FpgaContractCapabilities & capability) == capability;
    WdfWaitLockRelease(context->RegisterLock);
    return allowed;
}

static VOID
TimeCardInventoryAppend(TIMECARD_CORE_INVENTORY *inventory, ULONG type,
                        ULONG instance, ULONG offset, ULONG span,
                        ULONG interruptMessage, ULONG version, ULONG flags)
{
    TIMECARD_CORE_DESCRIPTOR *descriptor;

    if (inventory->Count >= TIMECARD_CORE_INVENTORY_MAX)
        return;
    descriptor = &inventory->Cores[inventory->Count++];
    descriptor->Type = type;
    descriptor->Instance = instance;
    descriptor->RegisterOffset = offset;
    descriptor->RegisterSpan = span;
    descriptor->InterruptMessage = interruptMessage;
    descriptor->Version = version;
    descriptor->Flags = TIMECARD_CORE_FLAG_TRUSTED_PROFILE | flags;
    if (version != 0u && version != MAXULONG)
        descriptor->Flags |= TIMECARD_CORE_FLAG_VERSION_VALID;
    if (interruptMessage != TIMECARD_CORE_INTERRUPT_NONE)
        descriptor->Flags |= TIMECARD_CORE_FLAG_INTERRUPT;
}

NTSTATUS
TimeCardCoreInventoryQuery(PDEVICE_CONTEXT context,
                           TIMECARD_CORE_INVENTORY *inventory)
{
    static const ULONG timestampMsi[TIMECARD_TIMESTAMP_COUNT] = {
        TIMECARD_TIMESTAMP0_OFFSET_MSI, TIMECARD_TIMESTAMP1_OFFSET_MSI,
        TIMECARD_TIMESTAMP2_OFFSET_MSI, TIMECARD_TIMESTAMP3_OFFSET_MSI,
        TIMECARD_TIMESTAMP4_OFFSET_MSI, TIMECARD_TIMESTAMP5_OFFSET_MSI
    };
    static const ULONG timestampMsix[TIMECARD_TIMESTAMP_COUNT] = {
        TIMECARD_TIMESTAMP0_OFFSET_MSIX, TIMECARD_TIMESTAMP1_OFFSET_MSIX,
        TIMECARD_TIMESTAMP2_OFFSET_MSIX, TIMECARD_TIMESTAMP3_OFFSET_MSIX,
        TIMECARD_TIMESTAMP4_OFFSET_MSIX, TIMECARD_TIMESTAMP5_OFFSET_MSIX
    };
    static const ULONG timestampArt[TIMECARD_TIMESTAMP_COUNT] = {
        TIMECARD_TIMESTAMP0_OFFSET_ART, TIMECARD_TIMESTAMP1_OFFSET_ART,
        TIMECARD_TIMESTAMP2_OFFSET_ART, TIMECARD_TIMESTAMP3_OFFSET_ART,
        TIMECARD_TIMESTAMP4_OFFSET_ART, TIMECARD_TIMESTAMP5_OFFSET_ART
    };
    static const ULONG timestampMsiIrq[TIMECARD_TIMESTAMP_COUNT] = {
        1u, 2u, 6u, 15u, 16u, 0u
    };
    static const ULONG timestampMsixIrq[TIMECARD_TIMESTAMP_COUNT] = {
        33u, 34u, 38u, 47u, 48u, 32u
    };
    static const ULONG timestampArtIrq[TIMECARD_TIMESTAMP_COUNT] = {
        12u, 8u, 10u, 14u, 15u, 11u
    };
    TIMECARD_FPGA_IMAGE_INFO image;
    const ULONG *timestampOffsets;
    const ULONG *timestampIrqs;
    ULONG clockOffset;
    ULONG coreFlags;
    ULONG index;
    ULONG signalBase;
    ULONG signalIrqBase;
    ULONG span;
    NTSTATUS imageStatus;

    if (!context->HardwareReady || context->Bar0Base == NULL)
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(inventory, sizeof(*inventory));
    inventory->Size = sizeof(*inventory);
    inventory->AbiVersion = TIMECARD_ABI_VERSION;
    inventory->BoardProfile = context->BoardProfile;
    inventory->Layout = context->Layout;
    inventory->Flags = TIMECARD_INVENTORY_FLAG_STATIC_PROFILE |
        TIMECARD_INVENTORY_FLAG_NO_CONFIG_SLAVE |
        TIMECARD_INVENTORY_FLAG_SYNTHESIS_UNKNOWN;

    imageStatus = TimeCardFpgaImageQuery(context, &image);
    if (NT_SUCCESS(imageStatus)) {
        inventory->RawImageVersion = image.RawVersion;
        inventory->Flags |= TIMECARD_INVENTORY_FLAG_IMAGE_PRESENT;
    }

    if (context->Layout == TIMECARD_LAYOUT_MSIX) {
        clockOffset = TIMECARD_CLOCK_OFFSET_MSIX;
        timestampOffsets = timestampMsix;
        timestampIrqs = timestampMsixIrq;
        signalBase = TIMECARD_SIGNAL_BASE_MSIX;
        signalIrqBase = 43u;
    } else if (context->Layout == TIMECARD_LAYOUT_ART) {
        clockOffset = TIMECARD_CLOCK_OFFSET_ART;
        timestampOffsets = timestampArt;
        timestampIrqs = timestampArtIrq;
        signalBase = 0u;
        signalIrqBase = 0u;
    } else {
        clockOffset = TIMECARD_CLOCK_OFFSET_MSI;
        timestampOffsets = timestampMsi;
        timestampIrqs = timestampMsiIrq;
        signalBase = TIMECARD_SIGNAL_BASE_MSI;
        signalIrqBase = 11u;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    TimeCardInventoryAppend(
        inventory, TIMECARD_CORE_TYPE_CLOCK, 0u, clockOffset,
        TIMECARD_REGISTER_WINDOW_SIZE, TIMECARD_CORE_INTERRUPT_NONE,
        context->Regs != NULL ?
            READ_REGISTER_ULONG((PULONG)&context->Regs->Version) : 0u,
        (context->Regs != NULL ? TIMECARD_CORE_FLAG_REGISTER_MAPPED : 0u) |
        TIMECARD_CORE_FLAG_SYNTHESIS_UNKNOWN);
    if (NT_SUCCESS(imageStatus)) {
        TimeCardInventoryAppend(
            inventory, TIMECARD_CORE_TYPE_IMAGE_IDENTITY, 0u,
            image.RegisterOffset, sizeof(ULONG),
            TIMECARD_CORE_INTERRUPT_NONE, image.RawVersion,
            TIMECARD_CORE_FLAG_REGISTER_MAPPED |
            TIMECARD_CORE_FLAG_CURRENT_LAYOUT);
    }

    span = context->Layout == TIMECARD_LAYOUT_ART ? 0x20u :
        TIMECARD_REGISTER_WINDOW_SIZE;
    for (index = 0; index < TIMECARD_TIMESTAMP_COUNT; ++index) {
        ULONG version = context->Timestamp[index] != NULL ?
            READ_REGISTER_ULONG(
                (PULONG)&context->Timestamp[index]->Version) : 0u;

        coreFlags = TIMECARD_CORE_FLAG_SYNTHESIS_UNKNOWN;
        if (context->Timestamp[index] != NULL)
            coreFlags |= TIMECARD_CORE_FLAG_REGISTER_MAPPED;
        if (TimeCardVersionAtLeast(version, 1u, 3u))
            coreFlags |= TIMECARD_CORE_FLAG_CURRENT_LAYOUT;
        if (context->Layout == TIMECARD_LAYOUT_ART)
            coreFlags |= TIMECARD_CORE_FLAG_BASIC_SURFACE;
        TimeCardInventoryAppend(
            inventory, TIMECARD_CORE_TYPE_SIGNAL_TIMESTAMPER, index,
            timestampOffsets[index], span, timestampIrqs[index], version,
            coreFlags);
    }

    if (TimeCardStandardFpga(context)) {
#define TIMECARD_APPEND_VERSIONED(_type, _instance, _offset, _pointer)       \
        TimeCardInventoryAppend(                                             \
            inventory, (_type), (_instance), (_offset),                     \
            TIMECARD_REGISTER_WINDOW_SIZE, TIMECARD_CORE_INTERRUPT_NONE,     \
            (_pointer) != NULL ? READ_REGISTER_ULONG(                        \
                (PULONG)&(_pointer)->Version) : 0u,                           \
            ((_pointer) != NULL ? TIMECARD_CORE_FLAG_REGISTER_MAPPED : 0u) | \
            TIMECARD_CORE_FLAG_SYNTHESIS_UNKNOWN)
        if (context->Layout == TIMECARD_LAYOUT_MSIX) {
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_PPS_MASTER, 0u,
                TIMECARD_PPS_MASTER_OFFSET_MSIX, context->PpsMaster);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_PPS_SLAVE, 0u,
                TIMECARD_PPS_SLAVE_OFFSET_MSIX, context->PpsSlave);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_TOD_SLAVE, 0u,
                TIMECARD_TOD_OFFSET_MSIX, context->Tod);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_IRIG_MASTER, 0u,
                TIMECARD_IRIG_MASTER_OFFSET_MSIX, context->IrigMaster);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_IRIG_SLAVE, 0u,
                TIMECARD_IRIG_SLAVE_OFFSET_MSIX, context->IrigSlave);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_DCF_MASTER, 0u,
                TIMECARD_DCF_MASTER_OFFSET_MSIX, context->DcfMaster);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_DCF_SLAVE, 0u,
                TIMECARD_DCF_SLAVE_OFFSET_MSIX, context->DcfSlave);
        } else {
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_PPS_MASTER, 0u,
                TIMECARD_PPS_MASTER_OFFSET_MSI, context->PpsMaster);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_PPS_SLAVE, 0u,
                TIMECARD_PPS_SLAVE_OFFSET_MSI, context->PpsSlave);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_TOD_SLAVE, 0u,
                TIMECARD_TOD_OFFSET_MSI, context->Tod);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_IRIG_MASTER, 0u,
                TIMECARD_IRIG_MASTER_OFFSET_MSI, context->IrigMaster);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_IRIG_SLAVE, 0u,
                TIMECARD_IRIG_SLAVE_OFFSET_MSI, context->IrigSlave);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_DCF_MASTER, 0u,
                TIMECARD_DCF_MASTER_OFFSET_MSI, context->DcfMaster);
            TIMECARD_APPEND_VERSIONED(TIMECARD_CORE_TYPE_DCF_SLAVE, 0u,
                TIMECARD_DCF_SLAVE_OFFSET_MSI, context->DcfSlave);
        }
#undef TIMECARD_APPEND_VERSIONED
        TimeCardInventoryAppend(
            inventory, TIMECARD_CORE_TYPE_TOD_MASTER, 0u,
            context->Layout == TIMECARD_LAYOUT_MSIX ?
                TIMECARD_NMEA_OUT_OFFSET_MSIX : TIMECARD_NMEA_OUT_OFFSET_MSI,
            TIMECARD_REGISTER_WINDOW_SIZE, TIMECARD_CORE_INTERRUPT_NONE, 0u,
            (context->NmeaOut != NULL ?
                TIMECARD_CORE_FLAG_REGISTER_MAPPED : 0u) |
            TIMECARD_CORE_FLAG_OPERATOR_ONLY |
            TIMECARD_CORE_FLAG_SYNTHESIS_UNKNOWN);
        for (index = 0; index < TIMECARD_SIGNAL_COUNT; ++index) {
            ULONG version = context->Signal[index] != NULL ?
                READ_REGISTER_ULONG(
                    (PULONG)&context->Signal[index]->Version) : 0u;

            TimeCardInventoryAppend(
                inventory, TIMECARD_CORE_TYPE_SIGNAL_GENERATOR, index,
                signalBase + index * TIMECARD_REGISTER_WINDOW_SIZE,
                TIMECARD_REGISTER_WINDOW_SIZE, signalIrqBase + index,
                version,
                (context->Signal[index] != NULL ?
                    TIMECARD_CORE_FLAG_REGISTER_MAPPED : 0u) |
                (TimeCardVersionAtLeast(version, 1u, 3u) ?
                    TIMECARD_CORE_FLAG_CURRENT_LAYOUT : 0u) |
                TIMECARD_CORE_FLAG_SYNTHESIS_UNKNOWN);
        }
        for (index = 0; index < TIMECARD_FREQUENCY_COUNT; ++index) {
            TimeCardInventoryAppend(
                inventory, TIMECARD_CORE_TYPE_FREQUENCY_INPUT, index,
                (context->Layout == TIMECARD_LAYOUT_MSIX ?
                    TIMECARD_FREQUENCY_BASE_MSIX :
                    TIMECARD_FREQUENCY_BASE_MSI) +
                    index * TIMECARD_REGISTER_WINDOW_SIZE,
                TIMECARD_REGISTER_WINDOW_SIZE,
                TIMECARD_CORE_INTERRUPT_NONE, 0u,
                (context->Frequency[index] != NULL ?
                    TIMECARD_CORE_FLAG_REGISTER_MAPPED : 0u) |
                TIMECARD_CORE_FLAG_SYNTHESIS_UNKNOWN);
        }
    }
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardClockTelemetryQuery(PDEVICE_CONTEXT context,
                            TIMECARD_CLOCK_TELEMETRY *telemetry)
{
    BOOLEAN optionalAllowed;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (!TimeCardStandardFpga(context))
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(telemetry, sizeof(*telemetry));
    telemetry->Size = sizeof(*telemetry);
    telemetry->KnownGaps =
        TIMECARD_FPGA_GAP_OPTIONAL_CLOCK_REGISTERS |
        TIMECARD_FPGA_GAP_SYNTHESIS_FEATURE_REPORTING;
    optionalAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_CLOCK_SERVO_LOG);
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    telemetry->Version = READ_REGISTER_ULONG((PULONG)&context->Regs->Version);
    if (!TimeCardVersionPresent(telemetry->Version)) {
        WdfWaitLockRelease(context->RegisterLock);
        return STATUS_NOT_SUPPORTED;
    }
    telemetry->Control = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl);
    if (TimeCardVersionAtLeast(telemetry->Version, 1u, 2u)) {
        telemetry->Status = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->Status);
    }
    telemetry->Select = READ_REGISTER_ULONG((PULONG)&context->Regs->Select);
    telemetry->CoreMask = TimeCardCoreMaskForProfile(context);
    telemetry->Flags = TIMECARD_CLOCK_TELEMETRY_FLAG_PRESENT;
    if ((telemetry->Status & 1u) != 0)
        telemetry->Flags |= TIMECARD_CLOCK_TELEMETRY_FLAG_IN_SYNC;
    if (TimeCardVersionAtLeast(telemetry->Version, 1u, 4u) &&
        (telemetry->Status & 2u) != 0)
        telemetry->Flags |= TIMECARD_CLOCK_TELEMETRY_FLAG_IN_HOLDOVER;

    if (TimeCardVersionAtLeast(telemetry->Version, 1u, 0u)) {
        ULONG threshold = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->InSyncThreshold);

        if (threshold != MAXULONG)
            telemetry->InSyncThreshold = threshold;
    }
    if (optionalAllowed &&
        TimeCardVersionAtLeast(telemetry->Version, 1u, 6u)) {
        telemetry->ServoOffsetP = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoOffsetP);
        telemetry->ServoOffsetI = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoOffsetI);
        telemetry->ServoDriftP = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoDriftP);
        telemetry->ServoDriftI = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoDriftI);
        telemetry->StatusOffsetNanoseconds = TimeCardSignedMagnitudeDecode(
            READ_REGISTER_ULONG((PULONG)&context->Regs->StatusOffset),
            0x7fffffffu);
        telemetry->StatusDriftPpb = TimeCardSignedMagnitudeDecode(
            READ_REGISTER_ULONG((PULONG)&context->Regs->StatusDrift),
            0x7fffffffu);
        telemetry->Flags |=
            TIMECARD_CLOCK_TELEMETRY_FLAG_SERVO_AVAILABLE |
            TIMECARD_CLOCK_TELEMETRY_FLAG_LOG_AVAILABLE;
        if (TimeCardVersionAtLeast(telemetry->Version, 2u, 0u)) {
            telemetry->StatusOffsetFraction = READ_REGISTER_ULONG(
                (PULONG)&context->Regs->StatusOffsetFraction);
            telemetry->StatusDriftFraction = READ_REGISTER_ULONG(
                (PULONG)&context->Regs->StatusDriftFraction);
            telemetry->Flags |=
                TIMECARD_CLOCK_TELEMETRY_FLAG_FRACTIONAL_LOG;
        }
        telemetry->KnownGaps &=
            ~TIMECARD_FPGA_GAP_OPTIONAL_CLOCK_REGISTERS;
    }
    WdfWaitLockRelease(context->RegisterLock);
    return STATUS_SUCCESS;
}

static NTSTATUS
TimeCardClockAdjustmentQueryLocked(
    PDEVICE_CONTEXT context, TIMECARD_CLOCK_ADJUSTMENT *adjustment)
{
    ULONG drift;
    ULONG fraction = 0u;
    ULONG offset;
    ULONG version;
    signed __int64 driftQ16;

    if (context->Regs == NULL)
        return STATUS_NOT_SUPPORTED;
    version = READ_REGISTER_ULONG((PULONG)&context->Regs->Version);
    if (!TimeCardVersionPresent(version))
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(adjustment, sizeof(*adjustment));
    adjustment->Size = sizeof(*adjustment);
    adjustment->Flags = TIMECARD_CLOCK_ADJUST_FLAG_PRESENT;
    adjustment->Version = version;
    adjustment->Control = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl);
    adjustment->Select = READ_REGISTER_ULONG((PULONG)&context->Regs->Select);
    offset = READ_REGISTER_ULONG((PULONG)&context->Regs->OffsetNs);
    adjustment->OffsetNanoseconds = TimeCardSignedMagnitudeDecode(
        offset, 0x7fffffffu);
    adjustment->OffsetIntervalNanoseconds = READ_REGISTER_ULONG(
        (PULONG)&context->Regs->OffsetWindowNs);
    drift = READ_REGISTER_ULONG((PULONG)&context->Regs->DriftNs);
    if (TimeCardVersionAtLeast(version, 2u, 0u)) {
        fraction = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->DriftFractions) & 0xffffu;
        adjustment->Flags |= TIMECARD_CLOCK_ADJUST_FLAG_FRACTIONAL_DRIFT;
    }
    driftQ16 = ((signed __int64)(drift & 0x7fffffffu) << 16) |
               fraction;
    if ((drift & 0x80000000u) != 0u && driftQ16 != 0)
        driftQ16 = -driftQ16;
    adjustment->DriftPpbQ16 = driftQ16;
    adjustment->DriftIntervalNanoseconds = READ_REGISTER_ULONG(
        (PULONG)&context->Regs->DriftWindowNs);
    adjustment->InSyncThresholdNanoseconds = READ_REGISTER_ULONG(
        (PULONG)&context->Regs->InSyncThreshold);
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardClockAdjustmentQuery(
    PDEVICE_CONTEXT context, TIMECARD_CLOCK_ADJUSTMENT *adjustment)
{
    NTSTATUS status;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (!TimeCardStandardFpga(context))
        return STATUS_NOT_SUPPORTED;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardClockAdjustmentQueryLocked(context, adjustment);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardClockAdjustmentSet(
    PDEVICE_CONTEXT context, const TIMECARD_CLOCK_ADJUSTMENT *request,
    TIMECARD_CLOCK_ADJUSTMENT *response)
{
    const signed __int64 maximumDriftQ16 =
        (signed __int64)TIMECARD_CLOCK_ADJUST_MAX_DRIFT_PPB << 16;
    unsigned __int64 driftMagnitude;
    ULONG allowedFlags;
    ULONG applyFlags;
    ULONG control;
    ULONG drift;
    ULONG driftFraction;
    ULONG offset;
    ULONG oldControl;
    ULONG oldDrift;
    ULONG oldDriftFraction;
    ULONG oldDriftWindow;
    ULONG oldOffset;
    ULONG oldOffsetWindow;
    ULONG oldSelect;
    ULONG oldThreshold;
    ULONG version;
    ULONG index;
    NTSTATUS status;

    allowedFlags = TIMECARD_CLOCK_ADJUST_FLAG_PRESENT |
        TIMECARD_CLOCK_ADJUST_FLAG_FRACTIONAL_DRIFT |
        TIMECARD_CLOCK_ADJUST_FLAG_APPLY_OFFSET |
        TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT |
        TIMECARD_CLOCK_ADJUST_FLAG_APPLY_THRESHOLD;
    applyFlags = request->Flags &
        (TIMECARD_CLOCK_ADJUST_FLAG_APPLY_OFFSET |
         TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT |
         TIMECARD_CLOCK_ADJUST_FLAG_APPLY_THRESHOLD);
    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (!TimeCardStandardFpga(context))
        return STATUS_NOT_SUPPORTED;
    if (request->Size < sizeof(*request) ||
        (request->Flags & ~allowedFlags) != 0u || applyFlags == 0u ||
        request->OffsetNanoseconds <
            -TIMECARD_CLOCK_ADJUST_MAX_OFFSET_NS ||
        request->OffsetNanoseconds >
            TIMECARD_CLOCK_ADJUST_MAX_OFFSET_NS ||
        request->DriftPpbQ16 < -maximumDriftQ16 ||
        request->DriftPpbQ16 > maximumDriftQ16 ||
        ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_OFFSET) != 0u &&
         request->OffsetIntervalNanoseconds == 0u) ||
        ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT) != 0u &&
         request->DriftIntervalNanoseconds == 0u) ||
        request->InSyncThresholdNanoseconds > 1000000000u) {
        return STATUS_INVALID_PARAMETER;
    }
    for (index = 0; index < ARRAYSIZE(request->Reserved); ++index) {
        if (request->Reserved[index] != 0u)
            return STATUS_INVALID_PARAMETER;
    }
    status = TimeCardSignedMagnitudeEncode(
        request->OffsetNanoseconds, 0x7fffffffu, &offset);
    if (!NT_SUCCESS(status))
        return status;
    driftMagnitude = request->DriftPpbQ16 < 0 ?
        (unsigned __int64)(-request->DriftPpbQ16) :
        (unsigned __int64)request->DriftPpbQ16;
    drift = (ULONG)(driftMagnitude >> 16);
    driftFraction = (ULONG)driftMagnitude & 0xffffu;
    if (request->DriftPpbQ16 < 0)
        drift |= 0x80000000u;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    version = READ_REGISTER_ULONG((PULONG)&context->Regs->Version);
    if (!TimeCardVersionPresent(version)) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    if ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT) != 0u &&
        driftFraction != 0u &&
        !TimeCardVersionAtLeast(version, 2u, 0u)) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }

    oldControl = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl) &
                 ~OCP_CTRL_TRANSIENT_MASK;
    oldSelect = READ_REGISTER_ULONG((PULONG)&context->Regs->Select);
    oldOffset = READ_REGISTER_ULONG((PULONG)&context->Regs->OffsetNs);
    oldOffsetWindow = READ_REGISTER_ULONG(
        (PULONG)&context->Regs->OffsetWindowNs);
    oldDrift = READ_REGISTER_ULONG((PULONG)&context->Regs->DriftNs);
    oldDriftWindow = READ_REGISTER_ULONG(
        (PULONG)&context->Regs->DriftWindowNs);
    oldDriftFraction = TimeCardVersionAtLeast(version, 2u, 0u) ?
        READ_REGISTER_ULONG((PULONG)&context->Regs->DriftFractions) : 0u;
    oldThreshold = READ_REGISTER_ULONG(
        (PULONG)&context->Regs->InSyncThreshold);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select,
                         OCP_SELECT_CLOCK_REG);
    control = oldControl | OCP_CTRL_ENABLE;
    if ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_OFFSET) != 0u) {
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->OffsetNs, offset);
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->OffsetWindowNs,
                             request->OffsetIntervalNanoseconds);
        control |= OCP_CTRL_ADJUST_OFFSET;
    }
    if ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT) != 0u) {
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->DriftNs, drift);
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->DriftWindowNs,
                             request->DriftIntervalNanoseconds);
        if (TimeCardVersionAtLeast(version, 2u, 0u)) {
            WRITE_REGISTER_ULONG((PULONG)&context->Regs->DriftFractions,
                                 driftFraction);
        }
        control |= OCP_CTRL_ADJUST_DRIFT;
    }
    if ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_THRESHOLD) != 0u) {
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->InSyncThreshold,
                             request->InSyncThresholdNanoseconds);
    }
    if (((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_OFFSET) != 0u &&
         (READ_REGISTER_ULONG((PULONG)&context->Regs->OffsetNs) != offset ||
          READ_REGISTER_ULONG((PULONG)&context->Regs->OffsetWindowNs) !=
              request->OffsetIntervalNanoseconds)) ||
        ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT) != 0u &&
         (READ_REGISTER_ULONG((PULONG)&context->Regs->DriftNs) != drift ||
          READ_REGISTER_ULONG((PULONG)&context->Regs->DriftWindowNs) !=
              request->DriftIntervalNanoseconds ||
          (TimeCardVersionAtLeast(version, 2u, 0u) &&
           (READ_REGISTER_ULONG((PULONG)&context->Regs->DriftFractions) &
            0xffffu) != driftFraction))) ||
        ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_THRESHOLD) != 0u &&
         READ_REGISTER_ULONG((PULONG)&context->Regs->InSyncThreshold) !=
             request->InSyncThresholdNanoseconds)) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto rollback;
    }
    if ((control & (OCP_CTRL_ADJUST_OFFSET |
                    OCP_CTRL_ADJUST_DRIFT)) != 0u) {
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, control);
        /* The adjustment bits are command pulses; retain the original state. */
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, oldControl);
    }
    /* Preserve the requested source rather than the active-source mirror. */
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select, oldSelect & 0xffu);
    status = TimeCardClockAdjustmentQueryLocked(context, response);
    if (!NT_SUCCESS(status))
        goto rollback;
    response->AppliedFlags = applyFlags;
    if (((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_OFFSET) != 0u &&
         (response->OffsetNanoseconds != request->OffsetNanoseconds ||
          response->OffsetIntervalNanoseconds !=
              request->OffsetIntervalNanoseconds)) ||
        ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_DRIFT) != 0u &&
         (response->DriftPpbQ16 != request->DriftPpbQ16 ||
          response->DriftIntervalNanoseconds !=
              request->DriftIntervalNanoseconds)) ||
        ((applyFlags & TIMECARD_CLOCK_ADJUST_FLAG_APPLY_THRESHOLD) != 0u &&
         response->InSyncThresholdNanoseconds !=
             request->InSyncThresholdNanoseconds)) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto rollback;
    }
    goto done;

rollback:
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->OffsetNs, oldOffset);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->OffsetWindowNs,
                         oldOffsetWindow);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->DriftNs, oldDrift);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->DriftWindowNs,
                         oldDriftWindow);
    if (TimeCardVersionAtLeast(version, 2u, 0u))
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->DriftFractions,
                             oldDriftFraction);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->InSyncThreshold,
                         oldThreshold);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Select, oldSelect & 0xffu);
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, oldControl);
done:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

static volatile ULONG *
TimeCardClockRegister(PDEVICE_CONTEXT context, ULONG offset)
{
    return (volatile ULONG *)(context->Bar0Base +
                              context->ClockOffset + offset);
}

static NTSTATUS
TimeCardClockAdvancedQueryLocked(
    PDEVICE_CONTEXT context, BOOLEAN advancedAllowed,
    BOOLEAN servoAllowed, TIMECARD_CLOCK_ADVANCED_CONTROL *result)
{
    ULONG version;

    if (context->Regs == NULL || (!advancedAllowed && !servoAllowed))
        return STATUS_NOT_SUPPORTED;
    version = READ_REGISTER_ULONG((PULONG)&context->Regs->Version);
    if (!TimeCardVersionPresent(version))
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(result, sizeof(*result));
    result->Size = sizeof(*result);
    result->Version = version;
    result->Control = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl);
    result->Status = READ_REGISTER_ULONG((PULONG)&context->Regs->Status);
    result->Flags = TIMECARD_CLOCK_ADVANCED_FLAG_PRESENT;
    if ((result->Status & (1u << 2)) != 0u)
        result->Flags |= TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER_READY;
    if ((result->Status & (1u << 3)) != 0u)
        result->Flags |= TIMECARD_CLOCK_ADVANCED_FLAG_AGING_READY;

    if (advancedAllowed && TimeCardVersionAtLeast(version, 2u, 1u)) {
        result->HoldoverConfiguration = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context,
                                  OCP_ADV_HOLDOVER_CONFIGURATION));
        result->StatusHoldover = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_STATUS_HOLDOVER));
        result->StatusHoldoverFraction = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(
                context, OCP_ADV_STATUS_HOLDOVER_FRACTION));
        result->StatusHoldoverSamples = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(
                context, OCP_ADV_STATUS_HOLDOVER_SAMPLES));
        result->Flags |= TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER;
    }
    if (advancedAllowed && TimeCardVersionAtLeast(version, 2u, 2u)) {
        result->OffsetOutlierFilter = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context,
                                  OCP_ADV_OFFSET_OUTLIER_FILTER));
        result->DriftOutlierFilter = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context,
                                  OCP_ADV_DRIFT_OUTLIER_FILTER));
        result->StatusOffsetOutliers = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(
                context, OCP_ADV_STATUS_OFFSET_OUTLIERS));
        result->StatusDriftOutliers = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(
                context, OCP_ADV_STATUS_DRIFT_OUTLIERS));
        result->Flags |= TIMECARD_CLOCK_ADVANCED_FLAG_OUTLIER_FILTERS;
    }
    if (advancedAllowed && TimeCardVersionAtLeast(version, 2u, 3u)) {
        result->OffsetRateLimiter = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_OFFSET_RATE_LIMITER));
        result->DriftRateLimiterQ16 = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_DRIFT_RATE_LIMITER));
        result->Flags |= TIMECARD_CLOCK_ADVANCED_FLAG_RATE_LIMITERS;
    }
    if (advancedAllowed && TimeCardVersionAtLeast(version, 2u, 4u)) {
        result->DynamicControl = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_DYNAMIC_CONTROL));
        result->Flags |= TIMECARD_CLOCK_ADVANCED_FLAG_DYNAMIC_CONTROL;
    }
    if (advancedAllowed && TimeCardVersionAtLeast(version, 2u, 5u)) {
        result->AgingConfiguration = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_AGING_CONFIGURATION));
        result->StatusAgingLow = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_STATUS_AGING_LOW));
        result->StatusAgingHigh = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_STATUS_AGING_HIGH));
        result->StatusAgingSamples = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_STATUS_AGING_SAMPLES));
        result->Flags |= TIMECARD_CLOCK_ADVANCED_FLAG_AGING;
    }
    if (advancedAllowed && TimeCardVersionAtLeast(version, 2u, 6u))
        result->Flags |= TIMECARD_CLOCK_ADVANCED_FLAG_REVERT;

    if (servoAllowed && TimeCardVersionAtLeast(version, 1u, 6u)) {
        result->ServoOffsetP = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoOffsetP);
        result->ServoOffsetI = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoOffsetI);
        result->ServoDriftP = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoDriftP);
        result->ServoDriftI = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoDriftI);
        result->StatusOffset = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->StatusOffset);
        result->StatusDrift = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->StatusDrift);
        if (TimeCardVersionAtLeast(version, 2u, 0u)) {
            result->StatusOffsetFraction = READ_REGISTER_ULONG(
                (PULONG)&context->Regs->StatusOffsetFraction);
            result->StatusDriftFraction = READ_REGISTER_ULONG(
                (PULONG)&context->Regs->StatusDriftFraction);
        }
        result->Flags |= TIMECARD_CLOCK_ADVANCED_FLAG_SERVO_FACTORS |
                         TIMECARD_CLOCK_ADVANCED_FLAG_SERVO_LOG;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardClockAdvancedQuery(
    PDEVICE_CONTEXT context, TIMECARD_CLOCK_ADVANCED_CONTROL *control)
{
    BOOLEAN advancedAllowed;
    BOOLEAN servoAllowed;
    NTSTATUS status;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (!TimeCardStandardFpga(context))
        return STATUS_NOT_SUPPORTED;
    advancedAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_CLOCK_ADVANCED);
    servoAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_CLOCK_SERVO_LOG);
    if (!advancedAllowed && !servoAllowed)
        return STATUS_NOT_SUPPORTED;

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardClockAdvancedQueryLocked(
        context, advancedAllowed, servoAllowed, control);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

static BOOLEAN
TimeCardClockLimiterValid(ULONG value, BOOLEAN offsetLimiter)
{
    ULONG magnitude = value & TIMECARD_CLOCK_LIMITER_VALUE_MASK;

    if (offsetLimiter && magnitude >
        (ULONG)TIMECARD_CLOCK_ADJUST_MAX_OFFSET_NS) {
        return FALSE;
    }
    return (value & TIMECARD_CLOCK_LIMITER_ENABLE) == 0u ||
           magnitude != 0u;
}

NTSTATUS
TimeCardClockAdvancedSet(
    PDEVICE_CONTEXT context,
    const TIMECARD_CLOCK_ADVANCED_CONTROL *request,
    TIMECARD_CLOCK_ADVANCED_CONTROL *response)
{
    BOOLEAN advancedAllowed;
    BOOLEAN servoAllowed;
    BOOLEAN rollback = FALSE;
    ULONG allowedFlags;
    ULONG apply;
    ULONG index;
    ULONG version;
    ULONG oldControl;
    ULONG targetControl;
    ULONG oldOffsetLimiter = 0u;
    ULONG oldDriftLimiter = 0u;
    ULONG oldAging = 0u;
    ULONG oldHoldover = 0u;
    ULONG oldOffsetOutlier = 0u;
    ULONG oldDriftOutlier = 0u;
    ULONG oldServoOffsetP = 0u;
    ULONG oldServoOffsetI = 0u;
    ULONG oldServoDriftP = 0u;
    ULONG oldServoDriftI = 0u;
    NTSTATUS status;

    if (!context->HardwareReady || context->Regs == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (!TimeCardStandardFpga(context))
        return STATUS_NOT_SUPPORTED;
    allowedFlags = TIMECARD_CLOCK_ADVANCED_FLAG_PRESENT |
        TIMECARD_CLOCK_ADVANCED_FLAG_RATE_LIMITERS |
        TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER |
        TIMECARD_CLOCK_ADVANCED_FLAG_OUTLIER_FILTERS |
        TIMECARD_CLOCK_ADVANCED_FLAG_SERVO_FACTORS |
        TIMECARD_CLOCK_ADVANCED_FLAG_SERVO_LOG |
        TIMECARD_CLOCK_ADVANCED_FLAG_AGING |
        TIMECARD_CLOCK_ADVANCED_FLAG_DYNAMIC_CONTROL |
        TIMECARD_CLOCK_ADVANCED_FLAG_REVERT |
        TIMECARD_CLOCK_ADVANCED_FLAG_HOLDOVER_READY |
        TIMECARD_CLOCK_ADVANCED_FLAG_AGING_READY;
    apply = request->ApplyFlags;
    if (request->Size < sizeof(*request) || apply == 0u ||
        (request->Flags & ~allowedFlags) != 0u ||
        (apply & ~TIMECARD_CLOCK_ADVANCED_APPLY_ALL) != 0u ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_CONTROL) != 0u &&
         (request->Control & OCP_CTRL_HOLDOVER_OFFSET) != 0u) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_RATE_LIMITERS) != 0u &&
         (!TimeCardClockLimiterValid(request->OffsetRateLimiter, TRUE) ||
          !TimeCardClockLimiterValid(request->DriftRateLimiterQ16,
                                     FALSE))) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_AGING) != 0u &&
         ((request->AgingConfiguration & 0x00fe0000u) != 0u ||
          (request->AgingConfiguration >> 24) == 0u)) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER) != 0u &&
         (request->HoldoverConfiguration & 0x00fe0000u) != 0u) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_OUTLIER_FILTERS) != 0u &&
         (!TimeCardClockLimiterValid(request->OffsetOutlierFilter, TRUE) ||
          !TimeCardClockLimiterValid(request->DriftOutlierFilter, TRUE))) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS) != 0u &&
         (request->ServoOffsetP > 0xffffu ||
          request->ServoOffsetI > 0xffffu ||
          request->ServoDriftP > 0xffffu ||
          request->ServoDriftI > 0xffffu))) {
        return STATUS_INVALID_PARAMETER;
    }
    for (index = 0; index < ARRAYSIZE(request->Reserved); ++index) {
        if (request->Reserved[index] != 0u)
            return STATUS_INVALID_PARAMETER;
    }

    advancedAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_CLOCK_ADVANCED);
    servoAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_CLOCK_SERVO_LOG);
    if (((apply & ~TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS) != 0u &&
         !advancedAllowed) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS) != 0u &&
         !servoAllowed)) {
        return STATUS_NOT_SUPPORTED;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    version = READ_REGISTER_ULONG((PULONG)&context->Regs->Version);
    if (!TimeCardVersionPresent(version)) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    if (((apply & TIMECARD_CLOCK_ADVANCED_APPLY_RATE_LIMITERS) != 0u &&
         !TimeCardVersionAtLeast(version, 2u, 3u)) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER) != 0u &&
         !TimeCardVersionAtLeast(version, 2u, 1u)) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_OUTLIER_FILTERS) != 0u &&
         !TimeCardVersionAtLeast(version, 2u, 2u)) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS) != 0u &&
         !TimeCardVersionAtLeast(version, 1u, 6u)) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_AGING) != 0u &&
         !TimeCardVersionAtLeast(version, 2u, 5u)) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_CONTROL) != 0u &&
         (((request->Control & OCP_CTRL_HOLDOVER) != 0u &&
           !TimeCardVersionAtLeast(version, 2u, 1u)) ||
          ((request->Control & OCP_CTRL_AGING) != 0u &&
           !TimeCardVersionAtLeast(version, 2u, 5u)) ||
          ((request->Control & OCP_CTRL_REVERT) != 0u &&
           !TimeCardVersionAtLeast(version, 2u, 6u))))) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }

    oldControl = READ_REGISTER_ULONG((PULONG)&context->Regs->Ctrl) &
                 ~OCP_CTRL_TRANSIENT_MASK;
    targetControl = oldControl;
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_CONTROL) != 0u) {
        targetControl = (oldControl &
            ~(TIMECARD_CLOCK_ADVANCED_CONTROL_MASK |
              OCP_CTRL_HOLDOVER_OFFSET)) |
            (request->Control & TIMECARD_CLOCK_ADVANCED_CONTROL_MASK);
    }
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_AGING) != 0u ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_CONTROL) != 0u &&
         (targetControl & OCP_CTRL_AGING) != 0u)) {
        oldAging = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_AGING_CONFIGURATION));
        if ((targetControl & OCP_CTRL_AGING) != 0u) {
            ULONG targetAging =
                (apply & TIMECARD_CLOCK_ADVANCED_APPLY_AGING) != 0u ?
                request->AgingConfiguration : oldAging;

            if ((targetAging >> 24) == 0u ||
                (targetAging & 0x1ffffu) == 0u) {
                status = STATUS_INVALID_PARAMETER;
                goto done;
            }
        }
    }
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER) != 0u ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_CONTROL) != 0u &&
         (targetControl & OCP_CTRL_HOLDOVER) != 0u)) {
        oldHoldover = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context,
                                  OCP_ADV_HOLDOVER_CONFIGURATION));
        if ((targetControl & OCP_CTRL_HOLDOVER) != 0u) {
            ULONG targetHoldover =
                (apply & TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER) != 0u ?
                request->HoldoverConfiguration : oldHoldover;

            if ((targetHoldover & 0x1ffffu) == 0u) {
                status = STATUS_INVALID_PARAMETER;
                goto done;
            }
        }
    }
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_RATE_LIMITERS) != 0u) {
        oldOffsetLimiter = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_OFFSET_RATE_LIMITER));
        oldDriftLimiter = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context, OCP_ADV_DRIFT_RATE_LIMITER));
    }
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_OUTLIER_FILTERS) != 0u) {
        oldOffsetOutlier = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context,
                                  OCP_ADV_OFFSET_OUTLIER_FILTER));
        oldDriftOutlier = READ_REGISTER_ULONG((PULONG)
            TimeCardClockRegister(context,
                                  OCP_ADV_DRIFT_OUTLIER_FILTER));
    }
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS) != 0u) {
        oldServoOffsetP = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoOffsetP);
        oldServoOffsetI = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoOffsetI);
        oldServoDriftP = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoDriftP);
        oldServoDriftI = READ_REGISTER_ULONG(
            (PULONG)&context->Regs->ServoDriftI);
    }

    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_RATE_LIMITERS) != 0u) {
        WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
            context, OCP_ADV_OFFSET_RATE_LIMITER),
            request->OffsetRateLimiter);
        WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
            context, OCP_ADV_DRIFT_RATE_LIMITER),
            request->DriftRateLimiterQ16);
    }
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER) != 0u)
        WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
            context, OCP_ADV_HOLDOVER_CONFIGURATION),
            request->HoldoverConfiguration);
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_OUTLIER_FILTERS) != 0u) {
        WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
            context, OCP_ADV_OFFSET_OUTLIER_FILTER),
            request->OffsetOutlierFilter);
        WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
            context, OCP_ADV_DRIFT_OUTLIER_FILTER),
            request->DriftOutlierFilter);
    }
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_AGING) != 0u)
        WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
            context, OCP_ADV_AGING_CONFIGURATION),
            request->AgingConfiguration);
    if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS) != 0u) {
        if (!advancedAllowed ||
            !TimeCardVersionAtLeast(version, 2u, 4u)) {
            WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl,
                                 oldControl & ~OCP_CTRL_ENABLE);
        }
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->ServoOffsetP,
                             request->ServoOffsetP);
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->ServoOffsetI,
                             request->ServoOffsetI);
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->ServoDriftP,
                             request->ServoDriftP);
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->ServoDriftI,
                             request->ServoDriftI);
        if (advancedAllowed && TimeCardVersionAtLeast(version, 2u, 4u)) {
            WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
                context, OCP_ADV_DYNAMIC_CONTROL), 1u);
        } else {
            WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl,
                (oldControl & ~OCP_CTRL_ENABLE) | OCP_CTRL_SERVO_VALID);
        }
    }
    WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, targetControl);

    status = TimeCardClockAdvancedQueryLocked(
        context, advancedAllowed, servoAllowed, response);
    if (!NT_SUCCESS(status)) {
        rollback = TRUE;
        goto restore;
    }
    response->ApplyFlags = apply;
    if (((apply & TIMECARD_CLOCK_ADVANCED_APPLY_RATE_LIMITERS) != 0u &&
         (response->OffsetRateLimiter != request->OffsetRateLimiter ||
          response->DriftRateLimiterQ16 !=
              request->DriftRateLimiterQ16)) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER) != 0u &&
         response->HoldoverConfiguration !=
             request->HoldoverConfiguration) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_OUTLIER_FILTERS) != 0u &&
         (response->OffsetOutlierFilter != request->OffsetOutlierFilter ||
          response->DriftOutlierFilter != request->DriftOutlierFilter)) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_AGING) != 0u &&
         response->AgingConfiguration != request->AgingConfiguration) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS) != 0u &&
         (response->ServoOffsetP != request->ServoOffsetP ||
          response->ServoOffsetI != request->ServoOffsetI ||
          response->ServoDriftP != request->ServoDriftP ||
          response->ServoDriftI != request->ServoDriftI)) ||
        ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_CONTROL) != 0u &&
         (response->Control & TIMECARD_CLOCK_ADVANCED_CONTROL_MASK) !=
         (targetControl & TIMECARD_CLOCK_ADVANCED_CONTROL_MASK))) {
        status = STATUS_DEVICE_DATA_ERROR;
        rollback = TRUE;
    }

restore:
    if (rollback) {
        if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_RATE_LIMITERS) != 0u) {
            WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
                context, OCP_ADV_OFFSET_RATE_LIMITER), oldOffsetLimiter);
            WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
                context, OCP_ADV_DRIFT_RATE_LIMITER), oldDriftLimiter);
        }
        if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_HOLDOVER) != 0u)
            WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
                context, OCP_ADV_HOLDOVER_CONFIGURATION), oldHoldover);
        if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_OUTLIER_FILTERS) != 0u) {
            WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
                context, OCP_ADV_OFFSET_OUTLIER_FILTER), oldOffsetOutlier);
            WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
                context, OCP_ADV_DRIFT_OUTLIER_FILTER), oldDriftOutlier);
        }
        if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_AGING) != 0u)
            WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
                context, OCP_ADV_AGING_CONFIGURATION), oldAging);
        if ((apply & TIMECARD_CLOCK_ADVANCED_APPLY_SERVO_FACTORS) != 0u) {
            WRITE_REGISTER_ULONG((PULONG)&context->Regs->ServoOffsetP,
                                 oldServoOffsetP);
            WRITE_REGISTER_ULONG((PULONG)&context->Regs->ServoOffsetI,
                                 oldServoOffsetI);
            WRITE_REGISTER_ULONG((PULONG)&context->Regs->ServoDriftP,
                                 oldServoDriftP);
            WRITE_REGISTER_ULONG((PULONG)&context->Regs->ServoDriftI,
                                 oldServoDriftI);
            if (advancedAllowed &&
                TimeCardVersionAtLeast(version, 2u, 4u)) {
                WRITE_REGISTER_ULONG((PULONG)TimeCardClockRegister(
                    context, OCP_ADV_DYNAMIC_CONTROL), 1u);
            } else {
                WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl,
                    (oldControl & ~OCP_CTRL_ENABLE) |
                    OCP_CTRL_SERVO_VALID);
            }
        }
        WRITE_REGISTER_ULONG((PULONG)&context->Regs->Ctrl, oldControl);
    }
done:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

static volatile TIMECARD_PPS_REG *
TimeCardPpsRegister(PDEVICE_CONTEXT context, ULONG core)
{
    if (!TimeCardStandardFpga(context))
        return NULL;
    if (core == TIMECARD_PPS_CORE_MASTER)
        return context->PpsMaster;
    if (core == TIMECARD_PPS_CORE_SLAVE)
        return context->PpsSlave;
    return NULL;
}

static NTSTATUS
TimeCardPpsQueryLocked(PDEVICE_CONTEXT context, ULONG core,
                       TIMECARD_PPS_CONTROL *control)
{
    volatile TIMECARD_PPS_REG *reg = TimeCardPpsRegister(context, core);
    ULONG version;
    ULONG magnitudeMask;
    ULONG errorMask;

    if (reg == NULL)
        return STATUS_NOT_SUPPORTED;
    version = READ_REGISTER_ULONG((PULONG)&reg->Version);
    if (!TimeCardVersionPresent(version))
        return STATUS_NOT_SUPPORTED;
    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Core = core;
    control->Version = version;
    control->Control = READ_REGISTER_ULONG((PULONG)&reg->Control);
    control->PulseWidthMilliseconds =
        READ_REGISTER_ULONG((PULONG)&reg->PulseWidth) & 0x3ffu;
    control->Flags = TIMECARD_PPS_FLAG_PRESENT;
    if ((control->Control & TIMECARD_CORE_ENABLE) != 0)
        control->Flags |= TIMECARD_PPS_FLAG_ENABLED;

    magnitudeMask = TimeCardVersionAtLeast(version, 1u, 6u) ?
        TIMECARD_PPS_DELAY_MAGNITUDE :
        TIMECARD_PPS_DELAY_LEGACY_MAGNITUDE;
    if (core == TIMECARD_PPS_CORE_MASTER) {
        control->Flags |= TIMECARD_PPS_FLAG_PULSE_WIDTH_WRITABLE;
        if (TimeCardVersionAtLeast(version, 1u, 1u)) {
            control->Polarity = READ_REGISTER_ULONG(
                (PULONG)&reg->Polarity) & 1u;
        }
        if (TimeCardVersionAtLeast(version, 1u, 2u)) {
            control->Status = READ_REGISTER_ULONG((PULONG)&reg->Status);
            if ((control->Status & TIMECARD_PPS_MASTER_ERROR_MASK) != 0)
                control->Flags |= TIMECARD_PPS_FLAG_ERROR;
        }
        if (TimeCardVersionAtLeast(version, 1u, 4u)) {
            control->CableDelayNanoseconds = TimeCardSignedMagnitudeDecode(
                READ_REGISTER_ULONG((PULONG)&reg->CableDelay),
                magnitudeMask);
        }
    } else {
        if (TimeCardVersionAtLeast(version, 1u, 2u)) {
            control->Polarity = READ_REGISTER_ULONG(
                (PULONG)&reg->Polarity) & 1u;
        }
        control->CableDelayNanoseconds = TimeCardSignedMagnitudeDecode(
            READ_REGISTER_ULONG((PULONG)&reg->CableDelay), magnitudeMask);
        if (TimeCardVersionAtLeast(version, 1u, 3u)) {
            control->Status = READ_REGISTER_ULONG((PULONG)&reg->Status);
            errorMask = TIMECARD_PPS_SLAVE_ERROR_MASK;
            if ((control->Status & errorMask) != 0)
                control->Flags |= TIMECARD_PPS_FLAG_ERROR;
        }
        if ((control->Status & 1u) != 0)
            control->Flags |= TIMECARD_PPS_FLAG_FILTER_ERROR;
        if ((control->Status & 2u) != 0)
            control->Flags |= TIMECARD_PPS_FLAG_SUPERVISION_ERROR;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardPpsQuery(PDEVICE_CONTEXT context, ULONG core,
                 TIMECARD_PPS_CONTROL *control)
{
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    if (core != TIMECARD_PPS_CORE_MASTER &&
        core != TIMECARD_PPS_CORE_SLAVE)
        return STATUS_INVALID_PARAMETER;
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardPpsQueryLocked(context, core, control);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardPpsSet(PDEVICE_CONTEXT context,
               const TIMECARD_PPS_CONTROL *request,
               TIMECARD_PPS_CONTROL *response)
{
    volatile TIMECARD_PPS_REG *reg;
    BOOLEAN configured = FALSE;
    ULONG encodedDelay;
    ULONG control;
    ULONG errorMask;
    ULONG allowedFlags;
    ULONG value;
    ULONG version;
    ULONG magnitudeMask;
    ULONG oldControl;
    ULONG oldDelay;
    ULONG oldPolarity;
    ULONG oldPulseWidth;
    ULONG index;
    BOOLEAN statusSupported;
    BOOLEAN enabled;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    allowedFlags = TIMECARD_PPS_FLAG_PRESENT |
        TIMECARD_PPS_FLAG_ENABLED | TIMECARD_PPS_FLAG_ERROR |
        TIMECARD_PPS_FLAG_FILTER_ERROR |
        TIMECARD_PPS_FLAG_SUPERVISION_ERROR |
        TIMECARD_PPS_FLAG_PULSE_WIDTH_WRITABLE |
        TIMECARD_PPS_FLAG_CLEAR_ERRORS;
    if (request->Size < sizeof(*request) ||
        (request->Core != TIMECARD_PPS_CORE_MASTER &&
         request->Core != TIMECARD_PPS_CORE_SLAVE) ||
        request->Polarity > 1u ||
        (request->Flags & ~allowedFlags) != 0 ||
        (request->Core == TIMECARD_PPS_CORE_MASTER &&
         (request->PulseWidthMilliseconds == 0u ||
          request->PulseWidthMilliseconds > 999u)))
        return STATUS_INVALID_PARAMETER;
    for (index = 0; index < ARRAYSIZE(request->Reserved); ++index) {
        if (request->Reserved[index] != 0u)
            return STATUS_INVALID_PARAMETER;
    }

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    reg = TimeCardPpsRegister(context, request->Core);
    if (reg == NULL) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    version = READ_REGISTER_ULONG((PULONG)&reg->Version);
    if (!TimeCardVersionPresent(version) ||
        (request->Core == TIMECARD_PPS_CORE_MASTER &&
         !TimeCardVersionAtLeast(version, 1u, 4u)) ||
        (request->Core == TIMECARD_PPS_CORE_SLAVE &&
         !TimeCardVersionAtLeast(version, 1u, 2u))) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    statusSupported = request->Core == TIMECARD_PPS_CORE_MASTER ?
        TimeCardVersionAtLeast(version, 1u, 2u) :
        TimeCardVersionAtLeast(version, 1u, 3u);
    if ((request->Flags & TIMECARD_PPS_FLAG_CLEAR_ERRORS) != 0 &&
        !statusSupported) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    magnitudeMask = TimeCardVersionAtLeast(version, 1u, 6u) ?
        TIMECARD_PPS_DELAY_MAGNITUDE :
        TIMECARD_PPS_DELAY_LEGACY_MAGNITUDE;
    status = TimeCardSignedMagnitudeEncode(
        request->CableDelayNanoseconds, magnitudeMask, &encodedDelay);
    if (!NT_SUCCESS(status))
        goto done;

    control = READ_REGISTER_ULONG((PULONG)&reg->Control);
    oldControl = control;
    oldPolarity = READ_REGISTER_ULONG((PULONG)&reg->Polarity);
    oldPulseWidth = request->Core == TIMECARD_PPS_CORE_MASTER ?
        READ_REGISTER_ULONG((PULONG)&reg->PulseWidth) : 0u;
    oldDelay = READ_REGISTER_ULONG((PULONG)&reg->CableDelay);
    configured = TRUE;
    WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                         control & ~TIMECARD_CORE_ENABLE);
    value = oldPolarity;
    value = (value & ~1u) | request->Polarity;
    WRITE_REGISTER_ULONG((PULONG)&reg->Polarity, value);
    if (request->Core == TIMECARD_PPS_CORE_MASTER) {
        value = oldPulseWidth;
        value = (value & ~0x3ffu) | request->PulseWidthMilliseconds;
        WRITE_REGISTER_ULONG((PULONG)&reg->PulseWidth, value);
    }
    value = oldDelay;
    value = (value & ~(TIMECARD_PPS_DELAY_SIGN |
                       magnitudeMask)) | encodedDelay;
    WRITE_REGISTER_ULONG((PULONG)&reg->CableDelay, value);
    errorMask = request->Core == TIMECARD_PPS_CORE_MASTER ?
        TIMECARD_PPS_MASTER_ERROR_MASK : TIMECARD_PPS_SLAVE_ERROR_MASK;
    control &= ~TIMECARD_CORE_ENABLE;
    if ((request->Flags & TIMECARD_PPS_FLAG_ENABLED) != 0)
        control |= TIMECARD_CORE_ENABLE;
    WRITE_REGISTER_ULONG((PULONG)&reg->Control, control);
    status = TimeCardPpsQueryLocked(context, request->Core, response);
    if (!NT_SUCCESS(status))
        goto rollback;
    enabled = (request->Flags & TIMECARD_PPS_FLAG_ENABLED) != 0;
    if (((response->Flags & TIMECARD_PPS_FLAG_ENABLED) != 0) != enabled ||
        response->Polarity != request->Polarity ||
        response->CableDelayNanoseconds !=
            request->CableDelayNanoseconds ||
        (request->Core == TIMECARD_PPS_CORE_MASTER &&
         response->PulseWidthMilliseconds !=
            request->PulseWidthMilliseconds)) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto rollback;
    }
    if ((request->Flags & TIMECARD_PPS_FLAG_CLEAR_ERRORS) != 0) {
        WRITE_REGISTER_ULONG((PULONG)&reg->Status, errorMask);
        response->Status = READ_REGISTER_ULONG((PULONG)&reg->Status);
        response->Flags &= ~(TIMECARD_PPS_FLAG_ERROR |
                             TIMECARD_PPS_FLAG_FILTER_ERROR |
                             TIMECARD_PPS_FLAG_SUPERVISION_ERROR);
        if ((response->Status & errorMask) != 0u)
            response->Flags |= TIMECARD_PPS_FLAG_ERROR;
        if ((response->Status & 1u) != 0u)
            response->Flags |= TIMECARD_PPS_FLAG_FILTER_ERROR;
        if ((response->Status & 2u) != 0u)
            response->Flags |= TIMECARD_PPS_FLAG_SUPERVISION_ERROR;
    }
    goto done;

rollback:
    if (configured) {
        WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                             oldControl & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Polarity, oldPolarity);
        if (request->Core == TIMECARD_PPS_CORE_MASTER)
            WRITE_REGISTER_ULONG((PULONG)&reg->PulseWidth, oldPulseWidth);
        WRITE_REGISTER_ULONG((PULONG)&reg->CableDelay, oldDelay);
        WRITE_REGISTER_ULONG((PULONG)&reg->Control, oldControl);
    }
done:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

static NTSTATUS
TimeCardTimecodeQueryLocked(PDEVICE_CONTEXT context, ULONG format,
                            ULONG role,
                            BOOLEAN masterAmAllowed,
                            BOOLEAN slaveAmAllowed,
                            BOOLEAN slaveYearAllowed,
                            TIMECARD_TIMECODE_CONTROL *result)
{
    ULONG control = 0;
    ULONG status = 0;
    ULONG version = 0;
    ULONG correction = 0;

    if (!TimeCardStandardFpga(context))
        return STATUS_NOT_SUPPORTED;
    RtlZeroMemory(result, sizeof(*result));
    result->Size = sizeof(*result);
    result->Format = format;
    result->Role = role;
    if (format == TIMECARD_TIMECODE_FORMAT_IRIG &&
        role == TIMECARD_TIMECODE_ROLE_MASTER) {
        volatile TIMECARD_IRIG_MASTER_REG *reg = context->IrigMaster;

        if (reg == NULL)
            return STATUS_NOT_SUPPORTED;
        version = READ_REGISTER_ULONG((PULONG)&reg->Version);
        if (!TimeCardVersionPresent(version))
            return STATUS_NOT_SUPPORTED;
        control = READ_REGISTER_ULONG((PULONG)&reg->Control);
        correction = READ_REGISTER_ULONG((PULONG)&reg->Correction);
        if (TimeCardVersionAtLeast(version, 1u, 1u))
            status = READ_REGISTER_ULONG((PULONG)&reg->Status);
        if (TimeCardVersionAtLeast(version, 1u, 2u)) {
            result->Mode = (control >> 24) & 3u;
            result->Code = (control >> 16) & 7u;
            result->ControlBits = READ_REGISTER_ULONG(
                (PULONG)&reg->ControlBits) & 0x07ffffffu;
            result->Flags |=
                TIMECARD_TIMECODE_FLAG_CONTROL_BITS_WRITABLE;
        } else {
            result->Mode = TIMECARD_IRIG_MODE_B;
        }
        if (masterAmAllowed &&
            TimeCardVersionAtLeast(version, 1u, 5u)) {
            result->AmplitudeModulation =
                (control & TIMECARD_IRIG_AM_CONTROL) != 0u ? 1u : 0u;
            result->Flags |= TIMECARD_TIMECODE_FLAG_AM_WRITABLE;
        }
    } else if (format == TIMECARD_TIMECODE_FORMAT_IRIG &&
               role == TIMECARD_TIMECODE_ROLE_SLAVE) {
        volatile TIMECARD_IRIG_SLAVE_REG *reg = context->IrigSlave;

        if (reg == NULL)
            return STATUS_NOT_SUPPORTED;
        version = READ_REGISTER_ULONG((PULONG)&reg->Version);
        if (!TimeCardVersionPresent(version))
            return STATUS_NOT_SUPPORTED;
        control = READ_REGISTER_ULONG((PULONG)&reg->Control);
        correction = READ_REGISTER_ULONG((PULONG)&reg->Correction);
        result->DelayNanoseconds = (LONG)(READ_REGISTER_ULONG(
            (PULONG)&reg->CableDelay) & TIMECARD_IRIG_DELAY_MAXIMUM);
        result->Flags |= TIMECARD_TIMECODE_FLAG_DELAY_WRITABLE;
        if (TimeCardVersionAtLeast(version, 1u, 1u))
            status = READ_REGISTER_ULONG((PULONG)&reg->Status);
        if (TimeCardVersionAtLeast(version, 1u, 2u)) {
            result->ControlBits = READ_REGISTER_ULONG(
                (PULONG)&reg->ControlBits) & 0x07ffffffu;
        }
        if (TimeCardVersionAtLeast(version, 1u, 3u))
            result->Mode = (control >> 24) & 3u;
        else
            result->Mode = TIMECARD_IRIG_MODE_B;
        if (slaveYearAllowed &&
            TimeCardVersionAtLeast(version, 1u, 5u)) {
            result->Code = (control >> 16) & 7u;
            result->ManualYear = READ_REGISTER_ULONG(
                (PULONG)&reg->ManualYear) & 0xfffu;
            result->Flags |= TIMECARD_TIMECODE_FLAG_YEAR_WRITABLE;
        }
        if (slaveAmAllowed &&
            TimeCardVersionAtLeast(version, 1u, 6u)) {
            result->AmplitudeModulation =
                (control & TIMECARD_IRIG_AM_CONTROL) != 0u ? 1u : 0u;
            result->Flags |= TIMECARD_TIMECODE_FLAG_AM_WRITABLE;
        }
    } else if (format == TIMECARD_TIMECODE_FORMAT_DCF &&
               role == TIMECARD_TIMECODE_ROLE_MASTER) {
        volatile TIMECARD_DCF_MASTER_REG *reg = context->DcfMaster;

        if (reg == NULL)
            return STATUS_NOT_SUPPORTED;
        version = READ_REGISTER_ULONG((PULONG)&reg->Version);
        if (!TimeCardVersionPresent(version))
            return STATUS_NOT_SUPPORTED;
        control = READ_REGISTER_ULONG((PULONG)&reg->Control);
        status = READ_REGISTER_ULONG((PULONG)&reg->Status);
        correction = READ_REGISTER_ULONG((PULONG)&reg->Correction);
    } else if (format == TIMECARD_TIMECODE_FORMAT_DCF &&
               role == TIMECARD_TIMECODE_ROLE_SLAVE) {
        volatile TIMECARD_DCF_SLAVE_REG *reg = context->DcfSlave;

        if (reg == NULL)
            return STATUS_NOT_SUPPORTED;
        version = READ_REGISTER_ULONG((PULONG)&reg->Version);
        if (!TimeCardVersionPresent(version))
            return STATUS_NOT_SUPPORTED;
        control = READ_REGISTER_ULONG((PULONG)&reg->Control);
        status = READ_REGISTER_ULONG((PULONG)&reg->Status);
        correction = READ_REGISTER_ULONG((PULONG)&reg->Correction);
        result->DelayNanoseconds = (LONG)(READ_REGISTER_ULONG(
            (PULONG)&reg->AirDelay) & TIMECARD_DCF_DELAY_MAXIMUM);
        result->BitPosition = READ_REGISTER_ULONG(
            (PULONG)&reg->BitPosition) & 0x3fu;
        result->Flags |= TIMECARD_TIMECODE_FLAG_DELAY_WRITABLE;
    } else {
        return STATUS_INVALID_PARAMETER;
    }
    result->Control = control;
    result->Status = status;
    result->Version = version;
    result->CorrectionSeconds = TimeCardSignedMagnitudeDecode(
        correction, TIMECARD_CORRECTION_MAGNITUDE);
    result->Flags |= TIMECARD_TIMECODE_FLAG_PRESENT;
    if ((control & TIMECARD_CORE_ENABLE) != 0)
        result->Flags |= TIMECARD_TIMECODE_FLAG_ENABLED;
    if ((status & TIMECARD_TIMECODE_ERROR_MASK) != 0)
        result->Flags |= TIMECARD_TIMECODE_FLAG_ERROR;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardTimecodeQuery(PDEVICE_CONTEXT context, ULONG format, ULONG role,
                      TIMECARD_TIMECODE_CONTROL *control)
{
    BOOLEAN masterAmAllowed;
    BOOLEAN slaveAmAllowed;
    BOOLEAN slaveYearAllowed;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    masterAmAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_IRIG_MASTER_AM);
    slaveAmAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_AM);
    slaveYearAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_YEAR);
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardTimecodeQueryLocked(
        context, format, role, masterAmAllowed, slaveAmAllowed,
        slaveYearAllowed, control);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardTimecodeSet(PDEVICE_CONTEXT context,
                    const TIMECARD_TIMECODE_CONTROL *request,
                    TIMECARD_TIMECODE_CONTROL *response)
{
    BOOLEAN configured = FALSE;
    BOOLEAN enabled;
    BOOLEAN masterAmAllowed;
    BOOLEAN slaveAmAllowed;
    BOOLEAN slaveYearAllowed;
    ULONG allowedFlags;
    ULONG correction;
    ULONG control;
    ULONG controlBits;
    ULONG delay;
    ULONG index;
    ULONG oldControl = 0u;
    ULONG oldControlBits = 0u;
    ULONG oldCorrection = 0u;
    ULONG oldDelay = 0u;
    ULONG oldYear = 0u;
    BOOLEAN oldYearCaptured = FALSE;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    allowedFlags = TIMECARD_TIMECODE_FLAG_PRESENT |
        TIMECARD_TIMECODE_FLAG_ENABLED | TIMECARD_TIMECODE_FLAG_ERROR |
        TIMECARD_TIMECODE_FLAG_DELAY_WRITABLE |
        TIMECARD_TIMECODE_FLAG_CONTROL_BITS_WRITABLE |
        TIMECARD_TIMECODE_FLAG_AM_WRITABLE |
        TIMECARD_TIMECODE_FLAG_YEAR_WRITABLE |
        TIMECARD_TIMECODE_FLAG_CLEAR_ERRORS;
    if (request->Size < sizeof(*request) ||
        (request->Format != TIMECARD_TIMECODE_FORMAT_IRIG &&
         request->Format != TIMECARD_TIMECODE_FORMAT_DCF) ||
        (request->Role != TIMECARD_TIMECODE_ROLE_MASTER &&
         request->Role != TIMECARD_TIMECODE_ROLE_SLAVE) ||
        (request->Flags & ~allowedFlags) != 0 ||
        request->ControlBits > 0x07ffffffu ||
        request->AmplitudeModulation > 1u ||
        (request->ManualYear != 0u &&
         (request->ManualYear < 1970u || request->ManualYear > 2069u)))
        return STATUS_INVALID_PARAMETER;
    if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
        (request->Mode > TIMECARD_IRIG_MODE_G || request->Code > 7u))
        return STATUS_INVALID_PARAMETER;
    if (request->Format == TIMECARD_TIMECODE_FORMAT_DCF &&
        (request->Mode != 0u || request->Code != 0u ||
         request->ControlBits != 0u ||
         request->AmplitudeModulation != 0u ||
         request->ManualYear != 0u))
        return STATUS_INVALID_PARAMETER;
    if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
        request->Role == TIMECARD_TIMECODE_ROLE_MASTER &&
        request->ManualYear != 0u)
        return STATUS_INVALID_PARAMETER;
    if (request->Role == TIMECARD_TIMECODE_ROLE_MASTER &&
        request->DelayNanoseconds != 0)
        return STATUS_INVALID_PARAMETER;
    if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
        request->Role == TIMECARD_TIMECODE_ROLE_SLAVE &&
        (request->DelayNanoseconds < 0 ||
         request->DelayNanoseconds > (LONG)TIMECARD_IRIG_DELAY_MAXIMUM))
        return STATUS_INVALID_PARAMETER;
    if (request->Format == TIMECARD_TIMECODE_FORMAT_DCF &&
        request->Role == TIMECARD_TIMECODE_ROLE_SLAVE &&
        (request->DelayNanoseconds < 0 ||
         request->DelayNanoseconds > (LONG)TIMECARD_DCF_DELAY_MAXIMUM))
        return STATUS_INVALID_PARAMETER;
    status = TimeCardSignedMagnitudeEncode(
        request->CorrectionSeconds, TIMECARD_CORRECTION_MAGNITUDE,
        &correction);
    if (!NT_SUCCESS(status))
        return status;
    for (index = 0; index < ARRAYSIZE(request->Reserved); ++index) {
        if (request->Reserved[index] != 0u)
            return STATUS_INVALID_PARAMETER;
    }

    masterAmAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_IRIG_MASTER_AM);
    slaveAmAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_AM);
    slaveYearAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_IRIG_SLAVE_YEAR);

    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardTimecodeQueryLocked(
        context, request->Format, request->Role, masterAmAllowed,
        slaveAmAllowed, slaveYearAllowed, response);
    if (!NT_SUCCESS(status))
        goto done;
    if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
        (!TimeCardVersionAtLeast(response->Version, 1u, 2u) ||
         (request->Mode == TIMECARD_IRIG_MODE_G &&
          !TimeCardVersionAtLeast(response->Version, 1u, 3u)) ||
         (request->Role == TIMECARD_TIMECODE_ROLE_SLAVE &&
          request->Mode != TIMECARD_IRIG_MODE_B &&
          !TimeCardVersionAtLeast(response->Version, 1u, 3u)))) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    if ((request->Flags & TIMECARD_TIMECODE_FLAG_CLEAR_ERRORS) != 0u &&
        !TimeCardVersionAtLeast(response->Version, 1u, 1u)) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
        request->Role == TIMECARD_TIMECODE_ROLE_MASTER &&
        request->AmplitudeModulation != response->AmplitudeModulation &&
        (response->Flags & TIMECARD_TIMECODE_FLAG_AM_WRITABLE) == 0u) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
        request->Role == TIMECARD_TIMECODE_ROLE_SLAVE &&
        ((request->AmplitudeModulation != response->AmplitudeModulation &&
          (response->Flags & TIMECARD_TIMECODE_FLAG_AM_WRITABLE) == 0u) ||
         ((request->Code != response->Code || request->ManualYear != 0u) &&
          (response->Flags & TIMECARD_TIMECODE_FLAG_YEAR_WRITABLE) == 0u))) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    control = response->Control;
    oldControl = control;
    if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
        request->Role == TIMECARD_TIMECODE_ROLE_MASTER) {
        volatile TIMECARD_IRIG_MASTER_REG *reg = context->IrigMaster;

        oldCorrection = READ_REGISTER_ULONG((PULONG)&reg->Correction);
        oldControlBits = READ_REGISTER_ULONG((PULONG)&reg->ControlBits);
        configured = TRUE;
        WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                             control & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Correction, correction);
        controlBits = oldControlBits;
        controlBits = (controlBits & ~0x07ffffffu) |
                      request->ControlBits;
        WRITE_REGISTER_ULONG((PULONG)&reg->ControlBits, controlBits);
        control = (control & ~TIMECARD_IRIG_MASTER_CONTROL_MASK) |
                  ((request->Mode & 3u) << 24) |
                  ((request->Code & 7u) << 16);
        if ((response->Flags & TIMECARD_TIMECODE_FLAG_AM_WRITABLE) != 0u) {
            control &= ~TIMECARD_IRIG_AM_CONTROL;
            if (request->AmplitudeModulation != 0u)
                control |= TIMECARD_IRIG_AM_CONTROL;
        }
        if ((request->Flags & TIMECARD_TIMECODE_FLAG_ENABLED) != 0)
            control |= TIMECARD_CORE_ENABLE;
        WRITE_REGISTER_ULONG((PULONG)&reg->Control, control);
    } else if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG) {
        volatile TIMECARD_IRIG_SLAVE_REG *reg = context->IrigSlave;

        oldCorrection = READ_REGISTER_ULONG((PULONG)&reg->Correction);
        oldDelay = READ_REGISTER_ULONG((PULONG)&reg->CableDelay);
        if ((response->Flags & TIMECARD_TIMECODE_FLAG_YEAR_WRITABLE) != 0u) {
            oldYear = READ_REGISTER_ULONG((PULONG)&reg->ManualYear);
            oldYearCaptured = TRUE;
        }
        configured = TRUE;
        WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                             control & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Correction, correction);
        delay = oldDelay;
        delay = (delay & ~TIMECARD_IRIG_DELAY_MAXIMUM) |
                (ULONG)request->DelayNanoseconds;
        WRITE_REGISTER_ULONG((PULONG)&reg->CableDelay, delay);
        control &= ~TIMECARD_CORE_ENABLE;
        if (TimeCardVersionAtLeast(response->Version, 1u, 3u)) {
            control = (control & ~((3u << 24) |
                                   TIMECARD_CORE_ENABLE)) |
                      ((request->Mode & 3u) << 24);
        }
        if ((response->Flags & TIMECARD_TIMECODE_FLAG_YEAR_WRITABLE) != 0u) {
            control = (control & ~(7u << 16)) |
                      ((request->Code & 7u) << 16);
            if (request->ManualYear != 0u) {
                WRITE_REGISTER_ULONG((PULONG)&reg->ManualYear,
                                     request->ManualYear & 0xfffu);
                WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                    (control & ~TIMECARD_CORE_ENABLE) |
                    TIMECARD_IRIG_YEAR_VALID_CONTROL);
            }
        }
        if ((response->Flags & TIMECARD_TIMECODE_FLAG_AM_WRITABLE) != 0u) {
            control &= ~TIMECARD_IRIG_AM_CONTROL;
            if (request->AmplitudeModulation != 0u)
                control |= TIMECARD_IRIG_AM_CONTROL;
        }
        if ((request->Flags & TIMECARD_TIMECODE_FLAG_ENABLED) != 0)
            control |= TIMECARD_CORE_ENABLE;
        WRITE_REGISTER_ULONG((PULONG)&reg->Control, control);
    } else if (request->Role == TIMECARD_TIMECODE_ROLE_MASTER) {
        volatile TIMECARD_DCF_MASTER_REG *reg = context->DcfMaster;

        oldCorrection = READ_REGISTER_ULONG((PULONG)&reg->Correction);
        configured = TRUE;
        WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                             control & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Correction, correction);
        control &= ~TIMECARD_CORE_ENABLE;
        if ((request->Flags & TIMECARD_TIMECODE_FLAG_ENABLED) != 0)
            control |= TIMECARD_CORE_ENABLE;
        WRITE_REGISTER_ULONG((PULONG)&reg->Control, control);
    } else {
        volatile TIMECARD_DCF_SLAVE_REG *reg = context->DcfSlave;

        oldCorrection = READ_REGISTER_ULONG((PULONG)&reg->Correction);
        oldDelay = READ_REGISTER_ULONG((PULONG)&reg->AirDelay);
        configured = TRUE;
        WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                             control & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Correction, correction);
        delay = oldDelay;
        delay = (delay & ~TIMECARD_DCF_DELAY_MAXIMUM) |
                (ULONG)request->DelayNanoseconds;
        WRITE_REGISTER_ULONG((PULONG)&reg->AirDelay, delay);
        control &= ~TIMECARD_CORE_ENABLE;
        if ((request->Flags & TIMECARD_TIMECODE_FLAG_ENABLED) != 0)
            control |= TIMECARD_CORE_ENABLE;
        WRITE_REGISTER_ULONG((PULONG)&reg->Control, control);
    }
    status = TimeCardTimecodeQueryLocked(
        context, request->Format, request->Role, masterAmAllowed,
        slaveAmAllowed, slaveYearAllowed, response);
    if (!NT_SUCCESS(status))
        goto rollback;
    enabled = (request->Flags & TIMECARD_TIMECODE_FLAG_ENABLED) != 0;
    if (((response->Flags & TIMECARD_TIMECODE_FLAG_ENABLED) != 0) !=
            enabled ||
        response->CorrectionSeconds != request->CorrectionSeconds ||
        (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
         response->Mode != request->Mode) ||
        (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
         request->Role == TIMECARD_TIMECODE_ROLE_MASTER &&
         (response->Code != request->Code ||
           response->ControlBits != request->ControlBits)) ||
        (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
         (response->Flags & TIMECARD_TIMECODE_FLAG_AM_WRITABLE) != 0u &&
         response->AmplitudeModulation !=
             request->AmplitudeModulation) ||
        (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
         request->Role == TIMECARD_TIMECODE_ROLE_SLAVE &&
         (response->Flags & TIMECARD_TIMECODE_FLAG_YEAR_WRITABLE) != 0u &&
         (response->Code != request->Code ||
          (request->ManualYear != 0u &&
           response->ManualYear != request->ManualYear))) ||
        (request->Role == TIMECARD_TIMECODE_ROLE_SLAVE &&
         response->DelayNanoseconds != request->DelayNanoseconds)) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto rollback;
    }
    if ((request->Flags & TIMECARD_TIMECODE_FLAG_CLEAR_ERRORS) != 0u) {
        if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
            request->Role == TIMECARD_TIMECODE_ROLE_MASTER) {
            WRITE_REGISTER_ULONG((PULONG)&context->IrigMaster->Status,
                                 TIMECARD_TIMECODE_ERROR_MASK);
        } else if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG) {
            WRITE_REGISTER_ULONG((PULONG)&context->IrigSlave->Status,
                                 TIMECARD_TIMECODE_ERROR_MASK);
        } else if (request->Role == TIMECARD_TIMECODE_ROLE_MASTER) {
            WRITE_REGISTER_ULONG((PULONG)&context->DcfMaster->Status,
                                 TIMECARD_TIMECODE_ERROR_MASK);
        } else {
            WRITE_REGISTER_ULONG((PULONG)&context->DcfSlave->Status,
                                 TIMECARD_TIMECODE_ERROR_MASK);
        }
        if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
            request->Role == TIMECARD_TIMECODE_ROLE_MASTER) {
            response->Status = READ_REGISTER_ULONG(
                (PULONG)&context->IrigMaster->Status);
        } else if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG) {
            response->Status = READ_REGISTER_ULONG(
                (PULONG)&context->IrigSlave->Status);
        } else if (request->Role == TIMECARD_TIMECODE_ROLE_MASTER) {
            response->Status = READ_REGISTER_ULONG(
                (PULONG)&context->DcfMaster->Status);
        } else {
            response->Status = READ_REGISTER_ULONG(
                (PULONG)&context->DcfSlave->Status);
        }
        if ((response->Status & TIMECARD_TIMECODE_ERROR_MASK) == 0u)
            response->Flags &= ~TIMECARD_TIMECODE_FLAG_ERROR;
    }
    if (request->Format == TIMECARD_TIMECODE_FORMAT_IRIG) {
        if (request->Role == TIMECARD_TIMECODE_ROLE_MASTER)
            context->IrigMasterRouteManaged = FALSE;
        else
            context->IrigSlaveRouteManaged = FALSE;
    } else if (request->Role == TIMECARD_TIMECODE_ROLE_MASTER) {
        context->DcfMasterRouteManaged = FALSE;
    } else {
        context->DcfSlaveRouteManaged = FALSE;
    }
    goto done;

rollback:
    if (configured && request->Format == TIMECARD_TIMECODE_FORMAT_IRIG &&
        request->Role == TIMECARD_TIMECODE_ROLE_MASTER) {
        volatile TIMECARD_IRIG_MASTER_REG *reg = context->IrigMaster;

        WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                             oldControl & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Correction, oldCorrection);
        WRITE_REGISTER_ULONG((PULONG)&reg->ControlBits, oldControlBits);
        WRITE_REGISTER_ULONG((PULONG)&reg->Control, oldControl);
    } else if (configured &&
               request->Format == TIMECARD_TIMECODE_FORMAT_IRIG) {
        volatile TIMECARD_IRIG_SLAVE_REG *reg = context->IrigSlave;

        WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                             oldControl & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Correction, oldCorrection);
        WRITE_REGISTER_ULONG((PULONG)&reg->CableDelay, oldDelay);
        if (oldYearCaptured) {
            WRITE_REGISTER_ULONG((PULONG)&reg->ManualYear, oldYear);
            WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                (oldControl & ~TIMECARD_CORE_ENABLE) |
                TIMECARD_IRIG_YEAR_VALID_CONTROL);
        }
        WRITE_REGISTER_ULONG((PULONG)&reg->Control, oldControl);
    } else if (configured &&
               request->Role == TIMECARD_TIMECODE_ROLE_MASTER) {
        volatile TIMECARD_DCF_MASTER_REG *reg = context->DcfMaster;

        WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                             oldControl & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Correction, oldCorrection);
        WRITE_REGISTER_ULONG((PULONG)&reg->Control, oldControl);
    } else if (configured) {
        volatile TIMECARD_DCF_SLAVE_REG *reg = context->DcfSlave;

        WRITE_REGISTER_ULONG((PULONG)&reg->Control,
                             oldControl & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&reg->Correction, oldCorrection);
        WRITE_REGISTER_ULONG((PULONG)&reg->AirDelay, oldDelay);
        WRITE_REGISTER_ULONG((PULONG)&reg->Control, oldControl);
    }
done:
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

static NTSTATUS
TimeCardTodQueryLocked(PDEVICE_CONTEXT context, TIMECARD_TOD_CONTROL *control,
                       BOOLEAN telemetryAllowed)
{
    ULONG rawPolarity;
    ULONG selector;
    ULONG version;

    if (!TimeCardStandardFpga(context) || context->Tod == NULL)
        return STATUS_NOT_SUPPORTED;
    version = READ_REGISTER_ULONG((PULONG)&context->Tod->Version);
    if (!TimeCardVersionPresent(version))
        return STATUS_NOT_SUPPORTED;
    RtlZeroMemory(control, sizeof(*control));
    control->Size = sizeof(*control);
    control->Version = version;
    control->Control = READ_REGISTER_ULONG((PULONG)&context->Tod->Ctrl);
    if (TimeCardVersionAtLeast(version, 1u, 2u))
        control->Status = READ_REGISTER_ULONG((PULONG)&context->Tod->Status);
    if (TimeCardVersionAtLeast(version, 1u, 6u))
        control->Protocol = (control->Control >> 28) & 7u;
    else
        control->Protocol = TIMECARD_TOD_PROTOCOL_NMEA;
    if (!TimeCardTodProtocolSupported(version, control->Protocol))
        return STATUS_DEVICE_DATA_ERROR;
    if (TimeCardVersionAtLeast(version, 1u, 5u))
        control->Gnss = (control->Control >> 24) & 0x0fu;
    if (control->Gnss > TIMECARD_TOD_GNSS_BEIDOU)
        return STATUS_DEVICE_DATA_ERROR;
    control->MessageDisableMask = ((control->Control >> 16) & 0xffu) &
        TimeCardTodSupportedMessageMask(version, control->Protocol);
    if (TimeCardVersionAtLeast(version, 1u, 3u)) {
        rawPolarity = READ_REGISTER_ULONG(
            (PULONG)&context->Tod->UartPolarity) & 1u;
        /* The FPGA encodes zero as inverted; the public ABI uses one. */
        control->Polarity = rawPolarity == 0u ? 1u : 0u;
    }
    selector = READ_REGISTER_ULONG((PULONG)&context->Tod->UartBaud) & 0x0fu;
    control->BaudSelector = selector;
    if (selector < ARRAYSIZE(TimeCardTodBaudRates))
        control->Baud = TimeCardTodBaudRates[selector];
    control->CorrectionSeconds = TimeCardSignedMagnitudeDecode(
        READ_REGISTER_ULONG((PULONG)&context->Tod->Correction),
        TIMECARD_CORRECTION_MAGNITUDE);
    control->Flags = TIMECARD_TOD_FLAG_PRESENT;
    if (telemetryAllowed &&
        TimeCardTodUtcTelemetrySupported(version, control->Protocol)) {
        control->UtcStatus = READ_REGISTER_ULONG(
            (PULONG)&context->Tod->UtcStatus);
        control->TimeToLeapSeconds = (LONG)READ_REGISTER_ULONG(
            (PULONG)&context->Tod->Leap);
        control->Flags |= TIMECARD_TOD_FLAG_UTC_TELEMETRY_VALID;
    }
    if (telemetryAllowed &&
        TimeCardTodGnssTelemetrySupported(version, control->Protocol)) {
        control->GnssStatus = READ_REGISTER_ULONG(
            (PULONG)&context->Tod->GnssStatus);
        control->Satellites = READ_REGISTER_ULONG(
            (PULONG)&context->Tod->NumSat);
        control->Flags |= TIMECARD_TOD_FLAG_GNSS_TELEMETRY_VALID;
    }
    if ((control->Control & TIMECARD_CORE_ENABLE) != 0)
        control->Flags |= TIMECARD_TOD_FLAG_ENABLED;
    if (TimeCardVersionAtLeast(version, 1u, 2u) &&
        (control->Status & 1u) != 0)
        control->Flags |= TIMECARD_TOD_FLAG_PARSE_ERROR;
    if (TimeCardVersionAtLeast(version, 1u, 4u) &&
        (control->Status & 2u) != 0)
        control->Flags |= TIMECARD_TOD_FLAG_CHECKSUM_ERROR;
    if (TimeCardVersionAtLeast(version, 1u, 4u) &&
        (control->Status & 4u) != 0)
        control->Flags |= TIMECARD_TOD_FLAG_UART_ERROR;
    return STATUS_SUCCESS;
}

NTSTATUS
TimeCardTodQuery(PDEVICE_CONTEXT context, TIMECARD_TOD_CONTROL *control)
{
    BOOLEAN telemetryAllowed;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    telemetryAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_TOD_TELEMETRY);
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    status = TimeCardTodQueryLocked(context, control, telemetryAllowed);
    WdfWaitLockRelease(context->RegisterLock);
    return status;
}

NTSTATUS
TimeCardTodSet(PDEVICE_CONTEXT context,
               const TIMECARD_TOD_CONTROL *request,
               TIMECARD_TOD_CONTROL *response)
{
    TIMECARD_TOD_CONTROL current;
    TIMECARD_UART_CONFIG uartConfig;
    TIMECARD_UART_HARDWARE_STATE uartState;
    BOOLEAN configured = FALSE;
    BOOLEAN uartStateCaptured = FALSE;
    ULONG allowedFlags;
    ULONG correction;
    ULONG selector;
    ULONG control;
    ULONG configMask;
    ULONG errorMask;
    ULONG supportedMessageMask;
    ULONG value;
    ULONG version;
    ULONG index;
    ULONG oldBaud = 0u;
    ULONG oldControl = 0u;
    ULONG oldCorrection = 0u;
    ULONG oldPolarity = 0u;
    BOOLEAN enabled;
    BOOLEAN telemetryAllowed;
    NTSTATUS status;

    if (!context->HardwareReady)
        return STATUS_DEVICE_NOT_READY;
    allowedFlags = TIMECARD_TOD_FLAG_PRESENT | TIMECARD_TOD_FLAG_ENABLED |
        TIMECARD_TOD_FLAG_PARSE_ERROR |
        TIMECARD_TOD_FLAG_CHECKSUM_ERROR |
        TIMECARD_TOD_FLAG_UART_ERROR |
        TIMECARD_TOD_FLAG_UTC_TELEMETRY_VALID |
        TIMECARD_TOD_FLAG_GNSS_TELEMETRY_VALID |
        TIMECARD_TOD_FLAG_CLEAR_ERRORS;
    if (request->Size < sizeof(*request) ||
        request->Protocol > TIMECARD_TOD_PROTOCOL_PFEC ||
        request->Gnss > TIMECARD_TOD_GNSS_BEIDOU ||
        request->Polarity > 1u || request->MessageDisableMask > 0xffu ||
        (request->Flags & ~allowedFlags) != 0)
        return STATUS_INVALID_PARAMETER;
    for (index = 0; index < ARRAYSIZE(request->Reserved); ++index) {
        if (request->Reserved[index] != 0u)
            return STATUS_INVALID_PARAMETER;
    }
    status = TimeCardTodSelectorFromBaud(request->Baud, &selector);
    if (!NT_SUCCESS(status))
        return status;
    status = TimeCardSignedMagnitudeEncode(
        request->CorrectionSeconds, TIMECARD_CORRECTION_MAGNITUDE,
        &correction);
    if (!NT_SUCCESS(status))
        return status;
    status = TimeCardTodQuery(context, &current);
    if (!NT_SUCCESS(status))
        return status;
    version = current.Version;
    if (!TimeCardTodProtocolSupported(version, request->Protocol) ||
        (request->Gnss != TIMECARD_TOD_GNSS_ALL &&
         !TimeCardVersionAtLeast(version, 1u, 5u)) ||
        (request->Polarity != current.Polarity &&
         !TimeCardVersionAtLeast(version, 1u, 3u)) ||
        ((request->Flags & TIMECARD_TOD_FLAG_CLEAR_ERRORS) != 0 &&
         !TimeCardVersionAtLeast(version, 1u, 2u))) {
        return STATUS_NOT_SUPPORTED;
    }
    supportedMessageMask = TimeCardTodSupportedMessageMask(
        version, request->Protocol);
    if ((request->MessageDisableMask & ~supportedMessageMask) != 0)
        return STATUS_NOT_SUPPORTED;

    if (context->Uart[TIMECARD_UART_GNSS] != NULL) {
        status = TimeCardUartSnapshotHardware(
            context, TIMECARD_UART_GNSS, &uartState);
        if (!NT_SUCCESS(status))
            return status;
        uartStateCaptured = TRUE;
        uartConfig.Port = TIMECARD_UART_GNSS;
        uartConfig.Baud = request->Baud;
        status = TimeCardUartConfigure(context, &uartConfig);
        if (!NT_SUCCESS(status)) {
            (VOID)TimeCardUartRestoreHardware(
                context, TIMECARD_UART_GNSS, &uartState);
            return status;
        }
    }

    telemetryAllowed = TimeCardFpgaContractAllows(
        context, TIMECARD_FPGA_CONTRACT_TOD_TELEMETRY);
    WdfWaitLockAcquire(context->RegisterLock, NULL);
    if (!TimeCardStandardFpga(context) || context->Tod == NULL) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    version = READ_REGISTER_ULONG((PULONG)&context->Tod->Version);
    if (!TimeCardVersionPresent(version) || version != current.Version) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    control = READ_REGISTER_ULONG((PULONG)&context->Tod->Ctrl);
    oldControl = control;
    oldCorrection = READ_REGISTER_ULONG(
        (PULONG)&context->Tod->Correction);
    oldBaud = READ_REGISTER_ULONG((PULONG)&context->Tod->UartBaud);
    oldPolarity = TimeCardVersionAtLeast(version, 1u, 3u) ?
        READ_REGISTER_ULONG((PULONG)&context->Tod->UartPolarity) : 0u;
    configured = TRUE;
    WRITE_REGISTER_ULONG((PULONG)&context->Tod->Ctrl,
                         control & ~TIMECARD_CORE_ENABLE);
    WRITE_REGISTER_ULONG((PULONG)&context->Tod->Correction, correction);
    value = READ_REGISTER_ULONG((PULONG)&context->Tod->UartBaud);
    value = (value & ~0x0fu) | selector;
    WRITE_REGISTER_ULONG((PULONG)&context->Tod->UartBaud, value);
    if (TimeCardVersionAtLeast(version, 1u, 3u)) {
        value = READ_REGISTER_ULONG((PULONG)&context->Tod->UartPolarity);
        value = (value & ~1u) | (request->Polarity != 0u ? 0u : 1u);
        WRITE_REGISTER_ULONG((PULONG)&context->Tod->UartPolarity, value);
    }
    configMask = TIMECARD_CORE_ENABLE |
        (((current.Protocol != request->Protocol ? 0xffu :
           supportedMessageMask) & 0xffu) << 16);
    if (TimeCardVersionAtLeast(version, 1u, 6u))
        configMask |= TIMECARD_TOD_PROTOCOL_CONFIG_MASK;
    if (TimeCardVersionAtLeast(version, 1u, 5u))
        configMask |= TIMECARD_TOD_GNSS_CONFIG_MASK;
    control = (control & ~configMask) |
              ((request->MessageDisableMask & 0xffu) << 16);
    if (TimeCardVersionAtLeast(version, 1u, 6u))
        control |= (request->Protocol & 7u) << 28;
    if (TimeCardVersionAtLeast(version, 1u, 5u))
        control |= (request->Gnss & 0x0fu) << 24;
    if ((request->Flags & TIMECARD_TOD_FLAG_CLEAR_ERRORS) != 0) {
        errorMask = TimeCardVersionAtLeast(version, 1u, 4u) ?
            TIMECARD_TOD_ERROR_MASK : 0x00000001u;
        WRITE_REGISTER_ULONG((PULONG)&context->Tod->Status, errorMask);
    }
    if ((request->Flags & TIMECARD_TOD_FLAG_ENABLED) != 0)
        control |= TIMECARD_CORE_ENABLE;
    WRITE_REGISTER_ULONG((PULONG)&context->Tod->Ctrl, control);
    status = TimeCardTodQueryLocked(context, response, telemetryAllowed);
    if (!NT_SUCCESS(status))
        goto rollback;
    enabled = (request->Flags & TIMECARD_TOD_FLAG_ENABLED) != 0;
    if (((response->Flags & TIMECARD_TOD_FLAG_ENABLED) != 0) != enabled ||
        response->Protocol != request->Protocol ||
        response->Gnss != request->Gnss ||
        response->Baud != request->Baud ||
        response->Polarity != request->Polarity ||
        response->CorrectionSeconds != request->CorrectionSeconds ||
        response->MessageDisableMask != request->MessageDisableMask) {
        status = STATUS_DEVICE_DATA_ERROR;
        goto rollback;
    }
    goto done;

rollback:
    if (configured) {
        WRITE_REGISTER_ULONG((PULONG)&context->Tod->Ctrl,
                             oldControl & ~TIMECARD_CORE_ENABLE);
        WRITE_REGISTER_ULONG((PULONG)&context->Tod->Correction,
                             oldCorrection);
        WRITE_REGISTER_ULONG((PULONG)&context->Tod->UartBaud, oldBaud);
        if (TimeCardVersionAtLeast(version, 1u, 3u))
            WRITE_REGISTER_ULONG((PULONG)&context->Tod->UartPolarity,
                                 oldPolarity);
        WRITE_REGISTER_ULONG((PULONG)&context->Tod->Ctrl, oldControl);
    }
done:
    WdfWaitLockRelease(context->RegisterLock);
    if (!NT_SUCCESS(status) && uartStateCaptured)
        (VOID)TimeCardUartRestoreHardware(
            context, TIMECARD_UART_GNSS, &uartState);
    return status;
}
