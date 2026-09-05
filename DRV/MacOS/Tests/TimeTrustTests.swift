/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

@main enum TimeTrustTests {
    static func put32(_ n: UInt32, _ p: inout [UInt8], _ at: Int) { for i in 0..<4 { p[at + i] = UInt8(truncatingIfNeeded: n >> (i * 8)) } }
    static func put16(_ n: UInt16, _ p: inout [UInt8], _ at: Int) { p[at] = UInt8(truncatingIfNeeded: n); p[at + 1] = UInt8(truncatingIfNeeded: n >> 8) }
    static func packet(_ id: UInt8, _ p: [UInt8]) -> [UInt8] {
        var bytes: [UInt8] = [1, id, UInt8(truncatingIfNeeded: p.count), UInt8(p.count >> 8)] + p
        var a: UInt8 = 0, b: UInt8 = 0; for n in bytes { a &+= n; b &+= a }
        bytes = [0xb5, 0x62] + bytes + [a, b]; return bytes
    }
    static func frame(_ id: UInt8, _ p: [UInt8]) -> TimeCardUBXFrame { TimeCardUBXFrame.parseFrames(from: packet(id, p)).first! }
    static func payloads() -> ([UInt8], [UInt8], [UInt8]) {
        // 2024-01-01 00:00:00 UTC, GPS week 2295, TOW 86418 s.
        var utc = [UInt8](repeating: 0, count: 20), gps = [UInt8](repeating: 0, count: 16), ls = [UInt8](repeating: 0, count: 24)
        put32(86_418_000, &utc, 0); put32(50, &utc, 4); put16(2024, &utc, 12)
        utc[14] = 1; utc[15] = 1; utc[19] = 7
        put32(86_418_000, &gps, 0); put16(2295, &gps, 8); gps[10] = 18; gps[11] = 7; put32(50, &gps, 12)
        put32(86_418_000, &ls, 0); ls[8] = 2; ls[9] = 18; ls[10] = 2; ls[11] = 0; ls[23] = 3
        return (utc, gps, ls)
    }
    static func evidence(_ p: ([UInt8], [UInt8], [UInt8])) -> ReceiverTimeEvidence {
        var e = ReceiverTimeEvidence(); e.ingest(frame(0x21, p.0), at: 10); e.ingest(frame(0x20, p.1), at: 10); e.ingest(frame(0x26, p.2), at: 10); return e
    }
    static func main() throws {
        let baseline = payloads()
        let e = evidence(baseline), result = e.assessment(at: 11)
        precondition(result.qualified && result.utc?.seconds == 1_704_067_200 && result.tai?.seconds == 1_704_067_237)
        precondition(result.gpsMinusUTC == 18 && result.taiMinusUTC == 37)
        precondition(!e.assessment(at: 13.001).qualified && !e.assessment(at: 9).qualified && !e.assessment(at: .nan).qualified)
        precondition(e.assessment(at: 13).qualified)
        var repeated = e
        repeated.ingest(frame(0x21, baseline.0), at: 12); repeated.ingest(frame(0x20, baseline.1), at: 12); repeated.ingest(frame(0x26, baseline.2), at: 12)
        precondition(!repeated.assessment(at: 13.001).qualified, "Repeated epochs must not renew freshness")
        var withdrawn = e, unconfirmedLeap = baseline.2
        unconfirmedLeap[23] = 0
        withdrawn.ingest(frame(0x26, unconfirmedLeap), at: 11)
        precondition(!withdrawn.assessment(at: 11).qualified, "Changed metadata for the same epoch must revoke qualification")
        let summary = frame(0x26, baseline.2).summary
        precondition(summary.contains("GPS-UTC 18 s (source 2)") && summary.contains("next change 0 s (source 2)"))
        let normalized = try PreciseEpoch(seconds: 100, nanoseconds: -1)
        precondition(normalized.seconds == 99 && normalized.nanoseconds == 999_999_999)
        do { _ = try PreciseEpoch(seconds: .max, nanoseconds: 1_000_000_000); preconditionFailure() } catch { }
        do { _ = try PreciseEpoch(seconds: .max).differenceNanoseconds(from: PreciseEpoch(seconds: .min)); preconditionFailure() } catch { }
        for change in 0..<16 {
            var p = baseline
            switch change {
            case 0: p.0[19] = 3
            case 1: p.0[18] = 60
            case 2: p.0[14] = 2; p.0[15] = 30
            case 3: p.0[16] = 24
            case 4: put32(1_000_001, &p.0, 4)
            case 5: put32(UInt32.max, &p.0, 0)
            case 6: put16(2023, &p.0, 12)
            case 7: p.1[10] = 17
            case 8: p.1[11] = 3
            case 9: put16(1271, &p.1, 8) // A GPS week rollover must never be guessed.
            case 10: p.2[8] = 0
            case 11: p.2[8] = 7
            case 12: p.2[23] = 2
            case 13: p.2[4] = 1
            case 14: p.2[11] = 1; put32(60, &p.2, 12)
            default: p.2[11] = 1; p.2[23] = 1
            }
            precondition(!evidence(p).assessment(at: 11).qualified, "Invalid case \(change) qualified")
        }
        var negative = baseline
        put32(UInt32(bitPattern: -20), &negative.0, 8); put32(UInt32(bitPattern: -20), &negative.1, 4)
        let negativeResult = evidence(negative).assessment(at: 10)
        precondition(negativeResult.qualified && negativeResult.utc?.nanoseconds == 999_999_980)
        var adjacent = baseline; put32(86_419_000, &adjacent.1, 0)
        precondition(evidence(adjacent).assessment(at: 10).qualified)
        put32(86_420_000, &adjacent.1, 0)
        precondition(!evidence(adjacent).assessment(at: 10).qualified)
        // GPS week boundary: UTC is still Saturday while GPS enters Sunday.
        var boundary = baseline
        put16(2024, &boundary.0, 12); boundary.0[14] = 1; boundary.0[15] = 6
        boundary.0[16] = 23; boundary.0[17] = 59; boundary.0[18] = 42
        put32(0, &boundary.0, 0); put32(604_799_000, &boundary.1, 0)
        put32(0, &boundary.2, 0)
        precondition(evidence(boundary).assessment(at: 10).qualified)
        var corrupted = packet(0x21, baseline.0); corrupted[corrupted.count - 1] ^= 1
        var revoked = e; revoked.ingest(TimeCardUBXFrame.parseFrames(from: corrupted).first!, at: 11)
        precondition(!revoked.assessment(at: 11).qualified && revoked.rejectedFrames == 1)
        // Exhaustive short/long payloads and every split boundary.
        for id: UInt8 in [0x20, 0x21, 0x26] {
            for size in 0..<80 {
                var bad = ReceiverTimeEvidence(); bad.ingest(frame(id, [UInt8](repeating: 0xff, count: size)), at: 10)
                precondition(!bad.assessment(at: 10).qualified)
            }
        }
        let wire = packet(0x21, baseline.0) + packet(0x20, baseline.1) + packet(0x26, baseline.2)
        for split in 0...wire.count {
            var framer = LiveUBXFramer()
            let decoded = framer.feed(Array(wire.prefix(split))) + framer.feed(Array(wire.dropFirst(split)))
            precondition(decoded.count == 3 && decoded.allSatisfy(\.checksumValid) && framer.pending.isEmpty)
        }
        var noisy = LiveUBXFramer()
        precondition(noisy.feed([UInt8](repeating: 0xff, count: 100_000) + [0xb5, 0x62, 1, 0x21, 255, 255] + wire).count == 3)
        precondition(noisy.pending.count <= 4_104)
        func input(_ sequence: UInt64, offset: Int64 = 20_000, trusted: Bool = true, owner: Bool = true, associated: Bool = true, service: UInt64 = 1) -> DisciplineInput {
            .init(sequence: sequence, serviceID: service, receivedMonotonic: Double(sequence), offsetNanoseconds: offset,
                  uncertaintyNanoseconds: 100, referenceQualified: trusted, phcEpochAssociated: associated, clockInSync: true, exclusiveClockOwnership: owner)
        }
        var controller = BoundedClockDiscipline()
        for i: UInt64 in 1...4 { precondition(controller.evaluate(input(i), at: Double(i)).correctionMicroseconds == nil) }
        precondition(controller.evaluate(input(5), at: 5).correctionMicroseconds == 2)
        precondition(controller.evaluate(input(5), at: 5).correctionMicroseconds == nil)
        for i: UInt64 in 1...5 { _ = controller.evaluate(input(i, offset: 90_000_000), at: Double(i)) }
        precondition(controller.evaluate(input(6, offset: 90_000_000), at: 6).correctionMicroseconds == 250)
        precondition(controller.evaluate(input(7, owner: false), at: 7).correctionMicroseconds == nil)
        precondition(controller.evaluate(input(8, trusted: false), at: 8).correctionMicroseconds == nil)
        precondition(controller.evaluate(input(9, associated: false), at: 9).correctionMicroseconds == nil)
        precondition(controller.evaluate(input(10), at: 13).correctionMicroseconds == nil)
        precondition(controller.evaluate(input(11, offset: .min), at: 11).correctionMicroseconds == nil)
        precondition(controller.evaluate(input(12, offset: .max), at: 12).correctionMicroseconds == nil)
        print("Time trust tests passed: UTC/GPS/leap agreement, exact epochs, stale/corrupt data, rollover/leap guards, bounded framing, and fail-closed slew proposals.")
    }
}
