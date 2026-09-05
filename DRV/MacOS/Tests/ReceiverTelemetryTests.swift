/* SPDX-License-Identifier: BSD-3-Clause */

import Foundation

@main
enum ReceiverTelemetryTests {
    static func main() throws {
        let gga = Array("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n".utf8)
        let poll: [UInt8] = [0xb5, 0x62, 0x0a, 0x04, 0, 0, 0x0e, 0x34]
        precondition(TimeCardUBXPoll.allCases.first!.packet == poll)
        let rtcm = rtcmPacket([0x43, 0x20])
        var badRTCM = rtcm
        badRTCM[badRTCM.count - 1] ^= 0x01
        let bytes = [UInt8(0xff), 0x00] + gga + poll + rtcm + badRTCM
        let document = ReceiverCaptureDocument(source: "Test \"receiver\", lab", bytes: bytes)
        precondition(document.messages.count == 4)
        precondition(document.messages.map(\.protocolName) == ["NMEA", "UBX", "RTCM3", "RTCM3"])
        precondition(document.messages.map(\.offset) == [2, 2 + gga.count, 2 + gga.count + 8, 2 + gga.count + 8 + rtcm.count])
        precondition(document.messages.map(\.checksumState) == [.ok, .ok, .ok, .failed])
        precondition(document.messages[2].rtcmMessageType == 1074)
        precondition(document.decodedBytes == bytes.count - 2)
        precondition(document.payload(for: document.messages[0]).elementsEqual(gga))
        precondition(document.filtered(protocolName: "RTCM3", errorsOnly: true, search: "GPS MSM4").count == 1)
        precondition(document.filtered(protocolName: "UBX", errorsOnly: false, search: "mon-ver").count == 1)
        let json = try JSONSerialization.jsonObject(with: document.exportJSON(document.messages)) as! [String: Any]
        precondition(json["captureByteCount"] as! Int == bytes.count)
        precondition((json["messages"] as! [[String: Any]])[1]["hex"] as! String == "B5 62 0A 04 00 00 0E 34")
        precondition(document.exportCSV(document.messages).contains("\"NMEA\",\"GPGGA\""))
        precondition(ReceiverCaptureDocument.csv(["a,\"b\"\r\nc"]) == "\"a,\"\"b\"\"\r\nc\"")
        precondition(document.exportText([]).contains("Export: 0 matching messages"))
        precondition(ReceiverRawFormat.hex.render([0x01, 0xff][...]) == "00000000  01 FF")
        precondition(ReceiverRawFormat.binary.render([0x01, 0xff][...]) == "00000000  00000001 11111111")
        precondition(ReceiverRawFormat.decimal.render([0x01, 0xff][...]) == "00000000  001 255")
        precondition(ReceiverRawFormat.ascii.render([0x41, 0x00, 0x0a][...]) == "A·\n")

        // Truncation at every byte boundary must not manufacture a complete frame.
        for frame in [poll, rtcm] {
            for count in 0..<frame.count {
                precondition(ReceiverStreamMessage.parse(from: Array(frame.prefix(count))).isEmpty)
            }
        }
        precondition(ReceiverStreamMessage.parse(from: Array("$GPGGA,partial".utf8)).isEmpty)
        precondition(ReceiverStreamMessage.parse(from: Array("$GPGGA,partial".utf8) + poll).last?.protocolName == "UBX")
        precondition(ReceiverStreamMessage.parse(from: Array(repeating: 0x24, count: 100_000) + poll).count == 1)
        precondition(ReceiverStreamMessage.parse(from: [0xd3, 0xfc, 0] + poll).first?.name == "MON-VER")
        precondition(ReceiverStreamMessage.parse(from: gga + poll, limit: 1).count == 1)
        precondition(ReceiverStreamMessage.parse(from: gga, limit: 0).isEmpty)
        precondition(ReceiverStreamMessage.parse(from: Array("$GPGGA,1*ZZ\r\n".utf8)).first?.checksumState == .failed)
        precondition(ReceiverStreamMessage.parse(from: Array("$GPGGA,1\r\n".utf8)).first?.checksumState == .missing)
        precondition(NMEASentence.parse("$GPGGA,1*00extra")?.checksumValid == false)
        precondition(NMEASentence.parse("$bad name,1*00") == nil)
        let gsv = ["$GPGSV,1,1,02,01,40,100,30,02,50,200,35", "$GPGSV,1,1,01,02,51,201,36"]
            .compactMap(NMEASentence.parse)
        let latestSignals = ReceiverStreamDecoder.satelliteSignals(nmeaSentences: gsv, ubxFrames: [])
        precondition(latestSignals.count == 1 && latestSignals[0].satelliteID == "02" && latestSignals[0].cn0 == 36)
        for poll in TimeCardUBXPoll.allCases {
            for length in 0...100 {
                let packet = ubxPacket(poll.messageClass, poll.messageID, Array(repeating: 0xff, count: length))
                let frame = TimeCardUBXFrame.parseFrames(from: packet).first!
                precondition(frame.checksumValid)
                _ = frame.summary
                _ = frame.navSatelliteSignals
            }
        }

        // File limits are enforced on the actual read, not just a metadata check.
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let replay = directory.appendingPathComponent("receiver.bin")
        try Data(bytes).write(to: replay)
        let replayBytes = try ReceiverCaptureDocument.readFile(replay)
        precondition(replayBytes == bytes)
        let handle = try FileHandle(forWritingTo: replay)
        try handle.truncate(atOffset: UInt64(ReceiverCaptureDocument.maximumBytes + 1))
        try handle.close()
        do {
            _ = try ReceiverCaptureDocument.readFile(replay)
            preconditionFailure("Oversized replay accepted")
        } catch ReceiverCaptureDocument.ReplayError.tooLarge { }

        let stats = SamplingWindowStatistics([100, 200, 300, 400, .nan, .infinity, -1])
        precondition(stats.percentile(0.5) == 250)
        precondition(stats.percentile(0.95) == 385)
        precondition(stats.percentile(0.99) == 397)
        precondition(stats.histogram().reduce(0) { $0 + $1.count } == 4)
        precondition(SamplingWindowStatistics([]).percentile(0.5) == nil)
        precondition(SamplingWindowStatistics([0, 0]).histogram().reduce(0) { $0 + $1.count } == 2)
        precondition(SamplingWindowStatistics([12]).percentile(0.99) == 12)
        precondition(SamplingWindowStatistics([Double(UInt64.max)]).histogram().reduce(0) { $0 + $1.count } == 1)
        let sample = makeSample(service: "123", temperature: 24.5)
        var recording = TimeCardTelemetryRecording(startedAt: sample.timestamp)
        recording.append(sample)
        recording.append(makeSample(service: "456", temperature: nil))
        precondition(!recording.isRecording && recording.samples.count == 1)
        recording.append(sample)
        precondition(recording.samples.count == 1)
        var bounded = TimeCardTelemetryRecording(startedAt: sample.timestamp)
        for _ in 0..<(TimeCardTelemetryRecording.maximumSamples + 1) { bounded.append(sample) }
        precondition(!bounded.isRecording && bounded.samples.count == TimeCardTelemetryRecording.maximumSamples)
        let missing = makeSample(service: "123", temperature: nil)
        let csv = TimeCardTelemetryExport.csv([sample, missing])
        precondition(csv.contains("temperature_celsius:LM75B 0x48"))
        precondition(csv.hasSuffix("\"123\",\"900.0\",\"\",\"\",\"\",\"\"\r\n"))
        let telemetry = try JSONSerialization.jsonObject(with: TimeCardTelemetryExport.json([sample, missing])) as! [String: Any]
        let rows = telemetry["samples"] as! [[String: Any]]
        precondition(rows.count == 2 && rows[1]["lockedSatellites"] == nil)
        precondition(rows[0]["timestamp"] as! String == "2023-11-14T22:13:20Z")
        print("Receiver and telemetry tests passed: framing, corruption, replay, exports, statistics, and recording boundaries.")
    }

    static func makeSample(service: String, temperature: Double?) -> TimeCardTelemetrySample {
        TimeCardTelemetrySample(timestamp: Date(timeIntervalSince1970: 1_700_000_000), serviceID: service,
                                samplingWindowNanoseconds: 900, clockInSync: nil, seenSatellites: nil,
                                lockedSatellites: nil, temperaturesCelsius: temperature.map { ["LM75B 0x48": $0] } ?? [:])
    }

    static func ubxPacket(_ cls: UInt8, _ id: UInt8, _ payload: [UInt8]) -> [UInt8] {
        let body = [cls, id, UInt8(payload.count & 0xff), UInt8(payload.count >> 8)] + payload
        var a: UInt8 = 0, b: UInt8 = 0
        for byte in body { a &+= byte; b &+= a }
        return [0xb5, 0x62] + body + [a, b]
    }

    static func rtcmPacket(_ payload: [UInt8]) -> [UInt8] {
        let body = [UInt8(0xd3), UInt8(payload.count >> 8), UInt8(payload.count & 0xff)] + payload
        var crc: UInt32 = 0
        // Table-form reference CRC, separate from the decoder's bit-at-a-time loop.
        let table: [UInt32] = (0..<256).map { value in
            var entry = UInt32(value) << 16
            for _ in 0..<8 { entry = (entry << 1) ^ ((entry & 0x800000) != 0 ? 0x1864cfb : 0) }
            return entry & 0xffffff
        }
        for byte in body { crc = ((crc << 8) & 0xffffff) ^ table[Int((crc >> 16) ^ UInt32(byte))] }
        return body + [UInt8(crc >> 16), UInt8((crc >> 8) & 0xff), UInt8(crc & 0xff)]
    }
}
