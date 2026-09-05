/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

struct TimeCardLiveProfileBackend: TimeCardProfileBackend, Sendable {
    let descriptor: TimeCardServiceDescriptor

    func read() throws -> TimeCardProfileState {
        let snapshot = try TimeCardClient.readSnapshot(for: descriptor)
        let target = TimeCardProfileTarget(vendorID: snapshot.vendorID, deviceID: snapshot.deviceID,
            revision: snapshot.pciRevision, boardProfile: snapshot.boardProfile, layout: snapshot.layout,
            clockVersion: snapshot.clockVersion)
        var settings: [TimeCardProfileSetting] = []
        var sources: Set<UInt32> = []
        var fixedDirections: [UInt32: UInt32] = [:]
        var immutable: Set<String> = []
        var notes: [String] = []
        if snapshot.supportsClockSource {
            let clock = try TimeCardClient.queryClockControl(for: descriptor)
            guard clock.clockVersion == snapshot.clockVersion else {
                throw TimeCardProfileError.invalid("Clock core changed during capture.")
            }
            sources = Set(clock.availableSources)
            if clock.supports(clock.source) {
                settings.append(.init(kind: .clockSource, channel: 0, values: [clock.source]))
            } else { notes.append("Current clock source has no validated write contract and is omitted.") }
        } else { notes.append("Clock-source control unavailable; omitted from capture.") }
        if snapshot.capabilityNames.contains("SMA") {
            for route in try TimeCardClient.querySMARoutes(for: descriptor) where route.isPresent {
                let setting = TimeCardProfileSetting(kind: .smaRoute, channel: route.connector,
                    values: [route.direction.rawValue, route.direction == .disabled ? 0 : route.function])
                settings.append(setting)
                if route.isFixedDirection { fixedDirections[route.connector] = route.direction.rawValue }
                // ART's driver ABI does not publish its route capability mask.
                if route.isFixedFunction || snapshot.boardProfile == 3 { immutable.insert(setting.id) }
            }
        } else { notes.append("SMA routing unavailable; omitted from capture.") }
        if snapshot.supportsFrequency {
            for counter in try TimeCardClient.queryFrequencies(for: descriptor) {
                guard !counter.isEnabled || counter.integrationSeconds > 0 else {
                    throw TimeCardProfileError.invalid("Counter \(counter.counter) has an invalid enabled interval.")
                }
                settings.append(.init(kind: .frequency, channel: counter.counter, values: [counter.integrationSeconds]))
            }
        } else { notes.append("Frequency-counter presence is not verified; optional addresses are not probed.") }
        notes.append("Profiles cover volatile clock source, SMA routes, and supported counters only. No PHC epoch, macOS clock, GNSS, flash, oscillator, or LED settings are changed.")
        return .init(serviceID: descriptor.id, target: target, settings: settings,
                     supportedClockSources: sources, fixedDirections: fixedDirections,
                     immutableSettings: immutable, notes: notes)
    }

    func write(_ setting: TimeCardProfileSetting, expected: TimeCardProfileSetting) throws {
        try setting.validate()
        guard setting.id == expected.id else { throw TimeCardProfileError.invalid("Mismatched expected setting.") }
        switch setting.kind {
        case .clockSource:
            _ = try TimeCardClient.setClockSource(for: descriptor, source: setting.values[0], expectedSource: expected.values[0])
        case .frequency:
            guard let current = try TimeCardClient.queryFrequencies(for: descriptor).first(where: { $0.counter == setting.channel }),
                  current.integrationSeconds == expected.values[0] else {
                throw TimeCardProfileError.invalid("Counter state changed before apply.")
            }
            _ = try TimeCardClient.setFrequency(for: descriptor, counter: setting.channel, seconds: setting.values[0],
                                                expectedControl: current.control)
        case .smaRoute:
            // Existing SMA ABI has no compare-and-set selector. Recheck immediately
            // before writing, then let the engine verify the complete configuration.
            let live = try read()
            guard live.values[setting.id] == expected, live.restriction(for: setting) == nil else {
                throw TimeCardProfileError.invalid("SMA state changed or the requested route is unsupported.")
            }
            let result = try TimeCardClient.setSMARoute(for: descriptor, connector: setting.channel,
                direction: TimeCardSMADirection(rawValue: setting.values[0])!, function: setting.values[1])
            guard result.direction.rawValue == setting.values[0],
                  result.direction == .disabled || result.function == setting.values[1] else {
                throw TimeCardProfileError.invalid("SMA readback did not match the requested route.")
            }
        }
    }

    func capture(name: String) throws -> (TimeCardConfigurationProfile, TimeCardProfileState) {
        let first = try read()
        let second = try read()
        guard first == second else { throw TimeCardProfileError.invalid("Configuration changed during capture. Try again.") }
        let profile = second.profile(name: name)
        try profile.validate()
        return (profile, second)
    }
}
