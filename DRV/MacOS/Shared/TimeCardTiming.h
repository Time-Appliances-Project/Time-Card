/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef TIMECARD_TIMING_H
#define TIMECARD_TIMING_H

#include "TimeCardRegisters.h"

enum class TimeCardTimingResult {
    success, invalid, unsupported, readFailed, stale, verifyFailed, rollbackFailed
};

/* IO supplies read(offset, uint32_t*) and write(offset, value).
 * Callers serialize the entire transaction. Tests exercise these same paths.
 */
template <typename IO>
TimeCardTimingResult TimeCardQueryClockControl(IO &io, const TimeCardRegisterMap &map,
                                               TimeCardClockControl &response)
{
    response = {};
    response.size = sizeof(response);
    if (map.layout == kTimeCardLayoutUnknown || map.clockOffset == 0)
        return TimeCardTimingResult::unsupported;
    uint32_t select = 0;
    if (!io.read(map.clockOffset + kTimeCardClockVersion, &response.clockVersion))
        return TimeCardTimingResult::readFailed;
    response.supportedSources = TimeCardClockSourceMask(&map, response.clockVersion);
    if (response.supportedSources == 0) return TimeCardTimingResult::unsupported;
    if (!io.read(map.clockOffset + kTimeCardClockControl, &response.control) ||
        !io.read(map.clockOffset + kTimeCardClockStatus, &response.status) ||
        !io.read(map.clockOffset + kTimeCardClockSelect, &select))
        return TimeCardTimingResult::readFailed;
    response.source = select & 0xffu;
    response.activeSource = (select >> 16) & 0xffu;
    return TimeCardTimingResult::success;
}

template <typename IO>
TimeCardTimingResult TimeCardApplyClockSource(IO &io, const TimeCardRegisterMap &map,
                                              const TimeCardClockSourceRequest &request,
                                              TimeCardClockControl &response)
{
    if (request.size != sizeof(request) || request.reserved != 0 ||
        request.expectedSource > 0xffu || TimeCardClockSourceBit(request.source) == 0)
        return TimeCardTimingResult::invalid;
    auto result = TimeCardQueryClockControl(io, map, response);
    if (result != TimeCardTimingResult::success) return result;
    if ((response.supportedSources & TimeCardClockSourceBit(request.source)) == 0)
        return TimeCardTimingResult::unsupported;
    if (response.source != request.expectedSource) return TimeCardTimingResult::stale;
    if (response.source == request.source) return TimeCardTimingResult::success;
    const uint32_t previous = response.source;
    const uint64_t offset = map.clockOffset + kTimeCardClockSelect;
    io.write(offset, request.source);
    result = TimeCardQueryClockControl(io, map, response);
    if (result == TimeCardTimingResult::success && response.source == request.source)
        return result;
    io.write(offset, previous);
    uint32_t restored = 0;
    if (!io.read(offset, &restored) || (restored & 0xffu) != previous)
        return TimeCardTimingResult::rollbackFailed;
    return TimeCardTimingResult::verifyFailed;
}

template <typename IO>
TimeCardTimingResult TimeCardQueryFrequency(IO &io, const TimeCardRegisterMap &map,
                                            uint64_t barSize, uint32_t counter,
                                            TimeCardFrequencyControl &response)
{
    if (counter < 1 || counter > TIMECARD_FREQUENCY_COUNT)
        return TimeCardTimingResult::invalid;
    const uint64_t offset = TimeCardFrequencyOffset(&map, barSize, counter);
    if (offset == 0) return TimeCardTimingResult::unsupported;
    response = {};
    response.size = sizeof(response);
    response.counter = counter;
    if (!io.read(offset, &response.control) || !io.read(offset + 4, &response.status))
        return TimeCardTimingResult::readFailed;
    if ((response.control & ~0xff01u) != 0) return TimeCardTimingResult::unsupported;
    response.flags = kTimeCardFrequencyPresent;
    if (response.control & 1u) {
        response.flags |= kTimeCardFrequencyEnabled;
        response.integrationSeconds = (response.control >> 8) & 0xffu;
    }
    if (response.status & (1u << 31)) {
        response.flags |= kTimeCardFrequencyValid;
        response.frequencyHz = response.status & 0xffffffu;
    }
    if (response.status & (1u << 30)) response.flags |= kTimeCardFrequencyError;
    if (response.status & (1u << 29)) response.flags |= kTimeCardFrequencyOverrun;
    return TimeCardTimingResult::success;
}

template <typename IO>
TimeCardTimingResult TimeCardApplyFrequency(IO &io, const TimeCardRegisterMap &map,
                                            uint64_t barSize,
                                            const TimeCardFrequencyRequest &request,
                                            TimeCardFrequencyControl &response)
{
    if (request.size != sizeof(request) || request.integrationSeconds > 255u ||
        (request.expectedControl & ~0xff01u) != 0)
        return TimeCardTimingResult::invalid;
    auto result = TimeCardQueryFrequency(io, map, barSize, request.counter, response);
    if (result != TimeCardTimingResult::success) return result;
    if (response.control != request.expectedControl) return TimeCardTimingResult::stale;
    const uint32_t previous = response.control;
    const uint32_t desired = request.integrationSeconds == 0 ? 0 :
        (request.integrationSeconds << 8) | 1u;
    if (previous == desired) return TimeCardTimingResult::success;
    const uint64_t offset = TimeCardFrequencyOffset(&map, barSize, request.counter);
    io.write(offset, desired);
    result = TimeCardQueryFrequency(io, map, barSize, request.counter, response);
    if (result == TimeCardTimingResult::success && response.control == desired)
        return result;
    io.write(offset, previous);
    uint32_t restored = 0;
    if (!io.read(offset, &restored) || restored != previous)
        return TimeCardTimingResult::rollbackFailed;
    return TimeCardTimingResult::verifyFailed;
}
#endif
