/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation
// Sign as the provisioned CLI user-client. Identity/telemetry only: no SA53 settings writes.
@main struct PeripheralHardwareSmoke {
    static func main() throws {
        let services = try TimeCardClient.discoverServices()
        guard services.count == 1, let card = services.first else { fatalError("Exactly one card required") }
        if CommandLine.arguments.contains("--motion") {
            _ = try TimeCardClient.queryIMU(for: card, mode: 1)
            defer { _ = try? TimeCardClient.queryIMU(for: card, mode: 2) }
            var samples = [MotionSample]()
            for _ in 0..<40 {
                Thread.sleep(forTimeInterval: 0.25)
                samples.append(try TimeCardClient.queryIMU(for: card))
            }
            let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]; encoder.dateEncodingStrategy = .iso8601
            print(String(decoding: try encoder.encode(samples), as: UTF8.self))
            guard samples.contains(where: { $0.quaternion != nil }), samples.contains(where: { $0.linearAcceleration != nil }) else {
                throw MotionError.invalid("No valid quaternion or linear acceleration reached the host.")
            }
        } else {
            let snapshot = try SA53Client(descriptor: card).refresh()
            let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]; encoder.dateEncodingStrategy = .iso8601
            print(String(decoding: try encoder.encode(snapshot), as: UTF8.self))
        }
    }
}
