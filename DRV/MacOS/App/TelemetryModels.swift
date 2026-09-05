/* SPDX-License-Identifier: BSD-3-Clause */

import Foundation

struct TimeCardTelemetrySample: Identifiable, Codable, Equatable, Sendable {
    let id: UUID
    let timestamp: Date
    let serviceID: String
    let samplingWindowNanoseconds: Double
    let clockInSync: Bool?
    let seenSatellites: Int?
    let lockedSatellites: Int?
    let temperaturesCelsius: [String: Double]

    init(timestamp: Date, serviceID: String, samplingWindowNanoseconds: Double,
         clockInSync: Bool?, seenSatellites: Int?, lockedSatellites: Int?,
         temperaturesCelsius: [String: Double]) {
        self.id = UUID()
        self.timestamp = timestamp
        self.serviceID = serviceID
        self.samplingWindowNanoseconds = samplingWindowNanoseconds
        self.clockInSync = clockInSync
        self.seenSatellites = seenSatellites
        self.lockedSatellites = lockedSatellites
        self.temperaturesCelsius = temperaturesCelsius.filter { $0.value.isFinite }
    }
}

struct SamplingWindowStatistics {
    let values: [Double]
    init(_ values: [Double]) { self.values = values.filter { $0.isFinite && $0 >= 0 }.sorted() }

    func percentile(_ fraction: Double) -> Double? {
        guard !values.isEmpty, fraction.isFinite else { return nil }
        let index = Double(values.count - 1) * min(1, max(0, fraction))
        let lower = Int(index)
        let upper = min(values.count - 1, lower + 1)
        return values[lower] + (values[upper] - values[lower]) * (index - Double(lower))
    }

    struct Bin: Identifiable {
        let id: Int
        let lower: Double
        let upper: Double
        var count: Int
    }

    func histogram(binCount: Int = 24) -> [Bin] {
        guard let first = values.first, let last = values.last else { return [] }
        let count = min(80, max(1, binCount))
        let padding = max(1, max(abs(last) * 0.02, (last - first) * 0.04))
        let minimum = max(0, first - padding)
        let maximum = last + padding
        let width = (maximum - minimum) / Double(count)
        guard width.isFinite, width > 0 else {
            return [Bin(id: 0, lower: first, upper: last, count: values.count)]
        }
        var bins = (0..<count).map {
            Bin(id: $0, lower: minimum + Double($0) * width,
                upper: minimum + Double($0 + 1) * width, count: 0)
        }
        for value in values {
            let index = min(count - 1, max(0, Int((value - minimum) / width)))
            bins[index].count += 1
        }
        return bins
    }
}

struct TimeCardTelemetryRecording: Sendable {
    static let maximumSamples = 21_600
    let startedAt: Date
    private(set) var stoppedAt: Date?
    private(set) var stopReason: String?
    private(set) var samples: [TimeCardTelemetrySample] = []
    var isRecording: Bool { stoppedAt == nil }

    mutating func append(_ sample: TimeCardTelemetrySample) {
        guard isRecording else { return }
        if let first = samples.first, first.serviceID != sample.serviceID {
            stop(at: sample.timestamp, reason: "Active Time Card changed")
            return
        }
        samples.append(sample)
        if samples.count >= Self.maximumSamples {
            stop(at: sample.timestamp, reason: "21,600-sample recording limit reached")
        }
    }

    mutating func stop(at date: Date = Date(), reason: String = "Stopped by operator") {
        guard isRecording else { return }
        stoppedAt = date
        stopReason = reason
    }
}

enum TimeCardTelemetryExport {
    static func json(_ samples: [TimeCardTelemetrySample], startedAt: Date? = nil,
                     stoppedAt: Date? = nil, stopReason: String? = nil) throws -> Data {
        struct Export: Encodable {
            let schemaVersion = 1
            let timeBasis = "UTC host observation timestamps; sampling windows are bracket durations, not PHC offsets"
            let startedAt: Date?
            let stoppedAt: Date?
            let stopReason: String?
            let samples: [TimeCardTelemetrySample]
        }
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try encoder.encode(Export(startedAt: startedAt, stoppedAt: stoppedAt,
                                         stopReason: stopReason, samples: samples))
    }

    static func csv(_ samples: [TimeCardTelemetrySample]) -> String {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        let sensorNames = Array(Set(samples.flatMap { $0.temperaturesCelsius.keys })).sorted()
        var rows = [ReceiverCaptureDocument.csv([
            "timestamp_utc", "service_id", "sampling_window_ns", "clock_in_sync",
            "satellites_seen", "satellites_locked",
        ] + sensorNames.map { "temperature_celsius:\($0)" })]
        rows += samples.map { sample in
            ReceiverCaptureDocument.csv([
                formatter.string(from: sample.timestamp), sample.serviceID,
                String(sample.samplingWindowNanoseconds),
                sample.clockInSync.map { $0 ? "true" : "false" } ?? "",
                sample.seenSatellites.map(String.init) ?? "",
                sample.lockedSatellites.map(String.init) ?? "",
            ] + sensorNames.map { sample.temperaturesCelsius[$0].map { String($0) } ?? "" })
        }
        return rows.joined(separator: "\r\n") + "\r\n"
    }
}
