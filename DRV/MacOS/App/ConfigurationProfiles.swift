/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

struct TimeCardProfileTarget: Codable, Equatable, Sendable {
    let vendorID: UInt16
    let deviceID: UInt16
    let revision: UInt32
    let boardProfile: UInt32
    let layout: UInt32
    let clockVersion: UInt32
    var summary: String {
        String(format: "%04x:%04x · revision %02x · layout %u · clock 0x%08x",
               vendorID, deviceID, revision, layout, clockVersion)
    }
}

struct TimeCardProfileSetting: Codable, Equatable, Identifiable, Sendable {
    enum Kind: String, Codable, CaseIterable, Sendable { case smaRoute, frequency, clockSource }
    let kind: Kind
    let channel: UInt32
    var values: [UInt32]
    var id: String { "\(kind.rawValue):\(channel)" }
    var title: String {
        switch kind {
        case .clockSource: return "Clock source"
        case .smaRoute: return "SMA \(channel)"
        case .frequency: return "Counter \(channel)"
        }
    }
    var summary: String {
        guard !values.isEmpty else { return "Invalid" }
        switch kind {
        case .clockSource: return TimeCardClockControlState.name(values[0])
        case .frequency: return values[0] == 0 ? "Disabled" : "\(values[0]) s integration"
        case .smaRoute:
            guard values.count == 2, let direction = TimeCardSMADirection(rawValue: values[0]) else { return "Invalid route" }
            return direction == .disabled ? "Disabled" : direction.label + " / " +
                TimeCardSMACatalog.name(for: values[1], direction: direction)
        }
    }
    func validate() throws {
        switch kind {
        case .clockSource:
            guard channel == 0, values.count == 1,
                  TimeCardClockControlState.knownSources.contains(values[0]) else {
                throw TimeCardProfileError.invalid("Invalid clock-source setting.")
            }
        case .frequency:
            guard (1...4).contains(channel), values.count == 1, values[0] <= 255 else {
                throw TimeCardProfileError.invalid("Counter channel must be 1...4 and interval 0...255 seconds.")
            }
        case .smaRoute:
            guard (1...4).contains(channel), values.count == 2, values[0] <= 2,
                  values[1] <= 0x7fff, values[0] != 2 || values[1] == 0 else {
                throw TimeCardProfileError.invalid("Invalid SMA direction, function, or channel.")
            }
        }
    }
}

struct TimeCardConfigurationProfile: Codable, Equatable, Sendable {
    static let formatIdentifier = "org.opentimeserver.timecard.macos.profile"
    static let maximumFileBytes = 64 * 1024
    var format = Self.formatIdentifier
    var schemaVersion = 1
    var name: String
    let capturedAt: Date
    let target: TimeCardProfileTarget
    var settings: [TimeCardProfileSetting]

    func validate() throws {
        guard format == Self.formatIdentifier, schemaVersion == 1 else {
            throw TimeCardProfileError.invalid("Unsupported profile format or schema. Windows XML profiles are not interchangeable with this JSON format.")
        }
        guard !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              name.utf8.count <= 160, !name.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) else {
            throw TimeCardProfileError.invalid("Use a profile name of 1 to 160 UTF-8 bytes, without control characters.")
        }
        guard !settings.isEmpty, settings.count <= 9,
              Set(settings.map(\.id)).count == settings.count else {
            throw TimeCardProfileError.invalid("Profiles require 1 to 9 unique settings; duplicate channels are rejected.")
        }
        guard (1...5).contains(target.boardProfile), (1...3).contains(target.layout),
              target.clockVersion != 0, target.clockVersion != UInt32.max else {
            throw TimeCardProfileError.invalid("Profile hardware identity is incomplete.")
        }
        try settings.forEach { try $0.validate() }
    }

    func encoded() throws -> Data {
        try validate()
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        let data = try encoder.encode(self)
        guard data.count <= Self.maximumFileBytes else { throw TimeCardProfileError.invalid("Profile exceeds 64 KiB.") }
        return data
    }
    static func decode(_ data: Data) throws -> Self {
        guard data.count <= maximumFileBytes else { throw TimeCardProfileError.invalid("Profile exceeds 64 KiB.") }
        // Unknown fields can conceal unsupported controls. Reject instead of silently ignoring them.
        guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              Set(root.keys) == Set(["format", "schemaVersion", "name", "capturedAt", "target", "settings"]),
              let target = root["target"] as? [String: Any],
              Set(target.keys) == Set(["vendorID", "deviceID", "revision", "boardProfile", "layout", "clockVersion"]),
              let settings = root["settings"] as? [[String: Any]],
              settings.allSatisfy({ Set($0.keys) == Set(["kind", "channel", "values"]) }) else {
            throw TimeCardProfileError.invalid("Unknown or missing profile fields.")
        }
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        let profile = try decoder.decode(Self.self, from: data)
        try profile.validate()
        return profile
    }
}

enum TimeCardProfileError: LocalizedError {
    case invalid(String)
    var errorDescription: String? { if case .invalid(let reason) = self { return reason }; return nil }
}

struct TimeCardProfileState: Equatable, Sendable {
    let serviceID: UInt64
    let target: TimeCardProfileTarget
    let settings: [TimeCardProfileSetting]
    let supportedClockSources: Set<UInt32>
    let fixedDirections: [UInt32: UInt32]
    let immutableSettings: Set<String>
    let notes: [String]
    var values: [String: TimeCardProfileSetting] { Dictionary(uniqueKeysWithValues: settings.map { ($0.id, $0) }) }
    func profile(name: String, date: Date = Date()) -> TimeCardConfigurationProfile {
        .init(name: name, capturedAt: date, target: target, settings: settings)
    }
    func restriction(for desired: TimeCardProfileSetting) -> String? {
        guard let existing = values[desired.id] else { return "Not available on this image." }
        if desired == existing { return nil }
        if immutableSettings.contains(desired.id) { return "Fixed or read-only route on this image." }
        if desired.kind == .clockSource && !supportedClockSources.contains(desired.values[0]) {
            return "Source is not supported by the active clock core."
        }
        if desired.kind == .smaRoute {
            guard let originalDirection = TimeCardSMADirection(rawValue: existing.values[0]),
                  originalDirection == .disabled || TimeCardSMACatalog.functions(for: originalDirection).contains(where: { $0.id == existing.values[1] }) else {
                return "Current route has no validated restore contract. Capture-only."
            }
            if let direction = fixedDirections[desired.channel], direction != desired.values[0] {
                return "Connector direction is fixed by the FPGA image."
            }
            let direction = TimeCardSMADirection(rawValue: desired.values[0])!
            if direction != .disabled && !TimeCardSMACatalog.functions(for: direction).contains(where: { $0.id == desired.values[1] }) {
                return "Only cataloged single-route functions can be applied by profiles."
            }
        }
        return nil
    }
}

struct TimeCardProfileDiff: Identifiable, Sendable {
    let requested: TimeCardProfileSetting
    let previous: TimeCardProfileSetting?
    let blocker: String?
    var id: String { requested.id }
    var changed: Bool { previous != requested }
}

struct TimeCardProfilePlan: Sendable {
    let profile: TimeCardConfigurationProfile
    let baseline: TimeCardProfileState
    let createdAt: Date
    let differences: [TimeCardProfileDiff]
    let blockers: [String]
    var changes: [TimeCardProfileDiff] { differences.filter(\.changed) }
    var canApply: Bool { blockers.isEmpty }
    static func create(profile: TimeCardConfigurationProfile, state: TimeCardProfileState,
                       now: Date = Date()) throws -> Self {
        try profile.validate()
        var blockers = profile.target == state.target ? [] : ["PCI identity, revision, register layout, or clock version does not match this card."]
        let differences = profile.settings.sorted { a, b in
            let order: [TimeCardProfileSetting.Kind: Int] = [.smaRoute: 0, .frequency: 1, .clockSource: 2]
            return order[a.kind]! == order[b.kind]! ? a.channel < b.channel : order[a.kind]! < order[b.kind]!
        }.map { setting in
            TimeCardProfileDiff(requested: setting, previous: state.values[setting.id], blocker: state.restriction(for: setting))
        }
        blockers += differences.compactMap { diff in diff.blocker.map { diff.requested.title + ": " + $0 } }
        return Self(profile: profile, baseline: state, createdAt: now, differences: differences, blockers: blockers)
    }
}

protocol TimeCardProfileBackend {
    func read() throws -> TimeCardProfileState
    func write(_ setting: TimeCardProfileSetting, expected: TimeCardProfileSetting) throws
}

struct TimeCardProfileApplyReport: Codable, Sendable {
    enum Outcome: String, Codable { case applied, unchanged, rejected, rolledBack, recoveryRequired }
    let outcome: Outcome
    let date: Date
    let profileName: String
    let serviceID: String
    let attemptedSettings: Int
    let verifiedSettings: Int
    let events: [String]
    let recoveryProfile: TimeCardConfigurationProfile?
    var successful: Bool { outcome == .applied || outcome == .unchanged }
}

enum TimeCardProfileEngine {
    // The sequence is recoverable, not hardware-atomic. Close other writers.
    // Clock/frequency setters have driver CAS guards; SMA has app prechecks only.
    static func apply(_ plan: TimeCardProfilePlan, backend: any TimeCardProfileBackend,
                      now: Date = Date()) -> TimeCardProfileApplyReport {
        var events: [String] = []
        var attempted: [TimeCardProfileDiff] = []
        var verifiedSettings = 0
        var expected = plan.baseline.values
        func report(_ outcome: TimeCardProfileApplyReport.Outcome) -> TimeCardProfileApplyReport {
            let previous = attempted.compactMap(\.previous)
            return .init(outcome: outcome, date: now, profileName: plan.profile.name,
                         serviceID: String(plan.baseline.serviceID), attemptedSettings: attempted.count,
                         verifiedSettings: verifiedSettings, events: events,
                         recoveryProfile: previous.isEmpty ? nil : .init(name: "Recovery: " + String(plan.profile.name.prefix(30)),
                              capturedAt: now, target: plan.baseline.target, settings: previous))
        }
        func matches(_ state: TimeCardProfileState, values: [String: TimeCardProfileSetting]) -> Bool {
            state.serviceID == plan.baseline.serviceID && state.target == plan.baseline.target && state.values == values &&
                state.supportedClockSources == plan.baseline.supportedClockSources &&
                state.immutableSettings == plan.baseline.immutableSettings && state.fixedDirections == plan.baseline.fixedDirections
        }
        do {
            try plan.profile.validate()
            guard plan.canApply, now.timeIntervalSince(plan.createdAt) >= 0,
                  now.timeIntervalSince(plan.createdAt) <= 120 else {
                throw TimeCardProfileError.invalid("Preview is blocked or older than two minutes. Run Preview again.")
            }
            guard matches(try backend.read(), values: expected) else {
                throw TimeCardProfileError.invalid("Card or settings changed since preview. No profile writes were made.")
            }
            for difference in plan.changes {
                guard let previous = difference.previous,
                      matches(try backend.read(), values: expected) else {
                    throw TimeCardProfileError.invalid("Concurrent settings change detected. Apply stopped.")
                }
                attempted.append(difference) // A failing driver call can have partially written.
                try backend.write(difference.requested, expected: previous)
                expected[difference.id] = difference.requested
                guard matches(try backend.read(), values: expected) else {
                    throw TimeCardProfileError.invalid("Readback did not match after \(difference.requested.title).")
                }
                events.append("Verified \(difference.requested.title): \(previous.summary) -> \(difference.requested.summary)")
                verifiedSettings += 1
            }
            events.append(plan.changes.isEmpty ? "All settings already match. No writes were made." : "All profile changes verified.")
            return report(plan.changes.isEmpty ? .unchanged : .applied)
        } catch {
            events.append("Apply stopped: " + error.localizedDescription)
            if attempted.isEmpty { return report(.rejected) }
            var recoveryFailed = false
            for difference in attempted.reversed() {
                do {
                    let live = try backend.read()
                    guard live.serviceID == plan.baseline.serviceID, live.target == plan.baseline.target,
                          let previous = difference.previous, let current = live.values[difference.id] else {
                        throw TimeCardProfileError.invalid("Original card or setting is no longer available.")
                    }
                    if current == previous { events.append("Already restored: \(previous.title)"); continue }
                    guard current == difference.requested else {
                        throw TimeCardProfileError.invalid("Setting is neither the original nor requested value. Not overwriting an unknown state.")
                    }
                    try backend.write(previous, expected: current)
                    let verified = try backend.read()
                    guard verified.serviceID == live.serviceID, verified.target == live.target,
                          verified.values[difference.id] == previous else {
                        throw TimeCardProfileError.invalid("Rollback readback failed.")
                    }
                    events.append("Restored and verified: \(previous.title)")
                } catch {
                    recoveryFailed = true
                    events.append("Manual recovery required for \(difference.requested.title): " + error.localizedDescription)
                }
            }
            return report(recoveryFailed ? .recoveryRequired : .rolledBack)
        }
    }
}
