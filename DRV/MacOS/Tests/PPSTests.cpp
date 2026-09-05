/* SPDX-License-Identifier: BSD-3-Clause */
#include <cassert>
#include <map>
#include <vector>
#include "TimeCardPPS.h"
using R = TimeCardTimingResult;
struct PPSIO {
    std::map<uint64_t, uint32_t> registers;
    std::vector<uint64_t> reads;
    std::vector<std::pair<uint64_t, uint32_t>> writes;
    unsigned failRead = 0, ignoreWrite = 0;
    uint64_t brokenRegister = 0;
    bool read(uint64_t address, uint32_t *value) {
        reads.push_back(address);
        if (reads.size() == failRead || !registers.contains(address)) return false;
        *value = registers.at(address); return true;
    }
    void write(uint64_t address, uint32_t value) {
        writes.emplace_back(address, value);
        if (writes.size() == ignoreWrite) return;
        registers[address] = address == brokenRegister ? 0xdeadbeefu : value;
    }
};
static TimeCardPPSRequest requestFor(const TimeCardPPSState &state) {
    TimeCardPPSRequest r = {};
    r.size = sizeof(r); r.core = state.core; r.fields = state.writableFields;
    r.expectedVersion = state.version; r.expectedControl = state.control;
    r.expectedPolarity = state.polarity; r.expectedPulseWidth = state.pulseWidth; r.expectedDelay = state.cableDelayRaw;
    r.enabled = state.control & 1; r.polarity = state.polarity & 1; r.pulseWidth = state.pulseWidth & 0x3ff;
    r.cableDelay = int32_t(state.cableDelayRaw & state.maximumDelay) * (state.cableDelayRaw >> 31 ? -1 : 1);
    return r;
}
int main() {
    const auto map = TimeCardRegisterMapForDevice(0x1d9b, 0x0400, 0, 0);
    constexpr uint64_t bar = 0x02000000, base = 0x01030000;
    TimeCardPPSState out = {};
    PPSIO untouched;
    for (auto identity : {0xa0001ad7u, 0x0400ad5au, 0x0410ad5au, 0xffffffffu}) {
        auto unsupported = TimeCardRegisterMapForDevice(identity & 0xffff, identity >> 16, 0, 0);
        assert(TimeCardQueryPPS(untouched, unsupported, bar, 1, out) == R::unsupported);
    }
    assert(TimeCardQueryPPS(untouched, map, base + 0x23, 1, out) == R::unsupported);
    assert(TimeCardQueryPPS(untouched, map, bar, 0, out) == R::invalid);
    assert(TimeCardQueryPPS(untouched, map, bar, 3, out) == R::invalid);
    assert(untouched.reads.empty() && untouched.writes.empty());
    const auto lite = TimeCardRegisterMapForDevice(0x18d4, 0x1008, 2, 64);
    assert(TimeCardPPSOffset(&lite, 0x04000000, 2) == 0x03040000);
    for (uint32_t version : {0u, 0xffffffffu, 0x00010000u, 0x01070000u, 0x02000000u}) {
        PPSIO io; io.registers[base + 12] = version;
        assert(TimeCardQueryPPS(io, map, bar, 1, out) == R::success);
        assert(out.validFields == 0 && out.writableFields == 0 && out.version == version && io.reads.size() == 1);
        auto r = requestFor(out); r.fields = kTimeCardPPSControl;
        assert(TimeCardApplyPPS(io, map, bar, r, out) == R::unsupported && io.writes.empty());
    }
    for (uint32_t core = 1; core <= 2; ++core) {
        for (uint32_t minor = 0; minor <= 6; ++minor) {
            PPSIO io; const auto offset = base + (core - 1) * 0x10000;
            const auto version = 0x01000000u | minor << 16;
            const auto fields = TimeCardPPSFields(core, version);
            io.registers[offset + 12] = version; io.registers[offset] = 1; io.registers[offset + 16] = 500;
            if (fields & 2) io.registers[offset + 4] = 3;
            if (fields & 4) io.registers[offset + 8] = 1;
            if (fields & 16) io.registers[offset + 32] = 0x80000006;
            assert(TimeCardQueryPPS(io, map, bar, core, out) == R::success);
            assert(out.validFields == fields && (core == 1 || !(out.writableFields & 8)));
            assert(out.maximumDelay == (minor >= 6 ? 0x3fffffffu : 0xffffu));
            auto r = requestFor(out);
            assert(TimeCardApplyPPS(io, map, bar, r, out) == R::success && io.writes.empty());
        }
    }
    PPSIO original;
    original.registers = {{base, 0xa5000001}, {base + 4, 1}, {base + 8, 0x80000001},
                          {base + 12, 0x01060000}, {base + 16, 0x800001f4}, {base + 32, 0x40000006}};
    assert(TimeCardQueryPPS(original, map, bar, 1, out) == R::success);
    const auto baseline = out;
    auto r = requestFor(baseline); r.polarity = 0; r.pulseWidth = 25; r.cableDelay = -25;
    {
        auto io = original;
        assert(TimeCardApplyPPS(io, map, bar, r, out) == R::success);
        assert(out.polarity == 0x80000000 && out.pulseWidth == 0x80000019 && out.cableDelayRaw == 0xc0000019);
        assert(out.control == baseline.control && io.writes.front() == std::make_pair(base, 0xa5000000u));
        assert(io.writes.back() == std::make_pair(base, baseline.control));
        assert(io.registers[base + 4] == 1); // Status is never cleared by configuration.
        for (const auto &write : io.writes) assert(write.first == base || write.first == base + 8 || write.first == base + 16 || write.first == base + 32);
    }
    for (unsigned field = 0; field < 5; ++field) {
        auto io = original; auto stale = r;
        uint32_t *values[] = {&stale.expectedVersion, &stale.expectedControl, &stale.expectedPolarity, &stale.expectedPulseWidth, &stale.expectedDelay};
        ++*values[field];
        assert(TimeCardApplyPPS(io, map, bar, stale, out) == R::stale && io.writes.empty());
    }
    for (unsigned index = 0; index < 7; ++index) {
        auto io = original; auto invalid = r;
        if (index == 0) invalid.size = 0;
        if (index == 1) invalid.reserved[2] = 1;
        if (index == 2) invalid.fields = 2;
        if (index == 3) invalid.enabled = 2;
        if (index == 4) invalid.polarity = 2;
        if (index == 5) invalid.pulseWidth = 0;
        if (index == 6) invalid.pulseWidth = 1000;
        const auto readCount = io.reads.size();
        assert(TimeCardApplyPPS(io, map, bar, invalid, out) == R::invalid);
        assert(io.reads.size() == readCount && io.writes.empty());
    }
    for (int32_t delay : {INT32_MIN, INT32_MAX, -1073741824}) {
        auto io = original; auto invalid = r; invalid.cableDelay = delay;
        assert(TimeCardApplyPPS(io, map, bar, invalid, out) == R::invalid && io.writes.empty());
    }
    for (unsigned write = 1; write <= 5; ++write) {
        auto io = original; io.ignoreWrite = write;
        assert(TimeCardApplyPPS(io, map, bar, r, out) == R::verifyFailed);
        assert(io.registers == original.registers);
    }
    for (unsigned read = 1; read <= 22; ++read) {
        auto io = original; io.failRead = unsigned(io.reads.size()) + read;
        const auto result = TimeCardApplyPPS(io, map, bar, r, out);
        if (result == R::readFailed) assert(io.writes.empty());
        else if (result == R::verifyFailed) assert(io.registers == original.registers);
        else assert(result == R::success || result == R::rollbackFailed);
    }
    {
        auto io = original; io.brokenRegister = base + 8;
        assert(TimeCardApplyPPS(io, map, bar, r, out) == R::rollbackFailed);
        assert((io.registers[base] & 1) == 0); // Fail safely without re-enabling bad parameters.
    }
    {
        auto io = original; io.registers[base + 12] = 0x01040000;
        assert(TimeCardQueryPPS(io, map, bar, 1, out) == R::success);
        auto legacy = requestFor(out); legacy.cableDelay = -65536;
        assert(TimeCardApplyPPS(io, map, bar, legacy, out) == R::invalid && io.writes.empty());
    }
    {
        constexpr auto input = base + 0x10000;
        PPSIO io;
        io.registers = {{input, 1}, {input + 4, 0}, {input + 8, 1},
                        {input + 12, 0x01060000}, {input + 16, 500}, {input + 32, 6}};
        assert(TimeCardQueryPPS(io, map, bar, 2, out) == R::success);
        auto update = requestFor(out); update.cableDelay = 7;
        // Live input measurements and latched status can change while the
        // editor is open. Neither is a stale configuration or writable field.
        io.registers[input + 16] = 80;
        io.registers[input + 4] = 3;
        assert(TimeCardApplyPPS(io, map, bar, update, out) == R::success);
        assert(out.pulseWidth == 80 && out.status == 3 && out.cableDelayRaw == 7 && out.control == 1);
        for (const auto &write : io.writes) assert(write.first != input + 16 && write.first != input + 4);
        auto forbidden = requestFor(out); forbidden.fields |= kTimeCardPPSWidth;
        const auto writes = io.writes.size();
        assert(TimeCardApplyPPS(io, map, bar, forbidden, out) == R::unsupported && io.writes.size() == writes);
    }
}
