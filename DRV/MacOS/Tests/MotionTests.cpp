/* SPDX-License-Identifier: BSD-3-Clause */
#include "TimeCardMotion.h"
#include <cassert>
#include <vector>
#include <cstdio>
struct MockIO {
    std::vector<std::vector<uint8_t>> reads;
    size_t index = 0;
    bool read(uint8_t *data, uint32_t count) {
        if (index >= reads.size() || reads[index].size() != count) return false;
        memcpy(data, reads[index++].data(), count); return true;
    }
};
static void word(uint8_t *p, int16_t value) { p[0] = value; p[1] = value >> 8; }
int main() {
    TimeCardIMURequest request = {sizeof(request), 0, {0,0}};
    for (uint32_t mode = 0; mode <= 2; ++mode) { request.mode = mode; assert(TimeCardMotionRequestValid(request)); }
    request.mode = 3; assert(!TimeCardMotionRequestValid(request)); request.mode = 0;
    request.size--; assert(!TimeCardMotionRequestValid(request)); request.size++;
    request.reserved[0] = 1; assert(!TimeCardMotionRequestValid(request)); request.reserved[0] = 0;
    request.reserved[1] = 1; assert(!TimeCardMotionRequestValid(request));
    uint8_t feature[21]; TimeCardMotionFeature(feature, 5, 255, 250000);
    assert(feature[0] == 21 && feature[2] == 2 && feature[3] == 255 && feature[4] == 0xfd && feature[5] == 5);
    assert(feature[9] == 0x90 && feature[10] == 0xd0 && feature[11] == 3);
    TimeCardMotionFeature(feature, 5, 0, 0); assert(feature[9] == 0 && feature[11] == 0);
    MockIO io{{{0x2c, 1, 3, 254}, std::vector<uint8_t>(255), std::vector<uint8_t>(49)}};
    io.reads[1][0] = 0x2c; io.reads[1][1] = 0x81; io.reads[1][2] = 3; io.reads[1][3] = 255;
    io.reads[2][0] = 49; io.reads[2][1] = 0x80; io.reads[2][2] = 3; io.reads[2][3] = 0;
    for (size_t i = 4; i < 255; ++i) io.reads[1][i] = 0xaa;
    for (size_t i = 4; i < 49; ++i) io.reads[2][i] = 0xbb;
    uint8_t cargo[1024] = {}; uint32_t length = 0; uint8_t channel = 0; bool resync = false;
    assert(TimeCardMotionReadCargo(io, cargo, length, channel, &resync));
    assert(length == 296 && channel == 3 && cargo[250] == 0xaa && cargo[251] == 0xbb && !resync);
    io.index = 0; io.reads[2][3] = 1;
    assert(!TimeCardMotionReadCargo(io, cargo, length, channel, &resync) && resync);
    io.index = 0; io.reads[1][1] = 1; resync = false;
    assert(!TimeCardMotionReadCargo(io, cargo, length, channel, &resync) && resync);
    MockIO empty{{{0,0,0,0}}}; assert(TimeCardMotionReadCargo(empty, cargo, length, channel) && length == 0);
    MockIO invalid{{{0,0,3,1}}}; assert(!TimeCardMotionReadCargo(invalid, cargo, length, channel));
    MockIO oversized{{{1,4,3,0}}}; assert(!TimeCardMotionReadCargo(oversized, cargo, length, channel));
    TimeCardIMUTelemetry out = {}; out.size = sizeof(out);
    uint8_t report[29] = {0xfb,0,0,0,0, 5,0,3,0};
    word(report + 15, 16384); // quaternion W after timestamp prefix
    report[19] = 4; word(report + 23, -256); word(report + 25, 512); word(report + 27, -768);
    assert(TimeCardMotionParse(out, report, sizeof(report)));
    assert(out.quaternionQ14[3] == 16384 && out.linearAccelerationQ8[0] == -256 && out.linearAccelerationQ8[2] == -768);
    assert(out.reportCount == 2 && out.calibration == 0xc0);
    uint8_t rebase[5] = {0xfa,0xff,0xff,0xff,0xff};
    assert(TimeCardMotionParse(out, rebase, sizeof(rebase)) && out.reportCount == 2);
    auto before = out;
    assert(!TimeCardMotionParse(out, report, sizeof(report) - 1)); assert(memcmp(&out, &before, sizeof(out)) == 0);
    report[19] = 0xfe; assert(!TimeCardMotionParse(out, report, sizeof(report))); assert(memcmp(&out, &before, sizeof(out)) == 0);
    uint8_t raw[45] = {}; word(raw, -100); word(raw + 12, 1440); word(raw + 24, 16384); word(raw + 32, 100); raw[44] = 25;
    TimeCardMotionBNO055(out, raw, 0x80);
    assert(out.accelerationQ8[0] == -256 && out.linearAccelerationQ8[0] == 256 && out.gyroscopeQ9[0] == 804);
    assert(out.quaternionQ14[3] == 16384 && out.temperatureQ7 == 3200 && out.reportCount == 7);
    word(raw, 1000); word(raw + 12, 900); raw[44] = 41;
    TimeCardMotionBNO055(out, raw, 0x13);
    assert(out.accelerationQ8[0] == 2511 && out.gyroscopeQ9[0] == 512 && out.temperatureQ7 == 3556);
    puts("Motion protocol tests passed: fragmentation, recovery, report validity, and unit conversion.");
}
