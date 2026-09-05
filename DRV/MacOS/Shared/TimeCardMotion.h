/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef TIMECARD_MOTION_H
#define TIMECARD_MOTION_H
#include "TimeCardABI.h"
#include <string.h>

static inline bool TimeCardMotionRequestValid(const TimeCardIMURequest &request) {
    return request.size == sizeof(request) && request.mode <= 2 &&
        request.reserved[0] == 0 && request.reserved[1] == 0;
}

static inline int32_t TimeCardMotionLE16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t TimeCardMotionLength(const uint8_t *p) {
    return p[0] | ((uint32_t)(p[1] & 0x7f) << 8);
}

/* CEVA SHTP 1000-3535: each partial I2C read repeats the header, reduces
 * remaining length, and increments its sequence. Never decode fragments. */
template <typename IO>
bool TimeCardMotionReadCargo(IO &io, uint8_t *cargo, uint32_t &length, uint8_t &channel, bool *resync = nullptr) {
    uint8_t header[4] = {};
    length = 0;
    if (!io.read(header, 4)) return false;
    const uint32_t total = TimeCardMotionLength(header);
    if (total < 4 || total > 1024 || header[2] > 5) return false;
    channel = header[2];
    uint8_t sequence = header[3];
    uint32_t remaining = total - 4;
    while (remaining != 0) {
        uint8_t packet[255] = {};
        const uint32_t count = remaining > 251 ? 251 : remaining;
        if (!io.read(packet, count + 4)) { if (resync) *resync = true; return false; }
        if (TimeCardMotionLength(packet) != remaining + 4 || packet[2] != channel ||
            (packet[1] & 0x80) == 0 || packet[3] != (uint8_t)(sequence + 1)) {
            if (resync) *resync = true;
            return false;
        }
        sequence = packet[3];
        memcpy(cargo + length, packet + 4, count);
        length += count;
        remaining -= count;
    }
    return true;
}

static inline void TimeCardMotionFeature(uint8_t packet[21], uint8_t report, uint8_t sequence, uint32_t interval) {
    memset(packet, 0, 21);
    packet[0] = 21; packet[2] = 2; packet[3] = sequence;
    packet[4] = 0xfd; packet[5] = report;
    for (uint32_t i = 0; i < 4; ++i) packet[9 + i] = (uint8_t)(interval >> (8 * i));
}

/* Decode one complete SH-2 cargo transactionally. A malformed or unsupported
 * tail cannot promote partial/default axes to a valid measurement. */
static inline bool TimeCardMotionParse(TimeCardIMUTelemetry &reading, const uint8_t *data, uint32_t length) {
    auto parsed = reading;
    for (uint32_t cursor = 0; cursor < length;) {
        const uint8_t id = data[cursor];
        const uint32_t count = (id == 0xfb || id == 0xfa) ? 5 : id == 5 ? 14 : id == 0x0e ? 6 :
            (id >= 1 && id <= 6) ? 10 : 0;
        if (!count || count > length - cursor) return false;
        const uint8_t *p = data + cursor;
        if (id != 0xfb && id != 0xfa) {
            const uint32_t accuracy = p[2] & 3;
            int32_t *vector = nullptr;
            if (id == 1) { vector = parsed.accelerationQ8; parsed.flags |= kTimeCardIMUAcceleration; parsed.calibration = (parsed.calibration & ~(3u << 2)) | (accuracy << 2); }
            if (id == 2) { vector = parsed.gyroscopeQ9; parsed.flags |= kTimeCardIMUGyroscope; parsed.calibration = (parsed.calibration & ~(3u << 4)) | (accuracy << 4); }
            if (id == 3) { vector = parsed.magneticQ4; parsed.flags |= kTimeCardIMUMagnetic; parsed.calibration = (parsed.calibration & ~3u) | accuracy; }
            if (id == 4) { vector = parsed.linearAccelerationQ8; parsed.flags |= kTimeCardIMULinearAcceleration; }
            if (id == 6) { vector = parsed.gravityQ8; parsed.flags |= kTimeCardIMUGravity; }
            if (vector) for (uint32_t axis = 0; axis < 3; ++axis) vector[axis] = TimeCardMotionLE16(p + 4 + 2 * axis);
            if (id == 5) {
                for (uint32_t axis = 0; axis < 4; ++axis) parsed.quaternionQ14[axis] = TimeCardMotionLE16(p + 4 + 2 * axis);
                parsed.flags |= kTimeCardIMURotation;
                parsed.calibration = (parsed.calibration & ~(3u << 6)) | (accuracy << 6);
            }
            if (id == 0x0e) { parsed.temperatureQ7 = TimeCardMotionLE16(p + 4); parsed.flags |= kTimeCardIMUTemperature; }
            ++parsed.reportCount;
        }
        cursor += count;
    }
    reading = parsed;
    return true;
}

static inline int32_t TimeCardMotionScale(int32_t value, int32_t numerator, int32_t denominator) {
    const int64_t product = (int64_t)value * numerator;
    return (int32_t)((product + (product >= 0 ? denominator / 2 : -denominator / 2)) / denominator);
}
static inline void TimeCardMotionBNO055(TimeCardIMUTelemetry &out, const uint8_t data[45], uint8_t units) {
    for (uint32_t i = 0; i < 3; ++i) {
        const int32_t numerator = (units & 1) ? 251053 : 256;
        const int32_t denominator = (units & 1) ? 100000 : 100;
        out.accelerationQ8[i] = TimeCardMotionScale(TimeCardMotionLE16(data + 2 * i), numerator, denominator);
        out.linearAccelerationQ8[i] = TimeCardMotionScale(TimeCardMotionLE16(data + 32 + 2 * i), numerator, denominator);
        out.gravityQ8[i] = TimeCardMotionScale(TimeCardMotionLE16(data + 38 + 2 * i), numerator, denominator);
        out.gyroscopeQ9[i] = (units & 2) ? TimeCardMotionScale(TimeCardMotionLE16(data + 12 + 2 * i), 512, 900) :
            TimeCardMotionScale(TimeCardMotionLE16(data + 12 + 2 * i), 558505, 1000000);
        out.magneticQ4[i] = TimeCardMotionLE16(data + 6 + 2 * i);
    }
    out.quaternionQ14[3] = TimeCardMotionLE16(data + 24);
    for (uint32_t i = 0; i < 3; ++i) out.quaternionQ14[i] = TimeCardMotionLE16(data + 26 + 2 * i);
    out.temperatureQ7 = (units & 0x10) ? TimeCardMotionScale((int8_t)data[44] * 2 - 32, 640, 9) : (int8_t)data[44] * 128;
    out.flags |= kTimeCardIMURotation | kTimeCardIMUAcceleration | kTimeCardIMULinearAcceleration |
        kTimeCardIMUGravity | kTimeCardIMUGyroscope | kTimeCardIMUMagnetic | kTimeCardIMUTemperature;
    out.reportCount = 7;
}
#endif
