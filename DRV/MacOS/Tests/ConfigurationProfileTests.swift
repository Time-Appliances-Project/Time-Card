/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

private final class MockProfiles: TimeCardProfileBackend {
    var state: TimeCardProfileState
    var writes: [TimeCardProfileSetting] = []
    var reads = 0
    var failAtWrite = 0
    var failAfterWrite = false
    var corruptOnFailure = false
    var rejectRollback = false
    var failAtRead = 0
    var failReadsFrom = 0
    var mutateAtRead = 0
    var replaceServiceAtRead = 0
    init(_ state: TimeCardProfileState) { self.state = state }
    func replacing(_ setting: TimeCardProfileSetting) {
        var settings = state.settings
        settings[settings.firstIndex { $0.id == setting.id }!] = setting
        state = .init(serviceID: state.serviceID, target: state.target, settings: settings,
                      supportedClockSources: state.supportedClockSources, fixedDirections: state.fixedDirections,
                      immutableSettings: state.immutableSettings, notes: state.notes)
    }
    func read() throws -> TimeCardProfileState {
        reads += 1
        if reads == failAtRead || (failReadsFrom > 0 && reads >= failReadsFrom) {
            throw TimeCardProfileError.invalid("Injected read failure")
        }
        if reads == mutateAtRead { replacing(.init(kind: .frequency, channel: 1, values: [17])) }
        if reads == replaceServiceAtRead {
            state = .init(serviceID: 456, target: state.target, settings: state.settings,
                supportedClockSources: state.supportedClockSources, fixedDirections: state.fixedDirections,
                immutableSettings: state.immutableSettings, notes: state.notes)
        }
        return state
    }
    func write(_ setting: TimeCardProfileSetting, expected: TimeCardProfileSetting) throws {
        guard state.values[setting.id] == expected else { throw TimeCardProfileError.invalid("Stale mock setter") }
        writes.append(setting)
        if rejectRollback && writes.count > failAtWrite { throw TimeCardProfileError.invalid("Rollback write failed") }
        if writes.count == failAtWrite {
            if failAfterWrite { replacing(setting) }
            if corruptOnFailure { replacing(.init(kind: setting.kind, channel: setting.channel, values: [0, 4])) }
            throw TimeCardProfileError.invalid("Injected write failure")
        }
        replacing(setting)
    }
}

@main enum ConfigurationProfileTests {
    static func rejected(_ body: () throws -> Void) {
        do { try body(); preconditionFailure("Expected rejection") } catch { }
    }
    static func main() throws {
        let date = Date(timeIntervalSince1970: 1_700_000_000)
        let target = TimeCardProfileTarget(vendorID: 0x1d9b, deviceID: 0x400, revision: 2,
                                          boardProfile: 1, layout: 2, clockVersion: 0x01080000)
        let original = TimeCardProfileState(serviceID: 123, target: target, settings: [
            .init(kind: .clockSource, channel: 0, values: [3]),
            .init(kind: .smaRoute, channel: 1, values: [0, 1]),
            .init(kind: .frequency, channel: 1, values: [1])
        ], supportedClockSources: [0, 1, 2, 3, 4, 5, 6, 0xfe, 0xff], fixedDirections: [:], immutableSettings: [], notes: [])
        let profile = original.profile(name: "Baseline", date: date)
        let data = try profile.encoded()
        precondition(try! TimeCardConfigurationProfile.decode(data) == profile)
        rejected { _ = try TimeCardConfigurationProfile.decode(Data(repeating: 32, count: 65537)) }
        rejected { _ = try TimeCardConfigurationProfile.decode(Data("not JSON".utf8)) }
        var root = try JSONSerialization.jsonObject(with: data) as! [String: Any]
        root["flashWrite"] = true
        rejected { _ = try TimeCardConfigurationProfile.decode(JSONSerialization.data(withJSONObject: root)) }
        var invalid = profile
        invalid.schemaVersion = 3
        rejected { try invalid.validate() }
        invalid = profile; invalid.name = ""
        rejected { try invalid.validate() }
        invalid.name = "bad\nname"
        rejected { try invalid.validate() }
        invalid.name = String(repeating: "é", count: 81)
        rejected { try invalid.validate() }
        invalid = profile; invalid.settings.append(profile.settings[0])
        rejected { try invalid.validate() }
        for setting in [
            TimeCardProfileSetting(kind: .clockSource, channel: 0, values: [7]),
            .init(kind: .clockSource, channel: 1, values: [3]),
            .init(kind: .frequency, channel: 0, values: [1]),
            .init(kind: .frequency, channel: 5, values: [1]),
            .init(kind: .frequency, channel: 1, values: [256]),
            .init(kind: .smaRoute, channel: 1, values: [2, 1]),
            .init(kind: .smaRoute, channel: 1, values: [0, 0x8000]),
            .init(kind: .smaRoute, channel: 1, values: [])
        ] { rejected { try setting.validate() } }

        let unchanged = try TimeCardProfilePlan.create(profile: profile, state: original, now: date)
        precondition(unchanged.canApply && unchanged.changes.isEmpty)
        let noop = MockProfiles(original)
        precondition(TimeCardProfileEngine.apply(unchanged, backend: noop, now: date).outcome == .unchanged)
        precondition(noop.writes.isEmpty)
        var desired = profile
        desired.settings = [
            .init(kind: .clockSource, channel: 0, values: [1]),
            .init(kind: .frequency, channel: 1, values: [10]),
            .init(kind: .smaRoute, channel: 1, values: [0, 2])
        ]
        let plan = try TimeCardProfilePlan.create(profile: desired, state: original, now: date)
        precondition(plan.changes.map(\.requested.kind) == [.smaRoute, .frequency, .clockSource])
        let success = MockProfiles(original)
        let result = TimeCardProfileEngine.apply(plan, backend: success, now: date)
        precondition(result.outcome == .applied && success.writes.count == 3)
        precondition(success.state.values == Dictionary(uniqueKeysWithValues: desired.settings.map { ($0.id, $0) }))
        let recovery = try TimeCardProfilePlan.create(profile: result.recoveryProfile!, state: success.state, now: date)
        precondition(TimeCardProfileEngine.apply(recovery, backend: success, now: date).successful)
        precondition(success.state == original)

        let stale = MockProfiles(original)
        stale.replacing(.init(kind: .frequency, channel: 1, values: [8]))
        precondition(TimeCardProfileEngine.apply(plan, backend: stale, now: date).outcome == .rejected)
        precondition(stale.writes.isEmpty)
        let expired = MockProfiles(original)
        precondition(TimeCardProfileEngine.apply(plan, backend: expired, now: date.addingTimeInterval(121)).outcome == .rejected)
        precondition(TimeCardProfileEngine.apply(plan, backend: expired, now: date.addingTimeInterval(-1)).outcome == .rejected)
        precondition(expired.writes.isEmpty)

        for afterWrite in [false, true] {
            let io = MockProfiles(original); io.failAtWrite = 2; io.failAfterWrite = afterWrite
            let result = TimeCardProfileEngine.apply(plan, backend: io, now: date)
            precondition(result.outcome == .rolledBack)
            precondition(io.state == original)
            precondition(io.writes.last?.kind == .smaRoute)
        }
        let brokenRollback = MockProfiles(original)
        brokenRollback.failAtWrite = 2; brokenRollback.failAfterWrite = true; brokenRollback.rejectRollback = true
        precondition(TimeCardProfileEngine.apply(plan, backend: brokenRollback, now: date).outcome == .recoveryRequired)
        let unknown = MockProfiles(original)
        unknown.failAtWrite = 1; unknown.corruptOnFailure = true
        let corrupt = TimeCardProfileEngine.apply(plan, backend: unknown, now: date)
        precondition(corrupt.outcome == .recoveryRequired && unknown.writes.count == 1)
        let changedMidway = MockProfiles(original); changedMidway.mutateAtRead = 4
        precondition(TimeCardProfileEngine.apply(plan, backend: changedMidway, now: date).outcome == .rolledBack)
        precondition(changedMidway.state.values["frequency:1"]?.values == [17])
        precondition(changedMidway.state.values["smaRoute:1"] == original.values["smaRoute:1"])
        let failedRead = MockProfiles(original); failedRead.failAtRead = 3
        precondition(TimeCardProfileEngine.apply(plan, backend: failedRead, now: date).outcome == .rolledBack)
        precondition(failedRead.state == original)
        let disconnected = MockProfiles(original); disconnected.failReadsFrom = 3
        let disconnectedReport = TimeCardProfileEngine.apply(plan, backend: disconnected, now: date)
        precondition(disconnectedReport.outcome == .recoveryRequired && disconnected.writes.count == 1)
        precondition(disconnectedReport.attemptedSettings == 1 && disconnectedReport.verifiedSettings == 0)
        precondition(disconnectedReport.recoveryProfile?.settings == [original.values["smaRoute:1"]!])
        let replacedMidway = MockProfiles(original); replacedMidway.replaceServiceAtRead = 3
        let replacedReport = TimeCardProfileEngine.apply(plan, backend: replacedMidway, now: date)
        precondition(replacedReport.outcome == .recoveryRequired && replacedMidway.writes.count == 1)
        precondition(replacedReport.serviceID == "123" && replacedMidway.state.serviceID == 456)

        let constrained = TimeCardProfileState(serviceID: 123, target: target, settings: original.settings,
            supportedClockSources: [3], fixedDirections: [1: 0], immutableSettings: ["smaRoute:1"], notes: [])
        let blocked = try TimeCardProfilePlan.create(profile: desired, state: constrained, now: date)
        precondition(!blocked.canApply && blocked.blockers.count == 2)
        let constrainedBackend = MockProfiles(constrained)
        precondition(TimeCardProfileEngine.apply(blocked, backend: constrainedBackend, now: date).outcome == .rejected)
        precondition(constrainedBackend.writes.isEmpty)
        let uncataloged = MockProfiles(original)
        uncataloged.replacing(.init(kind: .smaRoute, channel: 1, values: [0, 0x7fff]))
        let captureOnly = try TimeCardProfilePlan.create(profile: uncataloged.state.profile(name: "Unknown route"), state: uncataloged.state, now: date)
        precondition(captureOnly.canApply && captureOnly.changes.isEmpty)
        let unrestorable = try TimeCardProfilePlan.create(profile: desired, state: uncataloged.state, now: date)
        precondition(!unrestorable.canApply)
        precondition(TimeCardProfileEngine.apply(unrestorable, backend: uncataloged, now: date).outcome == .rejected)
        precondition(uncataloged.writes.isEmpty)
        var absent = profile; absent.settings = [.init(kind: .frequency, channel: 4, values: [1])]
        precondition(!(try! TimeCardProfilePlan.create(profile: absent, state: original, now: date)).canApply)
        let anotherCard = TimeCardProfileState(serviceID: 456, target: target, settings: original.settings,
            supportedClockSources: original.supportedClockSources, fixedDirections: [:], immutableSettings: [], notes: [])
        precondition(TimeCardProfileEngine.apply(plan, backend: MockProfiles(anotherCard), now: date).outcome == .rejected)
        let wrongTarget = TimeCardProfileTarget(vendorID: 0x18d4, deviceID: 0x1008, revision: 2,
            boardProfile: 2, layout: 2, clockVersion: 0x01080000)
        let incompatible = TimeCardProfileState(serviceID: 123, target: wrongTarget, settings: original.settings,
            supportedClockSources: original.supportedClockSources, fixedDirections: [:], immutableSettings: [], notes: [])
        precondition(!(try! TimeCardProfilePlan.create(profile: desired, state: incompatible, now: date)).canApply)
        var output = TimeCardPPSState(core: 1, version: 0x01060000, validFields: 31, control: 1,
            polarity: 1, pulseWidth: 100, cableDelayRaw: 0x80000019, maximumDelay: 0x3fffffff, writableFields: 29)
        let outputSetting = try TimeCardProfileSetting.pps(output)
        precondition(outputSetting.values == [0x01060000, 29, 1, 1, 100, UInt32(bitPattern: -25)])
        precondition(outputSetting.ppsState?.delayNanoseconds == -25)
        var input = TimeCardPPSState(core: 2, version: 0x01020000, validFields: 29, control: 1,
            polarity: 1, pulseWidth: 80, maximumDelay: 65535, writableFields: 21)
        let inputSetting = try TimeCardProfileSetting.pps(input)
        input.pulseWidth = 999; input.status = UInt32.max
        precondition(try! TimeCardProfileSetting.pps(input) == inputSetting, "Measurements must not make profiles stale")
        output.status = 1
        precondition(try! TimeCardProfileSetting.pps(output) == outputSetting, "Alarm state must never be serialized")
        let ppsOriginal = TimeCardProfileState(serviceID: 123, target: target,
            settings: original.settings + [outputSetting, inputSetting], supportedClockSources: original.supportedClockSources,
            fixedDirections: [:], immutableSettings: [], notes: [])
        let ppsProfile = ppsOriginal.profile(name: "PPS capture", date: date)
        precondition(ppsProfile.schemaVersion == 2)
        precondition(try! TimeCardConfigurationProfile.decode(ppsProfile.encoded()) == ppsProfile)
        invalid = ppsProfile; invalid.schemaVersion = 1
        rejected { try invalid.validate() }
        for (index, bad) in [(0, UInt32(0x01070000)), (1, 31), (2, 2), (3, 2), (4, 0), (4, 1000), (5, UInt32(bitPattern: Int32.min))] {
            var badSetting = outputSetting; badSetting.values[index] = bad
            rejected { try badSetting.validate() }
        }
        var badInput = inputSetting; badInput.values[4] = 80
        rejected { try badInput.validate() }
        badInput = inputSetting; badInput.values[5] = 65536
        rejected { try badInput.validate() }
        var changedOutput = outputSetting; changedOutput.values[4] = 250
        var changedInput = inputSetting; changedInput.values[5] = UInt32(bitPattern: -40)
        let ppsDesired = TimeCardConfigurationProfile(name: "PPS changes", capturedAt: date, target: target,
            settings: [changedInput, changedOutput, .init(kind: .clockSource, channel: 0, values: [1])])
        let ppsPlan = try TimeCardProfilePlan.create(profile: ppsDesired, state: ppsOriginal, now: date)
        precondition(ppsPlan.canApply && ppsPlan.changes.map(\.requested.kind) == [.pps, .pps, .clockSource])
        let ppsSuccess = MockProfiles(ppsOriginal)
        let ppsReport = TimeCardProfileEngine.apply(ppsPlan, backend: ppsSuccess, now: date)
        precondition(ppsReport.successful && ppsReport.recoveryProfile?.schemaVersion == 2)
        let restorePPS = try TimeCardProfilePlan.create(profile: ppsReport.recoveryProfile!, state: ppsSuccess.state, now: date)
        precondition(TimeCardProfileEngine.apply(restorePPS, backend: ppsSuccess, now: date).successful && ppsSuccess.state == ppsOriginal)
        for after in [true, false] {
            let failure = MockProfiles(ppsOriginal); failure.failAtWrite = 2; failure.failAfterWrite = after
            precondition(TimeCardProfileEngine.apply(ppsPlan, backend: failure, now: date).outcome == .rolledBack)
            precondition(failure.state == ppsOriginal)
        }
        let ppsStale = MockProfiles(ppsOriginal); ppsStale.replacing(changedInput)
        precondition(TimeCardProfileEngine.apply(ppsPlan, backend: ppsStale, now: date).outcome == .rejected && ppsStale.writes.isEmpty)
        var wrongCore = ppsDesired; wrongCore.settings = [outputSetting]
        wrongCore.settings[0].values[0] = 0x01050000
        precondition(!(try! TimeCardProfilePlan.create(profile: wrongCore, state: ppsOriginal, now: date)).canApply)
        let noopPPS = MockProfiles(ppsOriginal)
        let noopPPSPlan = try TimeCardProfilePlan.create(profile: ppsProfile, state: ppsOriginal, now: date)
        precondition(TimeCardProfileEngine.apply(noopPPSPlan, backend: noopPPS, now: date).outcome == .unchanged && noopPPS.writes.isEmpty)
        print("Profile tests passed: schema 1/2, PPS writable-only capture, core gates, ordering, no-op, stale/expired preview, rollback, and recovery conflicts.")
    }
}
