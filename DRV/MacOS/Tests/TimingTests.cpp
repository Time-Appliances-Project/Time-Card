/* SPDX-License-Identifier: BSD-3-Clause */
#include <cassert>
#include <map>
#include <vector>
#include "TimeCardTiming.h"

using Result = TimeCardTimingResult;
struct MockIO {
    std::map<uint64_t, uint32_t> registers;
    std::vector<std::pair<uint64_t, uint32_t>> writes;
    unsigned reads = 0;
    unsigned failRead = 0;
    bool rejectFirstWrite = false;
    bool corruptWrites = false;
    bool read(uint64_t address, uint32_t *value) {
        ++reads;
        if (reads == failRead || !registers.contains(address)) return false;
        *value = registers.at(address);
        return true;
    }
    void write(uint64_t address, uint32_t value) {
        writes.emplace_back(address, value);
        if (rejectFirstWrite && writes.size() == 1) return;
        registers[address] = corruptWrites ? 0xdeadbeefu : value;
    }
};

int main() {
    const auto classic = TimeCardRegisterMapForDevice(0x1d9b, 0x0400, 0, 0);
    const auto lite = TimeCardRegisterMapForDevice(0x1d9b, 0x0400, 2, 64);
    const auto celestica = TimeCardRegisterMapForDevice(0x18d4, 0x1008, 2, 64);
    const auto art = TimeCardRegisterMapForDevice(0x1ad7, 0xa000, 0, 0);
    const auto adva = TimeCardRegisterMapForDevice(0xad5a, 0x0400, 0, 0);
    const auto unknown = TimeCardRegisterMapForDevice(0xffff, 0xffff, 0, 0);
    constexpr uint64_t barSize = 0x04000000;
    TimeCardClockControl clock = {};
    TimeCardFrequencyControl frequency = {};
    MockIO absent;
    assert(TimeCardQueryClockControl(absent, unknown, clock) == Result::unsupported);
    for (const auto &map : {classic, art, adva, unknown}) {
        assert(TimeCardQueryFrequency(absent, map, barSize, 1, frequency) == Result::unsupported);
        TimeCardFrequencyRequest request = {sizeof(request), 1, 1, 0};
        assert(TimeCardApplyFrequency(absent, map, barSize, request, frequency) == Result::unsupported);
    }
    assert(absent.reads == 0 && absent.writes.empty());
    for (unsigned counter = 1; counter <= 4; ++counter) {
        const uint64_t offset = 0x03200000 + (counter - 1) * 0x10000;
        assert(TimeCardFrequencyOffset(&lite, offset + 8, counter) == offset);
        assert(TimeCardFrequencyOffset(&celestica, barSize, counter) == offset);
        assert(TimeCardFrequencyOffset(&lite, offset + 7, counter) == 0);
    }
    assert(TimeCardFrequencyOffset(&lite, barSize, 0) == 0);
    assert(TimeCardFrequencyOffset(&lite, barSize, 5) == 0);
    assert(TimeCardClockSourceMask(&classic, 0x01010000) == 0);
    assert(TimeCardClockSourceMask(&classic, 0x03000000) == 0);
    assert(TimeCardClockSourceMask(&classic, 0x01020000) == 0xc000003f);
    assert(TimeCardClockSourceMask(&classic, 0x01080000) == 0xc000007f);
    assert((TimeCardClockSourceMask(&art, 0x01080000) & 2) == 0);
    for (unsigned source : {7u, 8u, 0xfdu, 0x100u})
        assert(TimeCardClockSourceBit(source) == 0);

    MockIO base;
    base.registers = {{classic.clockOffset, 1}, {classic.clockOffset + 4, 1},
                      {classic.clockOffset + 8, 0x00030003},
                      {classic.clockOffset + 12, 0x01020000}};
    assert(TimeCardQueryClockControl(base, classic, clock) == Result::success);
    assert(clock.size == 32 && clock.source == 3 && clock.activeSource == 3);
    TimeCardClockSourceRequest request = {sizeof(request), 3, 3, 0};
    assert(TimeCardApplyClockSource(base, classic, request, clock) == Result::success);
    assert(base.writes.empty()); // Idempotent requests do not touch the clock.
    request.expectedSource = 1;
    assert(TimeCardApplyClockSource(base, classic, request, clock) == Result::stale);
    assert(base.writes.empty());
    request.expectedSource = 3;
    request.source = 6;
    assert(TimeCardApplyClockSource(base, classic, request, clock) == Result::unsupported);
    assert(base.writes.empty());
    request.source = 7;
    auto reads = base.reads;
    assert(TimeCardApplyClockSource(base, classic, request, clock) == Result::invalid);
    assert(base.reads == reads);
    request.source = 1;
    request.reserved = 1;
    assert(TimeCardApplyClockSource(base, classic, request, clock) == Result::invalid);
    request.reserved = 0;
    request.size = 0;
    assert(TimeCardApplyClockSource(base, classic, request, clock) == Result::invalid);
    request.size = sizeof(request);
    {
        auto io = base;
        assert(TimeCardApplyClockSource(io, classic, request, clock) == Result::success);
        assert(io.writes.size() == 1 && io.writes[0].first == classic.clockOffset + 8);
        assert(io.writes[0].second == 1 && clock.source == 1);
    }
    {
        auto io = base; io.rejectFirstWrite = true;
        assert(TimeCardApplyClockSource(io, classic, request, clock) == Result::verifyFailed);
        assert(io.writes.size() == 2 && io.writes.back().second == 3);
    }
    {
        auto io = base; io.corruptWrites = true;
        assert(TimeCardApplyClockSource(io, classic, request, clock) == Result::rollbackFailed);
    }
    {
        auto io = base; io.failRead = io.reads + 5;
        assert(TimeCardApplyClockSource(io, classic, request, clock) == Result::verifyFailed);
        assert(io.writes.back().second == 3);
    }
    {
        auto io = base; io.failRead = io.reads + 1;
        assert(TimeCardApplyClockSource(io, classic, request, clock) == Result::readFailed);
        assert(io.writes.empty());
    }

    MockIO counters;
    for (unsigned counter = 1; counter <= 4; ++counter) {
        const auto offset = TimeCardFrequencyOffset(&lite, barSize, counter);
        counters.registers[offset] = 0x101;
        counters.registers[offset + 4] = 0x80000000u | 10000000u;
        assert(TimeCardQueryFrequency(counters, lite, barSize, counter, frequency) == Result::success);
        assert(frequency.counter == counter && frequency.integrationSeconds == 1);
        assert(frequency.frequencyHz == 10000000 && frequency.flags == 7);
    }
    const auto offset = TimeCardFrequencyOffset(&lite, barSize, 1);
    counters.registers[offset + 4] = 0xe0ffffff;
    assert(TimeCardQueryFrequency(counters, lite, barSize, 1, frequency) == Result::success);
    assert(frequency.flags == 31 && frequency.frequencyHz == 0xffffff);
    TimeCardFrequencyRequest freqRequest = {sizeof(freqRequest), 1, 255, 0x101};
    {
        auto io = counters;
        assert(TimeCardApplyFrequency(io, lite, barSize, freqRequest, frequency) == Result::success);
        assert(io.writes.size() == 1 && io.writes.back().second == 0xff01);
        freqRequest.integrationSeconds = 0; freqRequest.expectedControl = 0xff01;
        assert(TimeCardApplyFrequency(io, lite, barSize, freqRequest, frequency) == Result::success);
        assert(io.writes.back().second == 0 && !(frequency.flags & kTimeCardFrequencyEnabled));
    }
    freqRequest.integrationSeconds = 1; freqRequest.expectedControl = 0x101;
    assert(TimeCardApplyFrequency(counters, lite, barSize, freqRequest, frequency) == Result::success);
    assert(counters.writes.empty());
    freqRequest.expectedControl = 0;
    assert(TimeCardApplyFrequency(counters, lite, barSize, freqRequest, frequency) == Result::stale);
    freqRequest.expectedControl = 0x101; freqRequest.integrationSeconds = 256;
    reads = counters.reads;
    assert(TimeCardApplyFrequency(counters, lite, barSize, freqRequest, frequency) == Result::invalid);
    assert(counters.reads == reads);
    freqRequest.integrationSeconds = 2;
    {
        auto io = counters; io.rejectFirstWrite = true;
        assert(TimeCardApplyFrequency(io, lite, barSize, freqRequest, frequency) == Result::verifyFailed);
        assert(io.writes.size() == 2 && io.writes.back().second == 0x101);
    }
    {
        auto io = counters; io.corruptWrites = true;
        assert(TimeCardApplyFrequency(io, lite, barSize, freqRequest, frequency) == Result::rollbackFailed);
    }
    {
        auto io = counters; io.failRead = io.reads + 3;
        assert(TimeCardApplyFrequency(io, lite, barSize, freqRequest, frequency) == Result::verifyFailed);
    }
    counters.registers[offset] = 0xffffffff;
    assert(TimeCardQueryFrequency(counters, lite, barSize, 1, frequency) == Result::unsupported);
    assert(TimeCardQueryFrequency(counters, lite, barSize, 5, frequency) == Result::invalid);
    assert(counters.writes.empty());
}
