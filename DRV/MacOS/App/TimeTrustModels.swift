/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

// Civil time is integer seconds plus nanoseconds, never a floating-point Unix
// timestamp. A receiver epoch is NOT a timestamp of when its UART bytes arrive.
struct PreciseEpoch: Codable, Equatable, Sendable {
    let seconds: Int64
    let nanoseconds: UInt32
    init(seconds: Int64, nanoseconds: Int64 = 0) throws {
        var carry = nanoseconds / 1_000_000_000
        var fraction = nanoseconds % 1_000_000_000
        if fraction < 0 { carry -= 1; fraction += 1_000_000_000 }
        let sum = seconds.addingReportingOverflow(carry)
        guard !sum.overflow else { throw TimeTrustError.invalid("Epoch overflow") }
        self.seconds = sum.partialValue; self.nanoseconds = UInt32(fraction)
    }
    func adding(seconds: Int64) throws -> Self {
        let sum = self.seconds.addingReportingOverflow(seconds)
        guard !sum.overflow else { throw TimeTrustError.invalid("Epoch overflow") }
        return try .init(seconds: sum.partialValue, nanoseconds: Int64(nanoseconds))
    }
    func differenceNanoseconds(from other: Self) throws -> Int64 {
        let delta = seconds.subtractingReportingOverflow(other.seconds)
        let scaled = delta.partialValue.multipliedReportingOverflow(by: 1_000_000_000)
        let result = scaled.partialValue.addingReportingOverflow(Int64(nanoseconds) - Int64(other.nanoseconds))
        guard !delta.overflow, !scaled.overflow, !result.overflow else { throw TimeTrustError.invalid("Epoch difference overflow") }
        return result.partialValue
    }
    var text: String { "\(seconds)." + String(format: "%09u", nanoseconds) }
    var utcText: String {
        let formatter = DateFormatter(); formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.timeZone = TimeZone(secondsFromGMT: 0); formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"
        return formatter.string(from: Date(timeIntervalSince1970: Double(seconds))) + "." + String(format: "%09u", nanoseconds) + " UTC"
    }
}

enum TimeTrustError: LocalizedError {
    case invalid(String)
    var errorDescription: String? { switch self { case .invalid(let detail): detail } }
}

struct ReceiverLeapSeconds: Equatable, Sendable {
    let iTOW: UInt32
    let source: UInt8
    let gpsMinusUTC: Int
    let changeSource: UInt8
    let change: Int
    let secondsToEvent: Int32
    let currentValid: Bool
    let eventValid: Bool
    init(_ payload: [UInt8]) throws {
        guard payload.count == 24, payload[4] == 0 else { throw TimeTrustError.invalid("Unsupported NAV-TIMELS layout") }
        iTOW = TimeWire.u32(payload, 0); source = payload[8]
        gpsMinusUTC = Int(Int8(bitPattern: payload[9]))
        changeSource = payload[10]; change = Int(Int8(bitPattern: payload[11]))
        secondsToEvent = Int32(bitPattern: TimeWire.u32(payload, 12))
        currentValid = payload[23] & 1 != 0; eventValid = payload[23] & 2 != 0
        guard iTOW < 604_800_000, payload[23] & ~3 == 0 else { throw TimeTrustError.invalid("Invalid leap epoch or flags") }
    }
    // Firmware defaults, configured values and unknown sources are displayed
    // but cannot qualify an independent GNSS reference.
    var broadcastCurrent: Bool { currentValid && [1, 2, 3, 4, 5, 8].contains(source) && (0...100).contains(gpsMinusUTC) }
    var taiMinusUTC: Int? { broadcastCurrent ? gpsMinusUTC + 19 : nil }
}

private enum TimeWire {
    static func u16(_ p: [UInt8], _ n: Int) -> UInt16 { UInt16(p[n]) | UInt16(p[n + 1]) << 8 }
    static func u32(_ p: [UInt8], _ n: Int) -> UInt32 {
        UInt32(p[n]) | UInt32(p[n + 1]) << 8 | UInt32(p[n + 2]) << 16 | UInt32(p[n + 3]) << 24
    }
    static func weekDelta(_ lhs: UInt32, _ rhs: UInt32) -> Int64 {
        var value = Int64(lhs) - Int64(rhs)
        if value > 302_400_000 { value -= 604_800_000 }
        if value < -302_400_000 { value += 604_800_000 }
        return value
    }
}

struct ReceiverTimeAssessment: Codable, Equatable, Sendable {
    let qualified: Bool
    let utc: PreciseEpoch?
    let tai: PreciseEpoch?
    let gpsMinusUTC: Int?
    let taiMinusUTC: Int?
    let accuracyNanoseconds: UInt32?
    let reasons: [String]
}

struct ReceiverTimeEvidence: Sendable {
    private struct Timed<Value: Sendable>: Sendable { let value: Value; let received: Double }
    private struct UTC: Sendable, Equatable { let time: PreciseEpoch; let tow: UInt32; let accuracy: UInt32 }
    private struct GPS: Sendable, Equatable { let utc: PreciseEpoch; let tow: UInt32; let leap: Int; let accuracy: UInt32 }
    private var utc: Timed<UTC>?
    private var gps: Timed<GPS>?
    private var leap: Timed<ReceiverLeapSeconds>?
    private var failures: [UInt8: String] = [:]
    private(set) var acceptedFrames = 0
    private(set) var rejectedFrames = 0
    static let freshnessSeconds = 3.0

    mutating func ingest(_ frame: TimeCardUBXFrame, at monotonic: Double) {
        guard frame.messageClass == 1, [0x20, 0x21, 0x26].contains(frame.messageID) else { return }
        let id = frame.messageID
        do {
            guard monotonic.isFinite, monotonic >= 0, frame.checksumValid,
                  Int(frame.length) == frame.payload.count else { throw TimeTrustError.invalid("Invalid time frame checksum, length, or reception clock") }
            switch id {
            case 0x21:
                let value = try Self.parseUTC(frame.payload)
                let received = utc?.value.time == value.time && utc?.value.tow == value.tow ? utc!.received : monotonic
                utc = Timed(value: value, received: received)
            case 0x20:
                let value = try Self.parseGPS(frame.payload)
                let received = gps?.value.utc == value.utc && gps?.value.tow == value.tow ? gps!.received : monotonic
                gps = Timed(value: value, received: received)
            default:
                let value = try ReceiverLeapSeconds(frame.payload)
                let received = leap?.value.iTOW == value.iTOW ? leap!.received : monotonic
                leap = Timed(value: value, received: received)
            }
            failures[id] = nil; acceptedFrames += 1
        } catch {
            if id == 0x21 { utc = nil }; if id == 0x20 { gps = nil }; if id == 0x26 { leap = nil }
            failures[id] = error.localizedDescription; rejectedFrames += 1
        }
    }

    func assessment(at now: Double) -> ReceiverTimeAssessment {
        var reasons = failures.keys.sorted().compactMap { failures[$0] }
        func fresh<T>(_ value: Timed<T>?) -> Bool {
            guard let value, now.isFinite else { return false }
            return now >= value.received && now - value.received <= Self.freshnessSeconds
        }
        if !fresh(utc) { reasons.append("Fresh, valid NAV-TIMEUTC is missing.") }
        if !fresh(gps) { reasons.append("Fresh, valid NAV-TIMEGPS is missing.") }
        if !fresh(leap) { reasons.append("Fresh NAV-TIMELS is missing.") }
        if let utc, let gps, let leap, fresh(utc), fresh(gps), fresh(leap) {
            let ls = leap.value
            if !ls.broadcastCurrent { reasons.append("Leap seconds are not confirmed by a broadcast-derived source.") }
            if ls.gpsMinusUTC != gps.value.leap { reasons.append("GPS and leap-second messages disagree on GPS-UTC.") }
            let gpsDelta = TimeWire.weekDelta(gps.value.tow, utc.value.tow)
            if abs(gpsDelta) > 1_000 || abs(TimeWire.weekDelta(ls.iTOW, utc.value.tow)) > 1_000 {
                reasons.append("Time messages do not describe adjacent navigation epochs.")
            } else {
                do {
                    let expected = try PreciseEpoch(seconds: utc.value.time.seconds,
                        nanoseconds: Int64(utc.value.time.nanoseconds) + gpsDelta * 1_000_000)
                    let residual = try gps.value.utc.differenceNanoseconds(from: expected)
                    let tolerance = Int64(utc.value.accuracy) + Int64(gps.value.accuracy) + 1
                    if residual < -tolerance || residual > tolerance {
                        reasons.append("UTC and GPS calendar epochs disagree; no week-rollover guess is allowed.")
                    }
                } catch { reasons.append(error.localizedDescription) }
            }
            if !(-1...1).contains(ls.change) { reasons.append("Unknown leap-second change.") }
            if ls.change != 0 {
                if !ls.eventValid || ![2, 3, 4, 5, 6, 7].contains(ls.changeSource) {
                    reasons.append("Leap-event information is incomplete.")
                } else if Double(ls.secondsToEvent) - (now - leap.received) <= 60 {
                    reasons.append("Leap-event guard is active; wait for fresh post-event metadata.")
                }
            }
        }
        let good = reasons.isEmpty
        let validUTC = good ? utc?.value.time : nil
        let offset = good ? leap?.value.taiMinusUTC : nil
        let tai = validUTC.flatMap { epoch in offset.flatMap { try? epoch.adding(seconds: Int64($0)) } }
        return .init(qualified: good, utc: validUTC, tai: tai,
            gpsMinusUTC: good ? leap?.value.gpsMinusUTC : nil, taiMinusUTC: offset,
            accuracyNanoseconds: good ? max(utc?.value.accuracy ?? 0, gps?.value.accuracy ?? 0) : nil, reasons: reasons)
    }

    private static func parseUTC(_ p: [UInt8]) throws -> UTC {
        guard p.count == 20, p[19] & 7 == 7, p[19] & 8 == 0 else { throw TimeTrustError.invalid("UTC date/week/time validity is incomplete.") }
        let tow = TimeWire.u32(p, 0), accuracy = TimeWire.u32(p, 4)
        let nano = Int32(bitPattern: TimeWire.u32(p, 8))
        guard tow < 604_800_000, accuracy <= 1_000_000, (-1_000_000_000...1_000_000_000).contains(nano) else { throw TimeTrustError.invalid("UTC epoch, fraction, or accuracy is outside the qualification limits.") }
        guard p[18] != 60 else { throw TimeTrustError.invalid("A leap second cannot be collapsed into a POSIX timestamp.") }
        let year = Int(TimeWire.u16(p, 12)), month = Int(p[14]), day = Int(p[15])
        let hour = Int(p[16]), minute = Int(p[17]), second = Int(p[18])
        guard (1999...2099).contains(year), (1...12).contains(month), (1...31).contains(day), hour < 24, minute < 60, second < 60 else { throw TimeTrustError.invalid("Invalid UTC calendar components.") }
        var calendar = Calendar(identifier: .gregorian); calendar.timeZone = TimeZone(secondsFromGMT: 0)!
        let components = DateComponents(year: year, month: month, day: day, hour: hour, minute: minute, second: second)
        guard let date = calendar.date(from: components) else { throw TimeTrustError.invalid("Invalid UTC calendar.") }
        let decoded = calendar.dateComponents([.year, .month, .day, .hour, .minute, .second], from: date)
        guard decoded.year == year, decoded.month == month, decoded.day == day, decoded.hour == hour,
              decoded.minute == minute, decoded.second == second else { throw TimeTrustError.invalid("UTC calendar normalizes to a different date.") }
        return .init(time: try .init(seconds: Int64(date.timeIntervalSince1970), nanoseconds: Int64(nano)), tow: tow, accuracy: accuracy)
    }
    private static func parseGPS(_ p: [UInt8]) throws -> GPS {
        guard p.count == 16, p[11] & 7 == 7, p[11] & ~7 == 0 else { throw TimeTrustError.invalid("GPS week, TOW, or leap validity is incomplete.") }
        let tow = TimeWire.u32(p, 0), nano = Int32(bitPattern: TimeWire.u32(p, 4))
        let week = Int16(bitPattern: TimeWire.u16(p, 8)), leap = Int(Int8(bitPattern: p[10])), accuracy = TimeWire.u32(p, 12)
        guard week >= 0, tow < 604_800_000, (-500_000...500_000).contains(nano), (0...100).contains(leap), accuracy <= 1_000_000 else { throw TimeTrustError.invalid("GPS time fields are outside qualification limits.") }
        let time = try PreciseEpoch(seconds: 315_964_800 + Int64(week) * 604_800 + Int64(tow / 1_000) - Int64(leap),
                                    nanoseconds: Int64(tow % 1_000) * 1_000_000 + Int64(nano))
        return .init(utc: time, tow: tow, leap: leap, accuracy: accuracy)
    }
}

// Bounded streaming UBX framing. Only complete frames are timestamped, and
// replay/import never feeds this live-evidence path.
struct LiveUBXFramer: Sendable {
    private(set) var pending: [UInt8] = []
    private(set) var discardedBytes = 0
    mutating func feed(_ bytes: [UInt8]) -> [TimeCardUBXFrame] {
        var frames: [TimeCardUBXFrame] = []
        for byte in bytes {
            pending.append(byte)
            while pending.count >= 2 {
                if pending[0] != 0xb5 || pending[1] != 0x62 { pending.removeFirst(); discardedBytes += 1; continue }
                guard pending.count >= 6 else { break }
                let size = Int(pending[4]) | Int(pending[5]) << 8
                if size > 4_096 { pending.removeFirst(); discardedBytes += 1; continue }
                guard pending.count >= size + 8 else { break }
                if let frame = TimeCardUBXFrame.parseFrames(from: Array(pending.prefix(size + 8))).first {
                    frames.append(frame)
                }
                pending.removeFirst(size + 8)
            }
        }
        return frames
    }
}

// Pure, bounded controller for future privileged integration. No clock APIs
// live here. Unknown provenance or another clock owner always inhibits it.
struct DisciplineInput: Sendable {
    let sequence: UInt64
    let serviceID: UInt64
    let receivedMonotonic: Double
    let offsetNanoseconds: Int64
    let uncertaintyNanoseconds: UInt64
    let referenceQualified: Bool
    let phcEpochAssociated: Bool
    let clockInSync: Bool
    let exclusiveClockOwnership: Bool
}
struct DisciplineDecision: Equatable, Sendable {
    let correctionMicroseconds: Int64?
    let reason: String
}
struct BoundedClockDiscipline: Sendable {
    private var previous: DisciplineInput?
    private var stable = 0
    mutating func reset() { previous = nil; stable = 0 }
    mutating func evaluate(_ input: DisciplineInput, at now: Double) -> DisciplineDecision {
        func blocked(_ reason: String) -> DisciplineDecision { .init(correctionMicroseconds: nil, reason: reason) }
        guard input.referenceQualified, input.phcEpochAssociated, input.clockInSync, input.exclusiveClockOwnership,
              now.isFinite, input.receivedMonotonic.isFinite, input.receivedMonotonic >= 0,
              now >= input.receivedMonotonic, now - input.receivedMonotonic <= 2,
              input.uncertaintyNanoseconds <= 100_000 else {
            reset(); return blocked("Reference, epoch association, freshness, uncertainty, lock, or exclusive ownership is missing.")
        }
        guard input.offsetNanoseconds >= -100_000_000, input.offsetNanoseconds <= 100_000_000 else {
            reset(); return blocked("Offset exceeds the 100 ms slew-only limit; a clock step is never automatic.")
        }
        guard let old = previous else { previous = input; stable = 1; return blocked("Collecting stable samples.") }
        let interval = input.receivedMonotonic - old.receivedMonotonic
        guard input.serviceID == old.serviceID, input.sequence > old.sequence, interval >= 0.1, interval <= 5,
              abs(input.offsetNanoseconds - old.offsetNanoseconds) <= 1_000_000 else {
            reset(); return blocked("Card, sample ordering, cadence, or phase continuity changed.")
        }
        previous = input; stable += 1
        guard stable >= 5 else { return blocked("Collecting stable samples (\(stable)/5).") }
        let rate = Double(input.offsetNanoseconds - old.offsetNanoseconds) / interval
        let requested = (Double(input.offsetNanoseconds) * 0.125 + rate * interval) / 1_000
        let bound = min(1_000.0, interval * 250.0)
        let correction = abs(input.offsetNanoseconds) < 5_000 ? 0 : Int64(max(-bound, min(bound, requested)).rounded(.towardZero))
        return .init(correctionMicroseconds: correction, reason: "Slew-only proposal, capped at 250 microseconds per second; no clock write performed.")
    }
}
