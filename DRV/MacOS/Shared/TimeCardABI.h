/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef TIMECARD_ABI_H
#define TIMECARD_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIMECARD_ABI_VERSION 2u
#define TIMECARD_DRIVER_VERSION 0x00000002u
#define TIMECARD_SERVICE_CLASS "TimeCardDriver"
#define TIMECARD_DRIVER_BUNDLE_ID "org.opentimeserver.timecard.macos.driver"

enum TimeCardExternalMethod {
    kTimeCardMethodGetInfo = 0,
    kTimeCardMethodGetTime = 1,
    kTimeCardMethodSetTime = 2,
    kTimeCardMethodGetCrossTimestamp = 3,
    kTimeCardMethodCount
};

enum TimeCardRegisterLayout {
    kTimeCardLayoutUnknown = 0,
    kTimeCardLayoutClassic = 1,
    kTimeCardLayoutLitePCIe = 2,
    kTimeCardLayoutART = 3,

    /* ABI v1 source-compatible aliases. These name the register map only. */
    kTimeCardLayoutMSI = kTimeCardLayoutClassic,
    kTimeCardLayoutMSIX = kTimeCardLayoutLitePCIe
};

enum TimeCardBoardProfile {
    kTimeCardBoardUnknown = 0,
    kTimeCardBoardFacebook = 1,
    kTimeCardBoardCelestica = 2,
    kTimeCardBoardOroliaART = 3,
    kTimeCardBoardADVA = 4,
    kTimeCardBoardADVAX1 = 5
};

enum TimeCardCapability {
    kTimeCardCapabilityReadClock = 1u << 0,
    kTimeCardCapabilitySetClock = 1u << 1,
    kTimeCardCapabilityCrossTimestamp = 1u << 2,
    kTimeCardCapabilityTOD = 1u << 3
};

enum TimeCardInfoValidField {
    kTimeCardInfoValidClockVersion = 1ull << 0,
    kTimeCardInfoValidClockStatus = 1ull << 1,
    kTimeCardInfoValidClockSelect = 1ull << 2,
    kTimeCardInfoValidTODVersion = 1ull << 3,
    kTimeCardInfoValidTODStatus = 1ull << 4,
    kTimeCardInfoValidUTCStatus = 1ull << 5,
    kTimeCardInfoValidLeap = 1ull << 6,
    kTimeCardInfoValidGNSSStatus = 1ull << 7,
    kTimeCardInfoValidSatellites = 1ull << 8
};

typedef struct TimeCardTime {
    uint64_t seconds;
    uint32_t nanoseconds;
    uint32_t reserved;
} TimeCardTime;

/* System timestamps are nanoseconds since the Unix epoch. */
typedef struct TimeCardCrossTimestamp {
    TimeCardTime cardTime;
    uint64_t systemTimeBeforeNanoseconds;
    uint64_t systemTimeAfterNanoseconds;
} TimeCardCrossTimestamp;

typedef struct TimeCardInfo {
    uint32_t abiVersion;
    uint32_t driverVersion;
    uint16_t vendorID;
    uint16_t deviceID;
    uint32_t layout;
    uint32_t advertisedMSIXVectors;
    uint64_t barSize;
    uint64_t clockOffset;
    uint64_t todOffset;
    uint32_t clockVersion;
    uint32_t clockStatus;
    uint32_t clockSelect;
    uint32_t todVersion;
    uint32_t todStatus;
    uint32_t utcStatus;
    uint32_t leap;
    uint32_t gnssStatus;
    uint32_t satellites;
    uint32_t boardProfile;
    uint32_t capabilities;
    uint64_t validFields;
    uint32_t pciRevision;
    uint32_t reserved;
} TimeCardInfo;

#ifdef __cplusplus
}

static_assert(sizeof(TimeCardTime) == 16, "TimeCardTime ABI changed");
static_assert(sizeof(TimeCardCrossTimestamp) == 32,
              "TimeCardCrossTimestamp ABI changed");
static_assert(sizeof(TimeCardInfo) == 112, "TimeCardInfo ABI changed");
#endif

#endif /* TIMECARD_ABI_H */
