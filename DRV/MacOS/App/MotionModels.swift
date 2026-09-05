/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation
import simd

enum MotionError: LocalizedError {
    case invalid(String)
    var errorDescription: String? { if case .invalid(let message) = self { return message }; return nil }
}

struct MotionVector: Codable, Equatable, Sendable {
    let x: Double, y: Double, z: Double
    var magnitude: Double { sqrt(x * x + y * y + z * z) }
    var simdValue: SIMD3<Double> { .init(x, y, z) }
}
struct MotionQuaternion: Codable, Equatable, Sendable {
    let x: Double, y: Double, z: Double, w: Double
    var normalized: simd_quatd? {
        let q = simd_quatd(ix: x, iy: y, iz: z, r: w)
        let length = simd_length(q.vector)
        guard length.isFinite, (0.8...1.2).contains(length) else { return nil }
        return simd_normalize(q)
    }
    var eulerDegrees: MotionVector? {
        guard let q = normalized else { return nil }
        let x = q.imag.x, y = q.imag.y, z = q.imag.z, w = q.real
        let roll = atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
        let pitch = asin(min(1, max(-1, 2 * (w * y - z * x))))
        let yaw = atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
        return .init(x: roll * 180 / .pi, y: pitch * 180 / .pi, z: yaw * 180 / .pi)
    }
}
struct MotionSample: Identifiable, Codable, Sendable {
    let id: UInt32
    let timestamp: Date
    let sensorType: UInt32
    let flags: UInt32
    let calibration: UInt32
    let reportCount: UInt32
    let quaternion: MotionQuaternion?
    let acceleration: MotionVector?
    let linearAcceleration: MotionVector?
    let gravity: MotionVector?
    let gyroscope: MotionVector?
    let magneticField: MotionVector?
    let temperature: Double?
    var sensorName: String { sensorType == 6 ? "BNO08x" : sensorType == 7 ? "BNO055" : "Unknown IMU" }
    var vibration: Double? { linearAcceleration?.magnitude }
    var fusionAccuracy: UInt32 { (calibration >> 6) & 3 }
    func isFresh(at now: Date) -> Bool { (0...2).contains(now.timeIntervalSince(timestamp)) }
    init(bytes: [UInt8], timestamp: Date = Date()) throws {
        guard bytes.count == 144 else { throw MotionError.invalid("Unexpected IMU response size.") }
        func word(_ offset: Int) -> UInt32 { (0..<4).reduce(0) { $0 | UInt32(bytes[offset + $1]) << (8 * $1) } }
        func scalar(_ offset: Int, _ divisor: Double) -> Double { Double(Int32(bitPattern: word(offset))) / divisor }
        guard word(0) == 144, [0, 6, 7].contains(word(8)), word(16) <= 0x77,
              stride(from: 128, to: 144, by: 4).allSatisfy({ word($0) == 0 }) else {
            throw MotionError.invalid("Invalid IMU response contract.")
        }
        self.id = word(32); self.timestamp = timestamp; sensorType = word(8); flags = word(4)
        calibration = word(40); reportCount = word(36)
        // A failed mux restore invalidates all measurements, regardless of data bits.
        let dataFlags = word(4)
        let valid = dataFlags & 0x403 == 0x403 && dataFlags & (1 << 11) == 0
        func vector(_ offset: Int, _ divisor: Double, _ flag: UInt32) -> MotionVector? {
            guard valid, dataFlags & flag != 0 else { return nil }
            return .init(x: scalar(offset, divisor), y: scalar(offset + 4, divisor), z: scalar(offset + 8, divisor))
        }
        let q = MotionQuaternion(x: scalar(48, 16384), y: scalar(52, 16384), z: scalar(56, 16384), w: scalar(60, 16384))
        quaternion = valid && flags & 4 != 0 && q.normalized != nil ? q : nil
        acceleration = vector(64, 256, 8); linearAcceleration = vector(76, 256, 16)
        gravity = vector(88, 256, 32); gyroscope = vector(100, 512, 64)
        magneticField = vector(112, 16, 128)
        temperature = valid && flags & 256 != 0 ? scalar(124, 128) : nil
    }
}
enum MotionStatistics {
    static func rms(_ samples: [MotionSample], now: Date) -> Double? {
        let values = samples.filter { (0...60).contains(now.timeIntervalSince($0.timestamp)) }.compactMap(\.vibration)
        guard !values.isEmpty else { return nil }
        return sqrt(values.reduce(0) { $0 + $1 * $1 } / Double(values.count))
    }
    static func csv(_ samples: [MotionSample]) -> String {
        let formatter = ISO8601DateFormatter(); formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        var lines = ["timestamp,sequence,sensor,accuracy,vibration_m_s2,linear_x_m_s2,linear_y_m_s2,linear_z_m_s2,qx,qy,qz,qw"]
        func number(_ value: Double?) -> String { value.map { String(format: "%.9g", locale: Locale(identifier: "en_US_POSIX"), $0) } ?? "" }
        for sample in samples {
            lines.append(([formatter.string(from: sample.timestamp), String(sample.id), sample.sensorName, String(sample.fusionAccuracy)] +
                [sample.vibration, sample.linearAcceleration?.x, sample.linearAcceleration?.y, sample.linearAcceleration?.z,
                 sample.quaternion?.x, sample.quaternion?.y, sample.quaternion?.z, sample.quaternion?.w].map(number)).joined(separator: ","))
        }
        return lines.joined(separator: "\n") + "\n"
    }
}
