/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation
@main struct PeripheralTests {
    static func check(_ value: Bool) { precondition(value) }
    static func rejects(_ action: () throws -> Void) {
        do { try action(); fatalError("Expected rejection") } catch {}
    }
    static func response(_ body: String) -> [UInt8] {
        Array(("[" + body + String(format: "|%02X]", SA53C3.checksum(body.utf8))).utf8)
    }
    static func main() throws {
        check(SA53C3.checksum("device?".utf8) == 0x27)
        let command = String(decoding: try SA53C3.request("get,Phase", sequence: 0xab), as: UTF8.self)
        check(command.hasPrefix("{get#AB,Phase|"))
        for bad in ["", "set,X,1}{store", "get,#01", "get,Alarms\n", String(repeating: "a", count: 221)] {
            rejects { _ = try SA53C3.request(bad, sequence: 0) }
        }
        var decoder = SA53C3.Decoder()
        let valid = response("#AB=\"hello] \\\"clock\\\"\"")
        for byte in valid.dropLast() { check(try decoder.append([byte], sequence: 0xab) == nil) }
        check(try decoder.append([valid.last!], sequence: 0xab) == "hello] \"clock\"")
        decoder = .init()
        check(try decoder.append(response("#AA=wrong") + response("#AB=sa5x"), sequence: 0xab) == "sa5x")
        decoder = .init()
        check(try decoder.append(Array("noise[>boot]".utf8) + response("#01=42"), sequence: 1) == "42")
        rejects { var d = SA53C3.Decoder(); _ = try d.append(Array("[#01=42|00]".utf8), sequence: 1) }
        rejects { var d = SA53C3.Decoder(); _ = try d.append(response("#01!5"), sequence: 1) }
        rejects { var d = SA53C3.Decoder(); _ = try d.append(Array(repeating: 0x5b, count: 8193), sequence: 1) }
        for parameter in SA53Parameter.writable {
            try parameter.validate(parameter.range.lowerBound); try parameter.validate(parameter.range.upperBound)
            rejects { try parameter.validate(parameter.range.lowerBound - 1) }
            rejects { try parameter.validate(parameter.range.upperBound + 1) }
        }
        rejects { try SA53Parameter.writable.first { $0.id == "PpsOffset" }!.validate(11) }
        let snapshot = SA53Snapshot(capturedAt: .now, serviceID: 1, values: ["Locked":"true", "Phase":"nan", "PowerSupply":"3300"], warnings: [])
        check(snapshot.boolean("Locked") == true && snapshot.boolean("Absent") == nil && snapshot.number("Phase") == nil)
        let alarm = SA53Snapshot(capturedAt: .now, serviceID: 1, values: ["Alarms":"131072", "Phase":"0", "PpsInDetected":"0", "Disciplining":"1", "JamSyncing":"0"], warnings: [])
        check(alarm.alarmDescription == "No PPS input" && alarm.phaseMeasurement == nil)
        var bytes = [UInt8](repeating: 0, count: 144)
        func put(_ offset: Int, _ value: UInt32) { for i in 0..<4 { bytes[offset + i] = UInt8(truncatingIfNeeded: value >> (8*i)) } }
        put(0, 144); put(8, 6); put(16, 0x4a); put(4, 0x403 | 4 | 16); put(36, 2); put(40, 0xc0); put(60, 16384); put(76, 768); put(80, 1024)
        let now = Date(); let sample = try MotionSample(bytes: bytes, timestamp: now)
        check(sample.quaternion?.eulerDegrees == MotionVector(x: 0,y: 0,z: 0))
        check(sample.acceleration == nil && sample.vibration == 5 && sample.fusionAccuracy == 3)
        check(sample.isFresh(at: now) && !sample.isFresh(at: now.addingTimeInterval(3)))
        check(MotionStatistics.rms([sample], now: now) == 5 && MotionStatistics.rms([sample], now: now.addingTimeInterval(61)) == nil)
        put(4, 0x403 | 4); put(60, 0)
        let missing = try MotionSample(bytes: bytes)
        check(missing.quaternion == nil && missing.vibration == nil && MotionStatistics.rms([missing], now: .now) == nil)
        check(MotionStatistics.csv([missing]).components(separatedBy: "\n")[1].hasSuffix(",,,,,,,,"))
        put(4, 0x403 | 16 | (1 << 11)); check(try MotionSample(bytes: bytes).vibration == nil)
        put(4, 3 | 16); check(try MotionSample(bytes: bytes).vibration == nil)
        put(128, 1); rejects { _ = try MotionSample(bytes: bytes) }
        rejects { _ = try MotionSample(bytes: []) }
        let rotated = MotionQuaternion(x: 0, y: 0, z: sqrt(0.5), w: sqrt(0.5))
        check(abs(rotated.eulerDegrees!.z - 90) < 0.00001)
        print("Peripheral tests passed: C3 checksums/sequence/fragmentation, bounds, missing data, quaternion, RMS, and export.")
    }
}
