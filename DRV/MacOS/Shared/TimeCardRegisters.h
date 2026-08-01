/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef TIMECARD_REGISTERS_H
#define TIMECARD_REGISTERS_H

#include <stdbool.h>
#include <stdint.h>

#include "TimeCardABI.h"

enum {
    kTimeCardVendorFacebook = 0x1d9b,
    kTimeCardDeviceFacebook = 0x0400,
    kTimeCardVendorCelestica = 0x18d4,
    kTimeCardDeviceCelestica = 0x1008,
    kTimeCardVendorOrolia = 0x1ad7,
    kTimeCardDeviceOrolia = 0xa000
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
    kTimeCardClockReadRequest = 1u << 30,
    kTimeCardClockReadDone = 1u << 31,
    kTimeCardClockRegisterSource = 0xfeu
};

typedef struct TimeCardRegisterMap {
    uint32_t layout;
    uint64_t clockOffset;
    uint64_t todOffset;
    uint64_t uartOffsets[4];
    uint64_t requiredBarSize;
} TimeCardRegisterMap;

static inline uint32_t
TimeCardMSIXVectorCount(uint16_t messageControl)
{
    return (uint32_t)(messageControl & 0x07ffu) + 1u;
}

static inline TimeCardRegisterMap
TimeCardRegisterMapForInterrupts(bool hasMSIX, uint32_t vectorCount)
{
    TimeCardRegisterMap map = {
        kTimeCardLayoutUnknown, 0, 0, {0, 0, 0, 0}, 0
    };

    if (hasMSIX && vectorCount > 1u) {
        map.layout = kTimeCardLayoutMSIX;
        map.clockOffset = 0x03000000u;
        map.todOffset = 0x03050000u;
        map.uartOffsets[0] = 0x02161000u;
        map.uartOffsets[1] = 0x02171000u;
        map.uartOffsets[2] = 0x02181000u;
        map.uartOffsets[3] = 0x02191000u;
        map.requiredBarSize = map.todOffset + kTimeCardTodSatellites + 4u;
    } else {
        map.layout = kTimeCardLayoutMSI;
        map.clockOffset = 0x01000000u;
        map.todOffset = 0x01050000u;
        map.uartOffsets[0] = 0x00161000u;
        map.uartOffsets[1] = 0x00171000u;
        map.uartOffsets[2] = 0x00181000u;
        map.uartOffsets[3] = 0x00191000u;
        map.requiredBarSize = map.todOffset + kTimeCardTodSatellites + 4u;
    }

    return map;
}

static inline bool
TimeCardRangeFits(uint64_t barSize, uint64_t offset, uint64_t length)
{
    return offset <= barSize && length <= barSize - offset;
}

#endif /* TIMECARD_REGISTERS_H */
