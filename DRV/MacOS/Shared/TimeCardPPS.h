/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef TIMECARD_PPS_H
#define TIMECARD_PPS_H
#include "TimeCardTiming.h"

// NetTimeLogic PPS 1.0 through 1.6 register contract, as implemented by the
// Windows driver. Open-source 0.1 has different synthesis/width semantics.
// Unknown versions expose their version only, never guessed settings.
static inline uint32_t TimeCardPPSFields(uint32_t core, uint32_t version) {
    if (core < 1 || core > 2 || version < 0x01000000u || version >= 0x01070000u) return 0;
    uint32_t fields = kTimeCardPPSControl | kTimeCardPPSWidth;
    if (version >= (core == 1 ? 0x01010000u : 0x01020000u)) fields |= kTimeCardPPSPolarity;
    if (version >= (core == 1 ? 0x01020000u : 0x01030000u)) fields |= kTimeCardPPSStatus;
    if (core == 2 || version >= 0x01040000u) fields |= kTimeCardPPSDelay;
    return fields;
}

template <typename IO>
TimeCardTimingResult TimeCardQueryPPS(IO &io, const TimeCardRegisterMap &map,
                                     uint64_t barSize, uint32_t core, TimeCardPPSState &out) {
    out = {}; out.size = sizeof(out); out.core = core;
    if (core < 1 || core > 2) return TimeCardTimingResult::invalid;
    const auto base = TimeCardPPSOffset(&map, barSize, core);
    if (!base) return TimeCardTimingResult::unsupported;
    if (!io.read(base + 0x0c, &out.version)) return TimeCardTimingResult::readFailed;
    out.validFields = TimeCardPPSFields(core, out.version);
    if (!out.validFields) return TimeCardTimingResult::success;
    const uint32_t fields[] = {kTimeCardPPSControl, kTimeCardPPSStatus, kTimeCardPPSPolarity, kTimeCardPPSWidth, kTimeCardPPSDelay};
    const uint32_t offsets[] = {0, 4, 8, 0x10, 0x20};
    uint32_t *values[] = {&out.control, &out.status, &out.polarity, &out.pulseWidth, &out.cableDelayRaw};
    for (unsigned i = 0; i < 5; ++i)
        if ((out.validFields & fields[i]) && !io.read(base + offsets[i], values[i])) return TimeCardTimingResult::readFailed;
    out.maximumDelay = out.version >= 0x01060000u ? 0x3fffffffu : 0xffffu;
    out.writableFields = out.validFields & ~kTimeCardPPSStatus;
    if (core == 2) out.writableFields &= ~kTimeCardPPSWidth;
    return TimeCardTimingResult::success;
}

template <typename IO>
TimeCardTimingResult TimeCardApplyPPS(IO &io, const TimeCardRegisterMap &map,
                                     uint64_t barSize, const TimeCardPPSRequest &request,
                                     TimeCardPPSState &out) {
    constexpr uint32_t allowed = kTimeCardPPSControl | kTimeCardPPSPolarity | kTimeCardPPSWidth | kTimeCardPPSDelay;
    if (request.size != sizeof(request) || request.reserved0 || request.reserved[0] || request.reserved[1] || request.reserved[2] ||
        !request.fields || (request.fields & ~allowed) || request.enabled > 1 || request.polarity > 1 ||
        ((request.fields & kTimeCardPPSWidth) && (request.pulseWidth < 1 || request.pulseWidth > 999))) return TimeCardTimingResult::invalid;
    auto result = TimeCardQueryPPS(io, map, barSize, request.core, out);
    if (result != TimeCardTimingResult::success) return result;
    if ((request.fields & out.writableFields) != request.fields) return TimeCardTimingResult::unsupported;
    // Compare raw persistent registers, including reserved bits. The measured
    // input pulse width and transient status are deliberately not compared.
    if (request.expectedVersion != out.version || request.expectedControl != out.control ||
        ((out.validFields & kTimeCardPPSPolarity) && request.expectedPolarity != out.polarity) ||
        ((out.writableFields & kTimeCardPPSWidth) && request.expectedPulseWidth != out.pulseWidth) ||
        ((out.validFields & kTimeCardPPSDelay) && request.expectedDelay != out.cableDelayRaw)) return TimeCardTimingResult::stale;
    const int64_t delay = request.cableDelay;
    if ((request.fields & kTimeCardPPSDelay) && (delay < -int64_t(out.maximumDelay) || delay > out.maximumDelay)) return TimeCardTimingResult::invalid;
    const auto old = out;
    auto desired = old;
    if (request.fields & kTimeCardPPSControl) desired.control = (old.control & ~1u) | request.enabled;
    if (request.fields & kTimeCardPPSPolarity) desired.polarity = (old.polarity & ~1u) | request.polarity;
    if (request.fields & kTimeCardPPSWidth) desired.pulseWidth = (old.pulseWidth & ~0x3ffu) | request.pulseWidth;
    if (request.fields & kTimeCardPPSDelay) desired.cableDelayRaw = (old.cableDelayRaw & ~(0x80000000u | old.maximumDelay)) |
        (delay < 0 ? 0x80000000u : 0) | uint32_t(delay < 0 ? -delay : delay);
    if (desired.control == old.control && desired.polarity == old.polarity && desired.pulseWidth == old.pulseWidth && desired.cableDelayRaw == old.cableDelayRaw)
        return TimeCardTimingResult::success;
    const auto base = TimeCardPPSOffset(&map, barSize, request.core);
    const uint32_t fields[] = {kTimeCardPPSPolarity, kTimeCardPPSWidth, kTimeCardPPSDelay};
    const uint32_t offsets[] = {8, 0x10, 0x20};
    const uint32_t before[] = {old.polarity, old.pulseWidth, old.cableDelayRaw};
    const uint32_t after[] = {desired.polarity, desired.pulseWidth, desired.cableDelayRaw};
    auto parametersMatch = [&](const uint32_t *values) {
        for (unsigned i = 0; i < 3; ++i) {
            uint32_t readback = 0;
            if ((request.fields & fields[i]) && (!io.read(base + offsets[i], &readback) || readback != values[i])) return false;
        }
        return true;
    };
    auto controlMatches = [&](uint32_t value) { uint32_t readback = 0; return io.read(base, &readback) && readback == value; };
    // Never alter parameters until disabling the engine is verified.
    io.write(base, old.control & ~1u);
    bool verified = controlMatches(old.control & ~1u);
    if (verified) {
        for (unsigned i = 0; i < 3; ++i) if (request.fields & fields[i]) io.write(base + offsets[i], after[i]);
        verified = parametersMatch(after);
    }
    if (verified) {
        io.write(base, desired.control);
        verified = controlMatches(desired.control);
    }
    if (verified) {
        result = TimeCardQueryPPS(io, map, barSize, request.core, out);
        if (result == TimeCardTimingResult::success && out.version == old.version && out.control == desired.control &&
            out.polarity == desired.polarity && out.cableDelayRaw == desired.cableDelayRaw &&
            (request.core == 2 || out.pulseWidth == desired.pulseWidth)) return result;
    }
    // Restore parameters while disabled. Never deliberately re-enable an
    // engine whose restored parameters could not be verified.
    io.write(base, old.control & ~1u);
    if (!controlMatches(old.control & ~1u)) return TimeCardTimingResult::rollbackFailed;
    for (unsigned i = 0; i < 3; ++i) if (request.fields & fields[i]) io.write(base + offsets[i], before[i]);
    if (!parametersMatch(before)) return TimeCardTimingResult::rollbackFailed;
    io.write(base, old.control);
    return controlMatches(old.control) ? TimeCardTimingResult::verifyFailed : TimeCardTimingResult::rollbackFailed;
}
#endif
