#pragma once

#include <QtGlobal>

struct TimingDerivedValues {
    qint64 phcUtcNanoseconds;
    qint64 offsetNanoseconds;
};

enum class TaiOffsetSource {
    Unavailable,
    Kernel,
    TimeCard
};

struct TaiOffsetSelection {
    bool valid;
    int seconds;
    TaiOffsetSource source;
};

inline TaiOffsetSelection selectTaiOffset(
    bool kernelOffsetValid,
    int kernelOffsetSeconds,
    bool cardOffsetValid,
    int cardOffsetSeconds)
{
    constexpr int maximumPlausibleTaiOffsetSeconds = 255;
    if (kernelOffsetValid && kernelOffsetSeconds > 0
        && kernelOffsetSeconds <= maximumPlausibleTaiOffsetSeconds) {
        return {true, kernelOffsetSeconds, TaiOffsetSource::Kernel};
    }
    if (cardOffsetValid && cardOffsetSeconds > 0
        && cardOffsetSeconds <= maximumPlausibleTaiOffsetSeconds) {
        return {true, cardOffsetSeconds, TaiOffsetSource::TimeCard};
    }
    return {false, 0, TaiOffsetSource::Unavailable};
}

inline TimingDerivedValues deriveTaiAwareTiming(
    qint64 phcTaiNanoseconds,
    qint64 systemUtcNanoseconds,
    int utcTaiOffsetSeconds)
{
    constexpr qint64 nanosecondsPerSecond = 1'000'000'000LL;
    const qint64 taiOffset = utcTaiOffsetSeconds * nanosecondsPerSecond;
    return {
        phcTaiNanoseconds - taiOffset,
        phcTaiNanoseconds - (systemUtcNanoseconds + taiOffset)
    };
}
