/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef TIMECARD_ABI_H
#define TIMECARD_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIMECARD_ABI_VERSION 1u
#define TIMECARD_DRIVER_VERSION 0x00010000u
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
    kTimeCardLayoutMSI = 1,
    kTimeCardLayoutMSIX = 2
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
    uint32_t interruptVectors;
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
} TimeCardInfo;

#ifdef __cplusplus
}

static_assert(sizeof(TimeCardTime) == 16, "TimeCardTime ABI changed");
static_assert(sizeof(TimeCardCrossTimestamp) == 32,
              "TimeCardCrossTimestamp ABI changed");
static_assert(sizeof(TimeCardInfo) == 88, "TimeCardInfo ABI changed");
#endif

#endif /* TIMECARD_ABI_H */
