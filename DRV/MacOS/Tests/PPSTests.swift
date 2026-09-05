/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation
@main struct PPSTests {
    static func main() throws {
        precondition(MemoryLayout<TimeCardPPSState>.size == 48)
        precondition(MemoryLayout<TimeCardPPSState>.offset(of: \.cableDelayRaw) == 32)
        precondition(MemoryLayout<TimeCardPPSRequestRaw>.size == 64)
        precondition(MemoryLayout<TimeCardPPSRequestRaw>.offset(of: \.cableDelay) == 48)
        precondition(MemoryLayout<TimeCardPPSQueryRaw>.size == 16)
        var state = TimeCardPPSState(core: 1, version: 0x01060000, validFields: 31, control: 1, status: 0,
            polarity: 1, pulseWidth: 500, cableDelayRaw: 0x80000019, maximumDelay: 0x3fffffff, writableFields: 29)
        precondition(state.validLayout && state.delayNanoseconds == -25 && state.measuredWidth == 500)
        var settings = TimeCardPPSSettings(state)
        precondition(settings.matches(state))
        let request = try settings.request(baseline: state)
        precondition(request.size == 64 && request.fields == 29 && request.expectedDelay == 0x80000019 && request.cableDelay == -25)
        for bad in ["", "1.5", "-1", "0", "1000", "4294967296"] {
            settings.width = bad
            do { _ = try settings.request(baseline: state); fatalError("Accepted invalid width") } catch {}
        }
        settings = TimeCardPPSSettings(state)
        for bad in ["", "nan", "-9223372036854775808", "1073741824", "-1073741824"] {
            settings.delay = bad
            do { _ = try settings.request(baseline: state); fatalError("Accepted invalid delay") } catch {}
        }
        state.core = 2; state.writableFields = 21; state.status = 0x103; state.pulseWidth = 0x3ff
        precondition(state.validLayout && state.measuredWidth == nil && state.errors.count == 3)
        settings = TimeCardPPSSettings(state); settings.width = "not a writable field"
        let slave = try settings.request(baseline: state)
        precondition(slave.fields == 21 && slave.pulseWidth == 0)
        state.version = 0x01020000; state.validFields = 29; state.maximumDelay = 65535
        precondition(state.validLayout && state.errors.isEmpty)
        state = TimeCardPPSState(core: 1, version: 0x00010000)
        precondition(state.validLayout && state.validFields == 0 && state.delayNanoseconds == nil && state.measuredWidth == nil)
        do { _ = try TimeCardPPSSettings(state).request(baseline: state); fatalError("Unknown version writable") } catch {}
        state.validFields = 31
        precondition(!state.validLayout)
        print("PPS models passed: ABI layout, signed delay, version/field gates, invalid input, and unavailable measurements.")
    }
}
