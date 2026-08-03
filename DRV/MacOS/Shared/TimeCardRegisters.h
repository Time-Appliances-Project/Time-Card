/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef TIMECARD_REGISTERS_H
#define TIMECARD_REGISTERS_H

#include <stdbool.h>
#include <stdint.h>

#include "TimeCardABI.h"

#define TIMECARD_PCI_MATCH_STRING \
    "0x04001d9b 0x100818d4 0xa0001ad7 0x0400ad5a 0x0410ad5a"

enum {
    kTimeCardPCIMatchFacebook = 0x04001d9b,
    kTimeCardPCIMatchCelestica = 0x100818d4,
    kTimeCardPCIMatchOroliaART = 0xa0001ad7,
    kTimeCardPCIMatchADVA = 0x0400ad5a,
    kTimeCardPCIMatchADVAX1 = 0x0410ad5a
};

enum {
    kTimeCardClockControl = 0x00,
    kTimeCardClockStatus = 0x04,
    kTimeCardClockSelect = 0x08,
    kTimeCardClockVersion = 0x0c,
    kTimeCardClockTimeNanoseconds = 0x10,
    kTimeCardClockTimeSeconds = 0x14,
    kTimeCardClockAdjustNanoseconds = 0x20,
    kTimeCardClockAdjustSeconds = 0x24,

    kTimeCardTodStatus = 0x04,
    kTimeCardTodVersion = 0x0c,
    kTimeCardTodUtcStatus = 0x30,
    kTimeCardTodLeap = 0x34,
    kTimeCardTodGnssStatus = 0x40,
    kTimeCardTodSatellites = 0x44
};

enum {
    kTimeCardClockEnable = 1u << 0,
    kTimeCardClockAdjustTime = 1u << 1,
    kTimeCardClockAdjustOffset = 1u << 2,
    kTimeCardClockAdjustDrift = 1u << 3,
    kTimeCardClockAdjustServo = 1u << 8,
    kTimeCardClockReadRequest = 1u << 30,
    kTimeCardClockReadDone = 1u << 31,
    kTimeCardClockTransientMask =
        kTimeCardClockAdjustTime | kTimeCardClockAdjustOffset |
        kTimeCardClockAdjustDrift | kTimeCardClockAdjustServo |
        kTimeCardClockReadRequest | kTimeCardClockReadDone,
    kTimeCardClockRegisterSource = 0xfeu
};

enum {
    kTimeCardClockVersionStatus = (1u << 24) | (2u << 16),
    kTimeCardTODVersionStatus = (1u << 24) | (2u << 16)
};

typedef struct TimeCardRegisterMap {
    uint32_t boardProfile;
    uint32_t layout;
    uint32_t capabilities;
    uint32_t reserved;
    uint64_t clockOffset;
    uint64_t todOffset;
    uint64_t uartOffsets[4];
    uint64_t requiredBarSize;
} TimeCardRegisterMap;

static inline uint32_t
TimeCardPCIPrimaryMatch(uint16_t vendorID, uint16_t deviceID)
{
    return ((uint32_t)deviceID << 16) | vendorID;
}

static inline uint32_t
TimeCardBoardProfileForDevice(uint16_t vendorID, uint16_t deviceID)
{
    switch (TimeCardPCIPrimaryMatch(vendorID, deviceID)) {
    case kTimeCardPCIMatchFacebook:
        return kTimeCardBoardFacebook;
    case kTimeCardPCIMatchCelestica:
        return kTimeCardBoardCelestica;
    case kTimeCardPCIMatchOroliaART:
        return kTimeCardBoardOroliaART;
    case kTimeCardPCIMatchADVA:
        return kTimeCardBoardADVA;
    case kTimeCardPCIMatchADVAX1:
        return kTimeCardBoardADVAX1;
    default:
        return kTimeCardBoardUnknown;
    }
}

static inline const char *
TimeCardBoardProfileName(uint32_t profile)
{
    switch (profile) {
    case kTimeCardBoardFacebook:
        return "Meta/Facebook";
    case kTimeCardBoardCelestica:
        return "Celestica R4006";
    case kTimeCardBoardOroliaART:
        return "Orolia/Safran ART";
    case kTimeCardBoardADVA:
        return "ADVA Time Card";
    case kTimeCardBoardADVAX1:
        return "ADVA Time Card X1";
    default:
        return "Unsupported";
    }
}

static inline const char *
TimeCardRegisterLayoutName(uint32_t layout)
{
    switch (layout) {
    case kTimeCardLayoutClassic:
        return "classic map";
    case kTimeCardLayoutLitePCIe:
        return "shifted LitePCIe map";
    case kTimeCardLayoutART:
        return "ART map";
    default:
        return "unknown map";
    }
}

static inline uint32_t
TimeCardMSIXVectorCount(uint16_t messageControl)
{
    return (uint32_t)(messageControl & 0x07ffu) + 1u;
}

static inline uint32_t
TimeCardConfiguredClockSource(uint32_t selectRegister)
{
    return selectRegister & 0xffu;
}

static inline uint32_t
TimeCardPersistentClockControl(uint32_t controlRegister)
{
    return controlRegister & ~kTimeCardClockTransientMask;
}

static inline uint32_t
TimeCardClockReadRequestControl(uint32_t controlRegister)
{
    return TimeCardPersistentClockControl(controlRegister) |
        kTimeCardClockReadRequest | kTimeCardClockEnable;
}

static inline uint32_t
TimeCardClockAdjustRequestControl(uint32_t controlRegister)
{
    return TimeCardPersistentClockControl(controlRegister) |
        kTimeCardClockAdjustTime | kTimeCardClockEnable;
}

static inline bool
TimeCardRangeFits(uint64_t barSize, uint64_t offset, uint64_t length)
{
    return offset <= barSize && length <= barSize - offset;
}

static inline bool
TimeCardRegisterMapFits(uint64_t barSize, const TimeCardRegisterMap *map)
{
    return map != NULL && map->layout != kTimeCardLayoutUnknown &&
        TimeCardRangeFits(barSize, 0, map->requiredBarSize);
}

static inline TimeCardRegisterMap
TimeCardFacebookRegisterMap(bool useMSIX, uint32_t boardProfile)
{
    TimeCardRegisterMap map = {
        boardProfile, kTimeCardLayoutUnknown,
        kTimeCardCapabilityReadClock | kTimeCardCapabilitySetClock |
            kTimeCardCapabilityCrossTimestamp | kTimeCardCapabilityTOD,
        0, 0, 0, {0, 0, 0, 0}, 0
    };

    if (useMSIX) {
        map.layout = kTimeCardLayoutLitePCIe;
        map.clockOffset = 0x03000000u;
        map.todOffset = 0x03050000u;
        map.uartOffsets[0] = 0x02161000u;
        map.uartOffsets[1] = 0x02171000u;
        map.uartOffsets[2] = 0x02181000u;
        map.uartOffsets[3] = 0x02191000u;
        map.requiredBarSize = map.todOffset + kTimeCardTodVersion + 4u;
    } else {
        map.layout = kTimeCardLayoutClassic;
        map.clockOffset = 0x01000000u;
        map.todOffset = 0x01050000u;
        map.uartOffsets[0] = 0x00161000u;
        map.uartOffsets[1] = 0x00171000u;
        map.uartOffsets[2] = 0x00181000u;
        map.uartOffsets[3] = 0x00191000u;
        map.requiredBarSize = map.todOffset + kTimeCardTodVersion + 4u;
    }

    return map;
}

static inline TimeCardRegisterMap
TimeCardRegisterMapForDevice(uint16_t vendorID, uint16_t deviceID,
                             uint8_t revisionID,
                             uint32_t advertisedMSIXVectors)
{
    const uint32_t profile =
        TimeCardBoardProfileForDevice(vendorID, deviceID);
    TimeCardRegisterMap map = {
        profile, kTimeCardLayoutUnknown, 0, 0, 0, 0,
        {0, 0, 0, 0}, 0
    };

    if (profile == kTimeCardBoardFacebook ||
        profile == kTimeCardBoardCelestica) {
        /*
         * This repository's LitePCIe gateware publishes revision 02 and
         * relocates the standard timing windows by 0x02000000. Its MSI-X
         * table advertises 64 vectors. Classic images use revision 00/01 and
         * do not advertise a multi-vector MSI-X table. Reject inconsistent
         * tuples before MMIO rather than guessing a register map.
         */
        if (revisionID <= 1u && advertisedMSIXVectors <= 1u)
            return TimeCardFacebookRegisterMap(false, profile);
        if (revisionID == 2u && advertisedMSIXVectors > 32u &&
            advertisedMSIXVectors != UINT32_MAX)
            return TimeCardFacebookRegisterMap(true, profile);
        return map;
    }

    if (profile == kTimeCardBoardOroliaART) {
        map.layout = kTimeCardLayoutART;
        map.capabilities = kTimeCardCapabilityReadClock |
            kTimeCardCapabilitySetClock |
            kTimeCardCapabilityCrossTimestamp;
        map.clockOffset = 0x01000000u;
        map.uartOffsets[0] = 0x00161000u;
        map.uartOffsets[2] = 0x00190000u;
        map.requiredBarSize =
            map.clockOffset + kTimeCardClockAdjustSeconds + 4u;
        return map;
    }

    if (profile == kTimeCardBoardADVA ||
        profile == kTimeCardBoardADVAX1) {
        /* ADVA uses the original common clock/ToD map even with MSI-X. */
        map.layout = kTimeCardLayoutClassic;
        map.capabilities = kTimeCardCapabilityReadClock |
            kTimeCardCapabilitySetClock |
            kTimeCardCapabilityCrossTimestamp | kTimeCardCapabilityTOD;
        map.clockOffset = 0x01000000u;
        map.todOffset = 0x01050000u;
        map.uartOffsets[0] = 0x00161000u;
        map.uartOffsets[2] = 0x00181000u;
        map.requiredBarSize =
            map.todOffset + kTimeCardTodVersion + 4u;
    }
    return map;
}

static inline bool
TimeCardRegisterMapHasTOD(const TimeCardRegisterMap *map)
{
    return map != NULL && map->todOffset != 0;
}

#endif /* TIMECARD_REGISTERS_H */
