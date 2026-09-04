/* SPDX-License-Identifier: BSD-3-Clause */

#include <cassert>
#include <cstdint>
#include <cstring>

#include "TimeCardRegisters.h"

static void
AssertExactBarBoundary(const TimeCardRegisterMap &map)
{
    assert(map.requiredBarSize != 0);
    assert(TimeCardRegisterMapFits(map.requiredBarSize, &map));
    assert(!TimeCardRegisterMapFits(map.requiredBarSize - 1, &map));
}

static void
AssertDriverAccessesFit(const TimeCardRegisterMap &map)
{
    const uint64_t clockRegisters[] = {
        kTimeCardClockControl,
        kTimeCardClockStatus,
        kTimeCardClockSelect,
        kTimeCardClockVersion,
        kTimeCardClockTimeNanoseconds,
        kTimeCardClockTimeSeconds,
        kTimeCardClockAdjustNanoseconds,
        kTimeCardClockAdjustSeconds,
    };
    for (uint64_t registerOffset : clockRegisters) {
        assert(TimeCardRangeFits(map.requiredBarSize,
                                 map.clockOffset + registerOffset, 4));
    }

    if (TimeCardRegisterMapHasTOD(&map)) {
        assert(TimeCardRangeFits(map.requiredBarSize,
                                 map.todOffset + kTimeCardTodStatus, 4));
        assert(TimeCardRangeFits(map.requiredBarSize,
                                 map.todOffset + kTimeCardTodVersion, 4));
    }
    if (TimeCardRegisterMapHasLED(&map)) {
        assert(TimeCardRangeFits(map.requiredBarSize,
                                 map.i2cOffset, kTimeCardI2CRegisterLength));
    }
}

int main()
{
    assert(TimeCardMSIXVectorCount(0x0000) == 1);
    assert(TimeCardMSIXVectorCount(0x003f) == 64);
    assert(TimeCardMSIXVectorCount(0xffff) == 2048);
    assert(std::strcmp(TIMECARD_PCI_MATCH_STRING,
                       "0x04001d9b 0x100818d4 0xa0001ad7 "
                       "0x0400ad5a 0x0410ad5a") == 0);
    assert(TimeCardPCIPrimaryMatch(0x1d9b, 0x0400) ==
           kTimeCardPCIMatchFacebook);
    assert(TimeCardPCIPrimaryMatch(0x18d4, 0x1008) ==
           kTimeCardPCIMatchCelestica);
    assert(TimeCardPCIPrimaryMatch(0x1ad7, 0xa000) ==
           kTimeCardPCIMatchOroliaART);
    assert(TimeCardPCIPrimaryMatch(0xad5a, 0x0400) ==
           kTimeCardPCIMatchADVA);
    assert(TimeCardPCIPrimaryMatch(0xad5a, 0x0410) ==
           kTimeCardPCIMatchADVAX1);
    assert(TimeCardConfiguredClockSource(0x000000feu) == 0xfeu);
    assert(TimeCardConfiguredClockSource(0x00ff0003u) == 0x03u);

    const uint32_t persistentControl = 0x000f0001u;
    const uint32_t controlWithTransients =
        persistentControl | kTimeCardClockTransientMask;
    assert(TimeCardPersistentClockControl(controlWithTransients) ==
           persistentControl);
    assert(TimeCardClockReadRequestControl(controlWithTransients) ==
           (persistentControl | kTimeCardClockReadRequest));
    assert(TimeCardClockAdjustRequestControl(controlWithTransients) ==
           (persistentControl | kTimeCardClockAdjustTime));

    assert(TimeCardBoardProfileForDevice(0x1d9b, 0x0400) ==
           kTimeCardBoardFacebook);
    assert(TimeCardBoardProfileForDevice(0x18d4, 0x1008) ==
           kTimeCardBoardCelestica);
    assert(TimeCardBoardProfileForDevice(0x1ad7, 0xa000) ==
           kTimeCardBoardOroliaART);
    assert(TimeCardBoardProfileForDevice(0xad5a, 0x0400) ==
           kTimeCardBoardADVA);
    assert(TimeCardBoardProfileForDevice(0xad5a, 0x0410) ==
           kTimeCardBoardADVAX1);
    assert(TimeCardBoardProfileForDevice(0x1d9b, 0x0401) ==
           kTimeCardBoardUnknown);
    assert(TimeCardBoardProfileForDevice(0xffff, 0xffff) ==
           kTimeCardBoardUnknown);

    const TimeCardRegisterMap facebookMSI =
        TimeCardRegisterMapForDevice(0x1d9b, 0x0400, 0, 0);
    assert(facebookMSI.boardProfile == kTimeCardBoardFacebook);
    assert(facebookMSI.layout == kTimeCardLayoutClassic);
    assert(facebookMSI.clockOffset == 0x01000000u);
    assert(facebookMSI.todOffset == 0x01050000u);
    assert(facebookMSI.uartOffsets[0] == 0x00161000u);
    assert(TimeCardRegisterMapHasTOD(&facebookMSI));
    assert((facebookMSI.capabilities & kTimeCardCapabilityTOD) != 0);
    assert(TimeCardRegisterMapHasLED(&facebookMSI));
    assert((facebookMSI.capabilities & kTimeCardCapabilityLED) != 0);
    assert(facebookMSI.requiredBarSize >=
           facebookMSI.i2cOffset + kTimeCardI2CRegisterLength);
    assert(facebookMSI.requiredBarSize <
           facebookMSI.todOffset + kTimeCardTodUtcStatus + 4u);
    AssertExactBarBoundary(facebookMSI);
    AssertDriverAccessesFit(facebookMSI);

    const TimeCardRegisterMap facebookMSIX =
        TimeCardRegisterMapForDevice(0x1d9b, 0x0400, 2, 64);
    assert(facebookMSIX.layout == kTimeCardLayoutLitePCIe);
    assert(facebookMSIX.clockOffset == 0x03000000u);
    assert(facebookMSIX.todOffset == 0x03050000u);
    assert(facebookMSIX.uartOffsets[3] == 0x02191000u);
    AssertExactBarBoundary(facebookMSIX);
    AssertDriverAccessesFit(facebookMSIX);

    const TimeCardRegisterMap futureFacebook =
        TimeCardRegisterMapForDevice(0x1d9b, 0x0400, 3, 64);
    assert(futureFacebook.layout == kTimeCardLayoutUnknown);

    const TimeCardRegisterMap unreadableFacebook =
        TimeCardRegisterMapForDevice(0x1d9b, 0x0400, UINT8_MAX, 0);
    assert(unreadableFacebook.layout == kTimeCardLayoutUnknown);
    assert(!TimeCardRegisterMapFits(UINT64_MAX, &unreadableFacebook));

    const TimeCardRegisterMap shiftedWithoutMSIX =
        TimeCardRegisterMapForDevice(0x1d9b, 0x0400, 2, 0);
    const TimeCardRegisterMap classicWithMSIX =
        TimeCardRegisterMapForDevice(0x1d9b, 0x0400, 0, 64);
    const TimeCardRegisterMap unreadableMSIX =
        TimeCardRegisterMapForDevice(0x1d9b, 0x0400, 2, UINT32_MAX);
    assert(shiftedWithoutMSIX.layout == kTimeCardLayoutUnknown);
    assert(classicWithMSIX.layout == kTimeCardLayoutUnknown);
    assert(unreadableMSIX.layout == kTimeCardLayoutUnknown);

    const TimeCardRegisterMap celesticaMSI =
        TimeCardRegisterMapForDevice(0x18d4, 0x1008, 1, 0);
    const TimeCardRegisterMap celesticaMSIX =
        TimeCardRegisterMapForDevice(0x18d4, 0x1008, 2, 64);
    assert(celesticaMSI.boardProfile == kTimeCardBoardCelestica);
    assert(celesticaMSI.layout == kTimeCardLayoutClassic);
    assert(celesticaMSIX.layout == kTimeCardLayoutLitePCIe);
    AssertExactBarBoundary(celesticaMSI);
    AssertExactBarBoundary(celesticaMSIX);
    AssertDriverAccessesFit(celesticaMSI);
    AssertDriverAccessesFit(celesticaMSIX);

    const TimeCardRegisterMap art =
        TimeCardRegisterMapForDevice(0x1ad7, 0xa000, 2, 16);
    assert(art.boardProfile == kTimeCardBoardOroliaART);
    assert(art.layout == kTimeCardLayoutART);
    assert(art.clockOffset == 0x01000000u);
    assert(art.todOffset == 0);
    assert(!TimeCardRegisterMapHasTOD(&art));
    assert(!TimeCardRegisterMapHasLED(&art));
    assert((art.capabilities & kTimeCardCapabilityReadClock) != 0);
    assert((art.capabilities & kTimeCardCapabilityTOD) == 0);
    assert((art.capabilities & kTimeCardCapabilityLED) == 0);
    assert(art.uartOffsets[0] == 0x00161000u);
    assert(art.uartOffsets[1] == 0);
    assert(art.uartOffsets[2] == 0x00190000u);
    AssertExactBarBoundary(art);
    AssertDriverAccessesFit(art);

    const TimeCardRegisterMap adva =
        TimeCardRegisterMapForDevice(0xad5a, 0x0400, 7, 32);
    const TimeCardRegisterMap advaX1 =
        TimeCardRegisterMapForDevice(0xad5a, 0x0410, 7, 64);
    assert(adva.boardProfile == kTimeCardBoardADVA);
    assert(advaX1.boardProfile == kTimeCardBoardADVAX1);
    assert(adva.layout == kTimeCardLayoutClassic);
    assert(advaX1.layout == kTimeCardLayoutClassic);
    assert(adva.clockOffset == 0x01000000u);
    assert(adva.todOffset == 0x01050000u);
    assert(TimeCardRegisterMapHasTOD(&adva));
    assert(TimeCardRegisterMapHasLED(&adva));
    assert((adva.capabilities & kTimeCardCapabilityLED) != 0);
    assert(adva.uartOffsets[1] == 0);
    assert(adva.uartOffsets[2] == 0x00181000u);
    AssertExactBarBoundary(adva);
    AssertExactBarBoundary(advaX1);
    AssertDriverAccessesFit(adva);
    AssertDriverAccessesFit(advaX1);

    const TimeCardRegisterMap unknown =
        TimeCardRegisterMapForDevice(0x1234, 0x5678, 0, 0);
    assert(unknown.boardProfile == kTimeCardBoardUnknown);
    assert(unknown.layout == kTimeCardLayoutUnknown);
    assert(unknown.capabilities == 0);
    assert(!TimeCardRegisterMapFits(UINT64_MAX, &unknown));

    assert(std::strcmp(TimeCardBoardProfileName(kTimeCardBoardOroliaART),
                       "Orolia/Safran ART") == 0);
    assert(std::strcmp(
               TimeCardRegisterLayoutName(kTimeCardLayoutLitePCIe),
               "shifted LitePCIe map") == 0);
    return 0;
}
