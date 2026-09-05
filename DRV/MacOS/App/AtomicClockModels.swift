/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

enum AtomicClockError: LocalizedError {
    case invalid(String)
    var errorDescription: String? { if case .invalid(let message) = self { return message }; return nil }
}

struct SA53Snapshot: Codable, Sendable {
    let capturedAt: Date
    let serviceID: UInt64
    let values: [String: String]
    let warnings: [String]
    var isSA5x: Bool { values["device?"]?.lowercased().contains("sa5") == true }
    var phaseMeasurement: Double? {
        guard boolean("PpsInDetected") == true, boolean("JamSyncing") == false,
              boolean("Disciplining") == true || boolean("PhaseMetering") == true else { return nil }
        return number("Phase")
    }
    var alarmDescription: String {
        guard let alarm = integer("Alarms"), alarm >= 0, alarm <= UInt32.max else { return "Alarm state unavailable" }
        if alarm == 0 { return "No active alarm bits" }
        let definitions: [(Int64, String)] = [(1,"FPGA fault"), (2,"PLL fault"), (4,"Flash fault"),
            (8,"Acquisition failed"), (16,"No external oscillator"), (32,"Cell heater fault"),
            (64,"Incompatible firmware"), (65536,"Temperature warning"), (131072,"No PPS input"),
            (262144,"Disciplining range warning")]
        var names = definitions.filter { alarm & $0.0 != 0 }.map(\.1)
        let unknown = alarm & ~definitions.reduce(Int64(0)) { $0 | $1.0 }
        if unknown != 0 { names.append(String(format: "Unknown bits 0x%08llX", unknown)) }
        return names.joined(separator: ", ")
    }
    func number(_ key: String) -> Double? {
        guard let text = values[key], let value = Double(text), value.isFinite else { return nil }
        return value
    }
    func integer(_ key: String) -> Int64? {
        if values[key] == "true" { return 1 }
        if values[key] == "false" { return 0 }
        return values[key].flatMap(Int64.init)
    }
    func boolean(_ key: String) -> Bool? {
        guard let value = integer(key), value == 0 || value == 1 else { return nil }
        return value == 1
    }
    static let identity = ["device?", "pn?", "serial?", "swrev?", "hwrev?"]
    static let telemetry = ["Alarms", "PpsInDetected", "TotalRuntime", "TotalLocktime", "Locked",
        "TimeOfDay", "DisciplineLocked", "PpsOffset", "PpsWidth", "CableDelay", "Disciplining",
        "PpsSource", "TauPps0", "PpsQErr", "PhaseLimit", "JamSyncing", "Phase", "LastCorrection",
        "TauPps1", "PhaseMetering", "DisciplineThresholdPps0", "DisciplineThresholdPps1", "LaserTempSet",
        "OscTuning", "OvenCurrent", "DCSignal", "AnalogTuning", "Temperature", "DigitalTuning",
        "PowerSupply", "AnalogTuningEnabled", "EffectiveTuning", "LockProgress", "DisciplineTuning"]
}

struct SA53Parameter: Identifiable, Sendable {
    let id: String
    let unit: String
    let range: ClosedRange<Int64>
    var step: Int64 = 1
    func validate(_ value: Int64) throws {
        guard range.contains(value), value % step == 0 else {
            throw AtomicClockError.invalid("\(id) requires \(range.lowerBound)...\(range.upperBound) \(unit), in steps of \(step).")
        }
    }
    // Microchip DS50002938G, parameter index. No arbitrary C3 write command.
    static let writable: [Self] = [
        .init(id: "DigitalTuning", unit: "×10⁻¹⁵", range: -20_000_000...20_000_000),
        .init(id: "PpsOffset", unit: "ns", range: -83_886_080...83_886_080, step: 10),
        .init(id: "PpsWidth", unit: "ns", range: 100...83_886_080, step: 10),
        .init(id: "CableDelay", unit: "ns", range: -500_000_000...500_000_000),
        .init(id: "PpsSource", unit: "0 or 1", range: 0...1),
        .init(id: "TauPps0", unit: "s", range: 10...45_000),
        .init(id: "TauPps1", unit: "s", range: 10...45_000),
        .init(id: "PpsQErr", unit: "ps", range: -1_000_000...1_000_000),
        .init(id: "PhaseLimit", unit: "ns", range: -1_000_000...1_000_000),
        .init(id: "DisciplineThresholdPps0", unit: "ns", range: 1...1_000),
        .init(id: "DisciplineThresholdPps1", unit: "ns", range: 1...1_000),
        .init(id: "Disciplining", unit: "0 or 1", range: 0...1),
        .init(id: "PhaseMetering", unit: "0 or 1", range: 0...1),
        .init(id: "AnalogTuningEnabled", unit: "0 or 1", range: 0...1),
        .init(id: "TimeOfDay", unit: "s, next PPS", range: 0...2_147_483_647)
    ]
}

enum SA53C3 {
    static func checksum(_ bytes: some Sequence<UInt8>) -> UInt8 { bytes.reduce(0, ^) }
    static func request(_ command: String, sequence: UInt8) throws -> [UInt8] {
        guard !command.isEmpty, command.utf8.count <= 220,
              command.utf8.allSatisfy({ $0 >= 0x21 && $0 <= 0x7e && !Array("{}[]|#".utf8).contains($0) }) else {
            throw AtomicClockError.invalid("Invalid C3 command.")
        }
        let fields = command.split(separator: ",", maxSplits: 1, omittingEmptySubsequences: false)
        let body = String(fields[0]) + String(format: "#%02X", sequence) +
            (fields.count == 2 ? "," + fields[1] : "")
        return Array(("{" + body + String(format: "|%02X}", checksum(body.utf8))).utf8)
    }
    struct Decoder {
        private var bytes: [UInt8] = []
        private var received = 0
        mutating func append(_ input: [UInt8], sequence: UInt8) throws -> String? {
            received += input.count
            guard received <= 8192 else { throw AtomicClockError.invalid("C3 response exceeded 8 KiB.") }
            bytes += input
            while let start = bytes.firstIndex(of: 0x5b) {
                if start > 0 { bytes.removeFirst(start) }
                // C3 quoted values may contain brackets. Locate the unquoted terminator.
                var quoted = false; var escaped = false; var end: Int?
                for index in 1..<bytes.count {
                    let byte = bytes[index]
                    if escaped { escaped = false; continue }
                    if quoted && byte == 0x5c { escaped = true; continue }
                    if byte == 0x22 { quoted.toggle() }
                    if byte == 0x5d && !quoted { end = index; break }
                }
                guard let end else { return nil }
                let frame = Array(bytes[1..<end]); bytes.removeFirst(end + 1)
                if frame.first == 0x3e { continue } // Boot announcements are not replies.
                guard let pipe = frame.lastIndex(of: 0x7c), frame.count - pipe == 3,
                      let expected = UInt8(String(decoding: frame[(pipe + 1)...], as: UTF8.self), radix: 16),
                      checksum(frame[..<pipe]) == expected else {
                    throw AtomicClockError.invalid("C3 response checksum missing or invalid.")
                }
                let body = Array(frame[..<pipe])
                let prefix = Array(String(format: "#%02X", sequence).utf8)
                guard body.count >= 4, body.starts(with: prefix) else { continue }
                let value = String(decoding: body.dropFirst(4), as: UTF8.self)
                if body[3] == 0x21 { throw AtomicClockError.invalid("SA53 rejected the command, C3 error " + value) }
                guard body[3] == 0x3d else { throw AtomicClockError.invalid("Unexpected C3 response type.") }
                return try unquote(value)
            }
            bytes.removeAll(keepingCapacity: true)
            return nil
        }
    }
    static func unquote(_ value: String) throws -> String {
        guard value.hasPrefix("\"") else { return value.trimmingCharacters(in: .whitespacesAndNewlines) }
        guard value.hasSuffix("\""), value.count >= 2 else { throw AtomicClockError.invalid("Incomplete C3 string.") }
        var result = ""; var escaped = false
        for char in value.dropFirst().dropLast() {
            if escaped {
                result.append(char == "n" ? "\n" : char == "r" ? "\r" : char == "t" ? "\t" : char)
                escaped = false
            } else if char == "\\" { escaped = true }
            else { result.append(char) }
        }
        guard !escaped else { throw AtomicClockError.invalid("Incomplete C3 escape.") }
        return result
    }
}
