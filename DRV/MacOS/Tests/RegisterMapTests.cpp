/* SPDX-License-Identifier: BSD-3-Clause */

#include <cassert>
#include <cstdint>

#include "TimeCardRegisters.h"

int main()
{
    assert(TimeCardMSIXVectorCount(0x0000) == 1);
    assert(TimeCardMSIXVectorCount(0x003f) == 64);
    assert(TimeCardMSIXVectorCount(0xffff) == 2048);

    const TimeCardRegisterMap msi =
        TimeCardRegisterMapForInterrupts(false, 1);
    assert(msi.layout == kTimeCardLayoutMSI);
    assert(msi.clockOffset == 0x01000000u);
    assert(msi.todOffset == 0x01050000u);
    assert(msi.uartOffsets[0] == 0x00161000u);
    assert(TimeCardRangeFits(msi.requiredBarSize, msi.todOffset,
                            kTimeCardTodSatellites + 4u));

    const TimeCardRegisterMap singleVectorMSIX =
        TimeCardRegisterMapForInterrupts(true, 1);
    assert(singleVectorMSIX.layout == kTimeCardLayoutMSI);

    const TimeCardRegisterMap msix =
        TimeCardRegisterMapForInterrupts(true, 64);
    assert(msix.layout == kTimeCardLayoutMSIX);
    assert(msix.clockOffset == 0x03000000u);
    assert(msix.todOffset == 0x03050000u);
    assert(msix.uartOffsets[3] == 0x02191000u);
    assert(!TimeCardRangeFits(msix.requiredBarSize - 1, msix.todOffset,
                             kTimeCardTodSatellites + 4u));
    assert(TimeCardRangeFits(msix.requiredBarSize, msix.todOffset,
                            kTimeCardTodSatellites + 4u));

    return 0;
}
