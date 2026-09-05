/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

final class SA53Client {
    let descriptor: TimeCardServiceDescriptor
    private static var sequence: UInt8 = 0 // Access only inside withUARTSession.
    init(descriptor: TimeCardServiceDescriptor) { self.descriptor = descriptor }

    private func prepare() throws {
        let snapshot = try TimeCardClient.readSnapshot(for: descriptor)
        guard [1, 2].contains(snapshot.boardProfile), snapshot.abiVersion >= 9,
              snapshot.capabilityNames.contains("UART") else {
            throw AtomicClockError.invalid("SA53 requires a Meta/Celestica MAC UART and ABI v9 or newer. ART mRO-50 and other unvalidated routes are not probed.")
        }
        try TimeCardClient.configureUART(for: descriptor, port: .mac, baudRate: 57_600)
        // Bounded stale-data drain. Never retry a clock-changing command.
        for _ in 0..<8 {
            if try TimeCardClient.readUART(for: descriptor, port: .mac, maximumBytes: 256, timeoutMilliseconds: 0).data.isEmpty { break }
        }
    }
    private func query(_ command: String) throws -> String {
        SA53Client.sequence &+= 1
        let sequence = SA53Client.sequence
        let bytes = try SA53C3.request(command, sequence: sequence)
        let result = try TimeCardClient.writeUART(for: descriptor, port: .mac, bytes: bytes, timeoutMilliseconds: 500)
        guard result.byteCount == bytes.count else { throw AtomicClockError.invalid("Incomplete C3 transmission. Outcome may be unknown; do not automatically retry.") }
        let deadline = ProcessInfo.processInfo.systemUptime + 1.5
        var decoder = SA53C3.Decoder()
        while ProcessInfo.processInfo.systemUptime < deadline {
            let transfer = try TimeCardClient.readUART(for: descriptor, port: .mac, maximumBytes: 256, timeoutMilliseconds: 100)
            guard transfer.lineStatus & 0x1e == 0 else { throw AtomicClockError.invalid("MAC UART reported a framing, parity, or overrun error.") }
            if let response = try decoder.append(transfer.data, sequence: sequence) { return response }
        }
        throw AtomicClockError.invalid("SA53 response timed out on UART 2 at 57,600 baud. Verify the fitted oscillator and its serial connection. A timed-out write has an unknown outcome.")
    }
    private func readSnapshot() throws -> SA53Snapshot {
        var values = [String: String](); var warnings = [String]()
        values["device?"] = try query("device?")
        guard values["device?"]?.lowercased().contains("sa5") == true else {
            throw AtomicClockError.invalid("UART 2 did not identify as an SA5x clock; controls are disabled.")
        }
        let deadline = ProcessInfo.processInfo.systemUptime + 15
        for key in Array(SA53Snapshot.identity.dropFirst()) + SA53Snapshot.telemetry {
            if ProcessInfo.processInfo.systemUptime >= deadline { warnings.append("Refresh budget reached; remaining fields are unavailable."); break }
            do { values[key] = try query(key.hasSuffix("?") ? key : "get," + key) }
            catch { warnings.append(key + ": " + error.localizedDescription) }
        }
        return .init(capturedAt: Date(), serviceID: descriptor.id, values: values, warnings: warnings)
    }
    func refresh() throws -> SA53Snapshot {
        try TimeCardClient.withUARTSession { try prepare(); return try readSnapshot() }
    }
    func set(_ parameter: String, value: Int64, baseline: SA53Snapshot) throws -> SA53Snapshot {
        guard let definition = SA53Parameter.writable.first(where: { $0.id == parameter }) else {
            throw AtomicClockError.invalid("Parameter is not writable.")
        }
        try definition.validate(value)
        return try TimeCardClient.withUARTSession {
            try validateBaseline(baseline)
            let currentResponse = try query("get," + parameter)
            let currentValue = currentResponse == "true" ? 1 : currentResponse == "false" ? 0 : Int64(currentResponse)
            guard let expected = baseline.integer(parameter), let current = currentValue,
                  parameter == "TimeOfDay" || parameter == "PpsQErr" || current == expected else {
                throw AtomicClockError.invalid("Setting changed since refresh. Refresh and review again.")
            }
            if current == value { return try readSnapshot() }
            func require(_ key: String, _ expected: Int64) throws {
                let response = try query("get," + key)
                let number = response == "true" ? 1 : response == "false" ? 0 : Int64(response)
                guard number == expected else { throw AtomicClockError.invalid("Set \(key) to \(expected) before changing \(parameter).") }
            }
            if parameter == "AnalogTuningEnabled" && value == 1 { try require("DigitalTuning", 0); try require("Disciplining", 0) }
            if parameter == "DigitalTuning" && value != 0 { try require("AnalogTuningEnabled", 0) }
            if parameter == "Disciplining" && value == 1 { try require("PhaseMetering", 0); try require("AnalogTuningEnabled", 0) }
            if parameter == "PhaseMetering" && value == 1 { try require("Disciplining", 0) }
            _ = try query("set,\(parameter),\(value)")
            if parameter == "TimeOfDay" { Thread.sleep(forTimeInterval: 1.1) }
            let response = try query("get," + parameter)
            let verified = response == "true" ? 1 : response == "false" ? 0 : Int64(response)
            if parameter != "PpsQErr" {
                guard let verified, parameter == "TimeOfDay" ? (value...min(value + 3, 2_147_483_647)).contains(verified) : verified == value else {
                    throw AtomicClockError.invalid("Write was acknowledged but readback did not match. Refresh before another action. No automatic phase-changing rollback was attempted.")
                }
            }
            return try readSnapshot()
        }
    }
    enum Action: String, CaseIterable, Identifiable, Sendable {
        case sync = "JamSync", acknowledge = "Acknowledge alarms", load = "Load stored configuration", store = "Store configuration to flash"
        var id: Self { self }
    }
    func action(_ action: Action, baseline: SA53Snapshot) throws -> SA53Snapshot {
        try TimeCardClient.withUARTSession {
            try validateBaseline(baseline)
            switch action {
            case .sync: _ = try query("sync")
            case .load: _ = try query("load")
            case .store: _ = try query("store")
            case .acknowledge:
                guard let mask = UInt32(try query("get,Alarms")), mask > 0 else { throw AtomicClockError.invalid("No active alarm bits to acknowledge.") }
                _ = try query("ackalm,\(mask)")
            }
            return try readSnapshot()
        }
    }
    private func validateBaseline(_ baseline: SA53Snapshot) throws {
        guard baseline.serviceID == descriptor.id, baseline.isSA5x,
              (0...120).contains(Date().timeIntervalSince(baseline.capturedAt)),
              let serial = baseline.values["serial?"], !serial.isEmpty else {
            throw AtomicClockError.invalid("A fresh identified SA53 snapshot is required. Refresh first.")
        }
        try prepare()
        guard try query("device?") == baseline.values["device?"], try query("serial?") == serial else {
            throw AtomicClockError.invalid("Oscillator identity changed. No settings were written.")
        }
    }
}
