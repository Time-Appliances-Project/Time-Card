/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

// Sign and provision this test as a CLI client. It will never forward a write.
private final class ReadOnlyProfileBackend: TimeCardProfileBackend {
    let live: TimeCardLiveProfileBackend
    private(set) var attemptedWrites = 0
    init(_ live: TimeCardLiveProfileBackend) { self.live = live }
    func read() throws -> TimeCardProfileState { try live.read() }
    func write(_ setting: TimeCardProfileSetting, expected: TimeCardProfileSetting) throws {
        attemptedWrites += 1
        throw TimeCardProfileError.invalid("Hardware smoke tests prohibit writes.")
    }
}

@main enum ProfileHardwareSmoke {
    static func main() throws {
        let services = try TimeCardClient.discoverServices()
        guard services.count == 1, let descriptor = services.first else {
            throw TimeCardProfileError.invalid("This test requires exactly one connected Time Card.")
        }
        let backend = ReadOnlyProfileBackend(.init(descriptor: descriptor))
        let (captured, _) = try backend.live.capture(name: "Hardware no-write smoke test")
        let decoded = try TimeCardConfigurationProfile.decode(captured.encoded())
        let plan = try TimeCardProfilePlan.create(profile: decoded, state: backend.read())
        guard plan.canApply, plan.changes.isEmpty else {
            throw TimeCardProfileError.invalid("Captured profile did not round-trip as an unchanged preview.")
        }
        let report = TimeCardProfileEngine.apply(plan, backend: backend)
        guard report.outcome == .unchanged, backend.attemptedWrites == 0 else {
            throw TimeCardProfileError.invalid("No-write apply test failed: \(report.events)")
        }
        print(String(data: try captured.encoded(), encoding: .utf8)!)
        print("PASS: capture, strict JSON round-trip, preview, unchanged apply. \(captured.settings.count) settings; zero write attempts.")
    }
}
