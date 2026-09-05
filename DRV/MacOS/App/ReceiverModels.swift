/* SPDX-License-Identifier: BSD-3-Clause */

import Foundation

struct TimeCardUBXPoll: Identifiable, Equatable, Sendable {
    let label: String
    let messageClass: UInt8
    let messageID: UInt8

    var id: String { label }

    static let allCases: [TimeCardUBXPoll] = [
        TimeCardUBXPoll(label: "MON-VER", messageClass: 0x0a, messageID: 0x04),
        TimeCardUBXPoll(label: "MON-HW", messageClass: 0x0a, messageID: 0x09),
        TimeCardUBXPoll(label: "MON-HW2", messageClass: 0x0a, messageID: 0x0b),
        TimeCardUBXPoll(label: "NAV-STATUS", messageClass: 0x01, messageID: 0x03),
        TimeCardUBXPoll(label: "NAV-PVT", messageClass: 0x01, messageID: 0x07),
        TimeCardUBXPoll(label: "NAV-DOP", messageClass: 0x01, messageID: 0x04),
        TimeCardUBXPoll(label: "NAV-CLOCK", messageClass: 0x01, messageID: 0x22),
        TimeCardUBXPoll(label: "NAV-TIMEGPS", messageClass: 0x01, messageID: 0x20),
        TimeCardUBXPoll(label: "NAV-TIMEUTC", messageClass: 0x01, messageID: 0x21),
        TimeCardUBXPoll(label: "NAV-TIMELS", messageClass: 0x01, messageID: 0x26),
        TimeCardUBXPoll(label: "NAV-SAT", messageClass: 0x01, messageID: 0x35),
        TimeCardUBXPoll(label: "NAV-SVIN", messageClass: 0x01, messageID: 0x3b),
        TimeCardUBXPoll(label: "TIM-TP", messageClass: 0x0d, messageID: 0x01),
    ]

    var packet: [UInt8] {
        var body = [messageClass, messageID, 0x00, 0x00]
        var checksumA: UInt8 = 0
        var checksumB: UInt8 = 0
        for byte in body {
            checksumA &+= byte
            checksumB &+= checksumA
        }
        body.insert(contentsOf: [0xb5, 0x62], at: 0)
        body.append(checksumA)
        body.append(checksumB)
        return body
    }

    var hexString: String {
        packet.map { String(format: "%02x", $0) }.joined(separator: " ")
    }
}

struct ReceiverSatelliteSignal: Identifiable, Equatable, Sendable {
    let source: String
    let constellation: String
    let satelliteID: String
    let cn0: Int?
    let elevation: Int?
    let azimuth: Int?
    let usedInFix: Bool?
    let quality: String
    let flags: UInt32?

    var id: String {
        [
            source,
            constellation,
            satelliteID,
            elevationText,
            azimuthText,
            cn0Text,
        ].joined(separator: "|")
    }

    var cn0Text: String {
        cn0.map { "\($0) dB-Hz" } ?? "Unavailable"
    }

    var elevationText: String {
        elevation.map { "\($0)°" } ?? "Unavailable"
    }

    var azimuthText: String {
        azimuth.map { "\($0)°" } ?? "Unavailable"
    }

    var usedText: String {
        switch usedInFix {
        case true:
            return "Used"
        case false:
            return "Tracked"
        case nil:
            return "Unknown"
        }
    }

    var flagsText: String {
        flags.map { String(format: "0x%08x", $0) } ?? "Unavailable"
    }
}

struct TimeCardUBXFrame: Identifiable, Equatable, Sendable {
    let id = UUID()
    let offset: Int
    let messageClass: UInt8
    let messageID: UInt8
    let length: UInt16
    let payload: [UInt8]
    let expectedChecksumA: UInt8
    let expectedChecksumB: UInt8
    let actualChecksumA: UInt8
    let actualChecksumB: UInt8

    var checksumValid: Bool {
        expectedChecksumA == actualChecksumA &&
            expectedChecksumB == actualChecksumB
    }

    var messageText: String {
        String(
            format: "%@ 0x%02x/0x%02x",
            messageName,
            messageClass,
            messageID
        )
    }

    var checksumText: String {
        String(
            format: "expected %02x %02x, actual %02x %02x",
            expectedChecksumA,
            expectedChecksumB,
            actualChecksumA,
            actualChecksumB
        )
    }

    var messageName: String {
        switch (messageClass, messageID) {
        case (0x01, 0x03): "NAV-STATUS"
        case (0x01, 0x04): "NAV-DOP"
        case (0x01, 0x07): "NAV-PVT"
        case (0x01, 0x20): "NAV-TIMEGPS"
        case (0x01, 0x21): "NAV-TIMEUTC"
        case (0x01, 0x22): "NAV-CLOCK"
        case (0x01, 0x26): "NAV-TIMELS"
        case (0x01, 0x35): "NAV-SAT"
        case (0x01, 0x3B): "NAV-SVIN"
        case (0x05, 0x01): "ACK-ACK"
        case (0x05, 0x00): "ACK-NAK"
        case (0x06, 0x08): "CFG-RATE"
        case (0x06, 0x24): "CFG-NAV5"
        case (0x0A, 0x04): "MON-VER"
        case (0x0A, 0x09): "MON-HW"
        case (0x0A, 0x0B): "MON-HW2"
        case (0x0D, 0x01): "TIM-TP"
        default: "UBX"
        }
    }

    var summary: String {
        switch (messageClass, messageID) {
        case (0x01, 0x07):
            return navPVTSummary
        case (0x01, 0x03):
            return navStatusSummary
        case (0x01, 0x04):
            return navDOPSummary
        case (0x01, 0x20):
            return navTimeGPSSummary
        case (0x01, 0x21):
            return navTimeUTCSummary
        case (0x01, 0x22):
            return navClockSummary
        case (0x01, 0x26):
            return navTimeLSSummary
        case (0x01, 0x35):
            return navSATSummary
        case (0x01, 0x3B):
            return navSVINSummary
        case (0x06, 0x08):
            return cfgRateSummary
        case (0x0A, 0x04):
            return monVersionSummary
        case (0x0A, 0x09):
            return monHWSummary
        case (0x0A, 0x0B):
            return monHW2Summary
        case (0x0D, 0x01):
            return timTPSummary
        default:
            return "Payload \(length) byte(s)."
        }
    }

    var navSatelliteSignals: [ReceiverSatelliteSignal] {
        guard messageName == "NAV-SAT",
              payload.count >= 8 else {
            return []
        }
        let reported = Int(payload[5])
        let available = max(0, (payload.count - 8) / 12)
        let count = min(reported, available)
        guard count > 0 else { return [] }

        return (0..<count).map { index in
            let offset = 8 + index * 12
            let gnssID = payload[offset]
            let svID = payload[offset + 1]
            let cn0 = Int(payload[offset + 2])
            let elevation = Int(Int8(bitPattern: payload[offset + 3]))
            let azimuth = Int(Self.readInt16(payload, at: offset + 4))
            let flags = Self.readUInt32(payload, at: offset + 8)
            return ReceiverSatelliteSignal(
                source: "UBX NAV-SAT",
                constellation: Self.gnssName(gnssID),
                satelliteID: String(svID),
                cn0: cn0 == 0 ? nil : cn0,
                elevation: elevation,
                azimuth: azimuth,
                usedInFix: (flags & 0x08) != 0,
                quality: "quality \(flags & 0x07)",
                flags: flags
            )
        }
    }

    private var navPVTSummary: String {
        guard payload.count >= 92 else {
            return "NAV-PVT payload is shorter than expected."
        }
        let year = Self.readUInt16(payload, at: 4)
        let month = payload[6]
        let day = payload[7]
        let hour = payload[8]
        let minute = payload[9]
        let second = payload[10]
        let fixType = payload[20]
        let flags = payload[21]
        let satellites = payload[23]
        let lon = Double(Self.readInt32(payload, at: 24)) / 10_000_000
        let lat = Double(Self.readInt32(payload, at: 28)) / 10_000_000
        let hMSL = Double(Self.readInt32(payload, at: 36)) / 1_000
        let hAcc = Double(Self.readUInt32(payload, at: 40)) / 1_000
        let fixOK = (flags & 0x01) != 0
        return String(
            format: "UTC %04u-%02u-%02u %02u:%02u:%02u, fix %@ (%u), %u SV, lat %.7f, lon %.7f, hMSL %.3f m, hAcc %.3f m.",
            year,
            month,
            day,
            hour,
            minute,
            second,
            fixOK ? "OK" : "not OK",
            fixType,
            satellites,
            lat,
            lon,
            hMSL,
            hAcc
        )
    }

    private var navStatusSummary: String {
        guard payload.count >= 16 else {
            return "NAV-STATUS payload is shorter than expected."
        }
        let fixType = payload[4]
        let flags = payload[5]
        let fixOK = (flags & 0x01) != 0
        let timeToFirstFix = Self.readUInt32(payload, at: 8)
        return "Fix type \(fixType), fix \(fixOK ? "OK" : "not OK"), time-to-first-fix \(timeToFirstFix) ms."
    }

    private var navDOPSummary: String {
        guard payload.count >= 18 else {
            return "NAV-DOP payload is shorter than expected."
        }
        let gDOP = Double(Self.readUInt16(payload, at: 4)) * 0.01
        let pDOP = Double(Self.readUInt16(payload, at: 6)) * 0.01
        let tDOP = Double(Self.readUInt16(payload, at: 8)) * 0.01
        let vDOP = Double(Self.readUInt16(payload, at: 10)) * 0.01
        let hDOP = Double(Self.readUInt16(payload, at: 12)) * 0.01
        let nDOP = Double(Self.readUInt16(payload, at: 14)) * 0.01
        let eDOP = Double(Self.readUInt16(payload, at: 16)) * 0.01
        return String(
            format: "DOP g %.2f, p %.2f, t %.2f, v %.2f, h %.2f, n %.2f, e %.2f.",
            gDOP,
            pDOP,
            tDOP,
            vDOP,
            hDOP,
            nDOP,
            eDOP
        )
    }

    private var navTimeGPSSummary: String {
        guard payload.count >= 16 else {
            return "NAV-TIMEGPS payload is shorter than expected."
        }
        let towMS = Self.readUInt32(payload, at: 0)
        let towNS = Self.readInt32(payload, at: 4)
        let week = Self.readInt16(payload, at: 8)
        let leapSeconds = Int(Int8(bitPattern: payload[10]))
        let valid = payload[11]
        let timeAccuracy = Self.readUInt32(payload, at: 12)
        return "GPS week \(week), TOW \(towMS) ms + \(towNS) ns, leap seconds \(leapSeconds), valid flags 0x\(String(format: "%02x", valid)), accuracy \(timeAccuracy) ns."
    }

    private var navTimeUTCSummary: String {
        guard payload.count >= 20 else {
            return "NAV-TIMEUTC payload is shorter than expected."
        }
        let timeAccuracy = Self.readUInt32(payload, at: 4)
        let nano = Self.readInt32(payload, at: 8)
        let year = Self.readUInt16(payload, at: 12)
        let month = payload[14]
        let day = payload[15]
        let hour = payload[16]
        let minute = payload[17]
        let second = payload[18]
        let valid = payload[19]
        return String(
            format: "UTC %04u-%02u-%02u %02u:%02u:%02u + %d ns, valid flags 0x%02x, accuracy %u ns.",
            year,
            month,
            day,
            hour,
            minute,
            second,
            nano,
            valid,
            timeAccuracy
        )
    }

    private var navClockSummary: String {
        guard payload.count >= 20 else {
            return "NAV-CLOCK payload is shorter than expected."
        }
        let bias = Self.readInt32(payload, at: 4)
        let drift = Self.readInt32(payload, at: 8)
        let timeAccuracy = Self.readUInt32(payload, at: 12)
        let frequencyAccuracy = Self.readUInt32(payload, at: 16)
        return "Clock bias \(bias) ns, drift \(drift) ns/s, time accuracy \(timeAccuracy) ns, frequency accuracy \(frequencyAccuracy) ps/s."
    }

    private var navTimeLSSummary: String {
        guard payload.count >= 24 else {
            return "NAV-TIMELS payload is shorter than expected."
        }
        let currentLeapSeconds = Int(Int8(bitPattern: payload[8]))
        let leapChange = Int(Int8(bitPattern: payload[10]))
        let timeToEvent = Self.readInt32(payload, at: 12)
        let eventWeek = Self.readUInt16(payload, at: 16)
        let eventDay = Self.readUInt16(payload, at: 18)
        let valid = payload[23]
        return "Current leap seconds \(currentLeapSeconds), next change \(leapChange), event in \(timeToEvent) s at GPS week \(eventWeek) day \(eventDay), valid flags 0x\(String(format: "%02x", valid))."
    }

    private var navSATSummary: String {
        guard payload.count >= 8 else {
            return "NAV-SAT payload is shorter than expected."
        }
        let signals = navSatelliteSignals
        guard !signals.isEmpty else {
            return "Version \(payload[4]), \(payload[5]) satellite record(s)."
        }
        let used = signals.filter { $0.usedInFix == true }.count
        let cn0Values = signals.compactMap(\.cn0)
        let averageText: String
        if cn0Values.isEmpty {
            averageText = ""
        } else {
            let average = Double(cn0Values.reduce(0, +)) / Double(cn0Values.count)
            averageText = String(format: ", average C/N0 %.1f dB-Hz", average)
        }
        let constellationText = Dictionary(
            grouping: signals,
            by: \.constellation
        )
        .map { key, value in "\(key) \(value.count)" }
        .sorted()
        .joined(separator: ", ")
        return "\(signals.count) visible, \(used) used\(averageText), \(constellationText)."
    }

    private var navSVINSummary: String {
        guard payload.count >= 40 else {
            return "NAV-SVIN payload is shorter than expected."
        }
        let duration = Self.readUInt32(payload, at: 8)
        let observations = Self.readUInt32(payload, at: 12)
        let meanAcc = Double(Self.readUInt32(payload, at: 28)) / 10_000
        let valid = payload[36] != 0
        let active = payload[37] != 0
        return String(
            format: "Survey-in %@, active %@, duration %u s, %u obs, mean accuracy %.4f m.",
            valid ? "valid" : "not valid",
            active ? "yes" : "no",
            duration,
            observations,
            meanAcc
        )
    }

    private var cfgRateSummary: String {
        guard payload.count >= 6 else {
            return "CFG-RATE payload is shorter than expected."
        }
        let measurementRate = Self.readUInt16(payload, at: 0)
        let navigationRate = Self.readUInt16(payload, at: 2)
        let timeRef = Self.readUInt16(payload, at: 4)
        return "Measurement \(measurementRate) ms, navigation \(navigationRate), time reference \(timeRef)."
    }

    private var monVersionSummary: String {
        guard payload.count >= 40 else {
            return "MON-VER payload is shorter than expected."
        }
        let software = Self.cString(payload[0..<30])
        let hardware = Self.cString(payload[30..<40])
        return "Software \(software), hardware \(hardware)."
    }

    private var monHWSummary: String {
        guard payload.count >= 24 else {
            return "MON-HW payload is shorter than expected."
        }
        let noise = Self.readUInt16(payload, at: 16)
        let agc = Self.readUInt16(payload, at: 18)
        let antennaStatus = Self.antennaStatusName(payload[20])
        let antennaPower = Self.antennaPowerName(payload[21])
        let flags = payload[22]
        let usedMask = payload.count >= 28 ?
            Self.readUInt32(payload, at: 24) : 0
        return "Antenna \(antennaStatus), power \(antennaPower), AGC \(agc), noise \(noise), flags 0x\(String(format: "%02x", flags)), used mask 0x\(String(format: "%08x", usedMask))."
    }

    private var monHW2Summary: String {
        guard payload.count >= 28 else {
            return "MON-HW2 payload is shorter than expected."
        }
        let offsetI = Self.readInt8(payload, at: 0)
        let magI = payload[1]
        let offsetQ = Self.readInt8(payload, at: 2)
        let magQ = payload[3]
        let cfgSource = payload[4]
        let lowLevel = Self.readUInt32(payload, at: 8)
        let postStatus = Self.readUInt32(payload, at: 16)
        return "I/Q offset \(offsetI)/\(offsetQ), I/Q magnitude \(magI)/\(magQ), config source \(cfgSource), low level \(lowLevel), POST 0x\(String(format: "%08x", postStatus))."
    }

    private var timTPSummary: String {
        guard payload.count >= 16 else {
            return "TIM-TP payload is shorter than expected."
        }
        let towMS = Self.readUInt32(payload, at: 0)
        let towSubMS = Self.readUInt32(payload, at: 4)
        let week = Self.readUInt16(payload, at: 8)
        let flags = payload[14]
        return "Time pulse TOW \(towMS) ms, sub-ms \(towSubMS), week \(week), flags 0x\(String(format: "%02x", flags))."
    }

    static func parseFrames(from bytes: [UInt8]) -> [TimeCardUBXFrame] {
        guard bytes.count >= 8 else { return [] }
        var frames: [TimeCardUBXFrame] = []
        var offset = 0
        while offset + 8 <= bytes.count {
            guard bytes[offset] == 0xb5, bytes[offset + 1] == 0x62 else {
                offset += 1
                continue
            }
            let messageClass = bytes[offset + 2]
            let messageID = bytes[offset + 3]
            let length = UInt16(bytes[offset + 4]) |
                (UInt16(bytes[offset + 5]) << 8)
            let frameLength = Int(length) + 8
            guard offset + frameLength <= bytes.count else {
                break
            }
            let payloadStart = offset + 6
            let payloadEnd = payloadStart + Int(length)
            let payload = Array(bytes[payloadStart..<payloadEnd])
            let expectedA = bytes[payloadEnd]
            let expectedB = bytes[payloadEnd + 1]
            let actual = checksum(
                messageClass: messageClass,
                messageID: messageID,
                length: length,
                payload: payload
            )
            frames.append(
                TimeCardUBXFrame(
                    offset: offset,
                    messageClass: messageClass,
                    messageID: messageID,
                    length: length,
                    payload: payload,
                    expectedChecksumA: expectedA,
                    expectedChecksumB: expectedB,
                    actualChecksumA: actual.0,
                    actualChecksumB: actual.1
                )
            )
            offset += frameLength
        }
        return frames
    }

    private static func checksum(
        messageClass: UInt8,
        messageID: UInt8,
        length: UInt16,
        payload: [UInt8]
    ) -> (UInt8, UInt8) {
        var ckA: UInt8 = 0
        var ckB: UInt8 = 0
        func add(_ byte: UInt8) {
            ckA &+= byte
            ckB &+= ckA
        }
        add(messageClass)
        add(messageID)
        add(UInt8(length & 0xff))
        add(UInt8(length >> 8))
        for byte in payload {
            add(byte)
        }
        return (ckA, ckB)
    }

    private static func readUInt16(_ bytes: [UInt8], at offset: Int) -> UInt16 {
        UInt16(bytes[offset]) | (UInt16(bytes[offset + 1]) << 8)
    }

    private static func readUInt32(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        UInt32(bytes[offset]) |
            (UInt32(bytes[offset + 1]) << 8) |
            (UInt32(bytes[offset + 2]) << 16) |
            (UInt32(bytes[offset + 3]) << 24)
    }

    private static func readInt16(_ bytes: [UInt8], at offset: Int) -> Int16 {
        Int16(bitPattern: readUInt16(bytes, at: offset))
    }

    private static func readInt8(_ bytes: [UInt8], at offset: Int) -> Int8 {
        Int8(bitPattern: bytes[offset])
    }

    private static func readInt32(_ bytes: [UInt8], at offset: Int) -> Int32 {
        Int32(bitPattern: readUInt32(bytes, at: offset))
    }

    private static func gnssName(_ identifier: UInt8) -> String {
        switch identifier {
        case 0: "GPS"
        case 1: "SBAS"
        case 2: "Galileo"
        case 3: "BeiDou"
        case 5: "QZSS"
        case 6: "GLONASS"
        case 7: "NavIC"
        default: "GNSS \(identifier)"
        }
    }

    private static func antennaStatusName(_ value: UInt8) -> String {
        switch value {
        case 0: "initializing"
        case 1: "unknown"
        case 2: "OK"
        case 3: "short"
        case 4: "open"
        default: "status \(value)"
        }
    }

    private static func antennaPowerName(_ value: UInt8) -> String {
        switch value {
        case 0: "off"
        case 1: "on"
        case 2: "unknown"
        default: "power \(value)"
        }
    }

    private static func cString(_ bytes: ArraySlice<UInt8>) -> String {
        let prefix = bytes.prefix { $0 != 0 }
        guard !prefix.isEmpty else { return "Unavailable" }
        return String(decoding: prefix, as: UTF8.self)
    }
}

enum ReceiverStreamDecoder {
    static func protocolSummary(messages: [ReceiverStreamMessage]) -> String {
        guard !messages.isEmpty else {
            return "No UBX, NMEA, or RTCM3 messages decoded."
        }
        let ubx = messages.filter { $0.protocolName == "UBX" }.count
        let nmea = messages.filter { $0.protocolName == "NMEA" }.count
        let rtcm = messages.filter { $0.protocolName == "RTCM3" }.count
        return "\(nmea) NMEA, \(ubx) UBX, \(rtcm) RTCM3."
    }

    static func checksumSummary(messages: [ReceiverStreamMessage]) -> String {
        guard !messages.isEmpty else {
            return "No checksums decoded yet."
        }
        let ok = messages.filter { $0.checksumState == .ok }.count
        let failed = messages.filter { $0.checksumState == .failed }.count
        let missing = messages.filter { $0.checksumState == .missing }.count
        let unchecked = messages.filter { $0.checksumState == .notChecked }.count
        return "\(ok) OK, \(failed) failed, \(missing) missing, \(unchecked) unchecked."
    }

    static func rtcmSummary(messages: [ReceiverStreamMessage]) -> String {
        let rtcmMessages = messages.filter { $0.protocolName == "RTCM3" }
        guard let latest = rtcmMessages.last else {
            return "No RTCM3 correction frames decoded."
        }
        let typeText = latest.rtcmMessageType.map {
            "latest \(rtcmMessageLabel($0))"
        } ?? "latest message type unavailable"
        let failed = rtcmMessages.filter { $0.checksumState == .failed }.count
        let failureText = failed == 0 ? "all CRCs OK" : "\(failed) CRC failure(s)"
        return "\(rtcmMessages.count) RTCM3 frame(s), \(typeText), \(failureText)."
    }

    static func rtcmMessageLabel(_ messageType: Int) -> String {
        switch messageType {
        case 1001: return "GPS L1 RTK observables (type 1001)"
        case 1002: return "extended GPS L1 RTK observables (type 1002)"
        case 1003: return "GPS L1/L2 RTK observables (type 1003)"
        case 1004: return "extended GPS L1/L2 RTK observables (type 1004)"
        case 1005: return "stationary RTK reference-station ARP (type 1005)"
        case 1006: return "stationary RTK ARP with antenna height (type 1006)"
        case 1007: return "antenna descriptor (type 1007)"
        case 1008: return "antenna descriptor and serial number (type 1008)"
        case 1019: return "GPS ephemeris (type 1019)"
        case 1020: return "GLONASS ephemeris (type 1020)"
        case 1033: return "receiver and antenna descriptor (type 1033)"
        case 1042: return "BeiDou ephemeris (type 1042)"
        case 1044: return "QZSS ephemeris (type 1044)"
        case 1045: return "Galileo F/NAV ephemeris (type 1045)"
        case 1046: return "Galileo I/NAV ephemeris (type 1046)"
        case 1071...1077:
            return "GPS MSM\(messageType - 1070) (type \(messageType))"
        case 1081...1087:
            return "GLONASS MSM\(messageType - 1080) (type \(messageType))"
        case 1091...1097:
            return "Galileo MSM\(messageType - 1090) (type \(messageType))"
        case 1101...1107:
            return "SBAS MSM\(messageType - 1100) (type \(messageType))"
        case 1111...1117:
            return "QZSS MSM\(messageType - 1110) (type \(messageType))"
        case 1121...1127:
            return "BeiDou MSM\(messageType - 1120) (type \(messageType))"
        case 1230:
            return "GLONASS code-phase bias (type 1230)"
        default:
            return "message type \(messageType)"
        }
    }

    static func satelliteSignals(
        nmeaSentences: [NMEASentence],
        ubxFrames: [TimeCardUBXFrame]
    ) -> [ReceiverSatelliteSignal] {
        if let ubxFrame = ubxFrames.last(where: {
            $0.checksumValid && $0.messageName == "NAV-SAT"
        }) {
            let signals = ubxFrame.navSatelliteSignals
            if !signals.isEmpty {
                return signals
            }
        }
        var latest: [String: ReceiverSatelliteSignal] = [:]
        for sentence in nmeaSentences where sentence.formatter == "GSV" &&
            (!sentence.raw.contains("*") || sentence.checksumValid) {
            // The first page begins a fresh view for this talker. Historical
            // epochs must not multiply sky-map markers during long replay.
            if sentence.fields.count >= 2 && sentence.fields[1] == "1" {
                latest = latest.filter { $0.value.source != "NMEA \(sentence.label)" }
            }
            for signal in sentence.gsvSatelliteSignals {
                latest["\(signal.constellation)|\(signal.satelliteID)"] = signal
            }
        }
        return latest.values.sorted {
            if $0.constellation != $1.constellation { return $0.constellation < $1.constellation }
            return (Int($0.satelliteID) ?? 0) < (Int($1.satelliteID) ?? 0)
        }
    }

    static func satelliteSource(
        nmeaSentences: [NMEASentence],
        ubxFrames: [TimeCardUBXFrame]
    ) -> String {
        if ubxFrames.last(where: {
            $0.checksumValid &&
                $0.messageName == "NAV-SAT" &&
                !$0.navSatelliteSignals.isEmpty
        }) != nil {
            return "Latest UBX NAV-SAT frame"
        }
        if nmeaSentences.contains(where: {
            $0.formatter == "GSV" &&
                (!$0.raw.contains("*") || $0.checksumValid) &&
                !$0.gsvSatelliteSignals.isEmpty
        }) {
            return "Decoded NMEA GSV sentences"
        }
        return "No satellite signal records decoded"
    }

    static func satelliteCSVText(
        signals: [ReceiverSatelliteSignal],
        csvLine: ([String]) -> String
    ) -> String {
        var rows = [csvLine([
            "source",
            "constellation",
            "satellite_id",
            "c_n0_dbhz",
            "elevation_deg",
            "azimuth_deg",
            "used_in_fix",
            "quality",
            "flags",
        ])]
        for signal in signals {
            rows.append(csvLine([
                signal.source,
                signal.constellation,
                signal.satelliteID,
                signal.cn0.map(String.init) ?? "",
                signal.elevation.map(String.init) ?? "",
                signal.azimuth.map(String.init) ?? "",
                signal.usedInFix.map { $0 ? "yes" : "no" } ?? "",
                signal.quality,
                signal.flags.map { String(format: "0x%08x", $0) } ?? "",
            ]))
        }
        return rows.joined(separator: "\n")
    }
}

enum ReceiverStreamChecksumState: Equatable, Sendable {
    case ok
    case failed
    case missing
    case notChecked

    var label: String {
        switch self {
        case .ok: "Checksum OK"
        case .failed: "Checksum fail"
        case .missing: "No checksum"
        case .notChecked: "Not checked"
        }
    }

}

struct ReceiverStreamMessage: Identifiable, Equatable, Sendable {
    let offset: Int
    let byteCount: Int
    let protocolName: String
    let name: String
    let summary: String
    let detail: String
    let checksumState: ReceiverStreamChecksumState
    let rtcmMessageType: Int?

    var id: String {
        "\(offset)-\(byteCount)-\(protocolName)-\(name)"
    }

    var offsetText: String {
        String(format: "0x%04x", max(0, offset))
    }

    init(
        offset: Int,
        byteCount: Int,
        protocolName: String,
        name: String,
        summary: String,
        detail: String,
        checksumState: ReceiverStreamChecksumState,
        rtcmMessageType: Int? = nil
    ) {
        self.offset = offset
        self.byteCount = byteCount
        self.protocolName = protocolName
        self.name = name
        self.summary = summary
        self.detail = detail
        self.checksumState = checksumState
        self.rtcmMessageType = rtcmMessageType
    }

    init(sentence: NMEASentence, offset: Int, byteCount: Int) {
        self.init(
            offset: offset,
            byteCount: byteCount,
            protocolName: "NMEA",
            name: sentence.label,
            summary: sentence.summary,
            detail: sentence.raw,
            checksumState: !sentence.raw.contains("*")
                ? .missing
                : (sentence.checksumValid ? .ok : .failed)
        )
    }

    init(sentence: NMEASentence, index: Int) {
        self.init(
            sentence: sentence,
            offset: index,
            byteCount: Array(sentence.raw.utf8).count
        )
    }

    init(frame: TimeCardUBXFrame, offset: Int, byteCount: Int) {
        self.init(
            offset: offset,
            byteCount: byteCount,
            protocolName: "UBX",
            name: frame.messageName,
            summary: frame.summary,
            detail: frame.checksumText,
            checksumState: frame.checksumValid ? .ok : .failed
        )
    }

    init(frame: TimeCardUBXFrame, index: Int) {
        self.init(
            frame: frame,
            offset: frame.offset == 0 ? index : frame.offset,
            byteCount: Int(frame.length) + 8
        )
    }

    static func rtcm3(
        offset: Int,
        payloadLength: Int,
        byteCount: Int,
        messageType: Int?,
        expectedCRC: UInt32,
        calculatedCRC: UInt32
    ) -> ReceiverStreamMessage {
        let crcValid = expectedCRC == calculatedCRC
        let typeText = messageType.map {
            "\(ReceiverStreamDecoder.rtcmMessageLabel($0)), "
        } ?? ""
        return ReceiverStreamMessage(
            offset: offset,
            byteCount: byteCount,
            protocolName: "RTCM3",
            name: "RTCM3",
            summary: "RTCM3 \(typeText)payload \(payloadLength) byte(s).",
            detail: String(
                format: "CRC24Q expected 0x%06x, calculated 0x%06x.",
                expectedCRC,
                calculatedCRC
            ),
            checksumState: crcValid ? .ok : .failed,
            rtcmMessageType: messageType
        )
    }

    static func parse(from bytes: [UInt8], limit: Int = 20_000) -> [ReceiverStreamMessage] {
        guard !bytes.isEmpty else { return [] }
        var messages: [ReceiverStreamMessage] = []
        var index = 0
        while index < bytes.count && messages.count < limit {
            if let parsed = parseUBX(in: bytes, at: index) {
                messages.append(parsed.message)
                index += parsed.consumed
                continue
            }
            if let parsed = parseNMEA(in: bytes, at: index) {
                messages.append(parsed.message)
                index += parsed.consumed
                continue
            }
            if let parsed = parseRTCM3(in: bytes, at: index) {
                messages.append(parsed.message)
                index += parsed.consumed
                continue
            }
            index += 1
        }
        return messages
    }

    private static func parseUBX(
        in bytes: [UInt8],
        at offset: Int
    ) -> (message: ReceiverStreamMessage, consumed: Int)? {
        guard offset + 8 <= bytes.count,
              bytes[offset] == 0xb5,
              bytes[offset + 1] == 0x62 else {
            return nil
        }
        let length = Int(bytes[offset + 4]) | (Int(bytes[offset + 5]) << 8)
        let frameLength = length + 8
        guard offset + frameLength <= bytes.count else {
            return nil
        }
        let fragment = Array(bytes[offset..<(offset + frameLength)])
        guard let frame = TimeCardUBXFrame.parseFrames(from: fragment).first else {
            return nil
        }
        return (
            ReceiverStreamMessage(
                frame: frame,
                offset: offset,
                byteCount: frameLength
            ),
            frameLength
        )
    }

    private static func parseNMEA(
        in bytes: [UInt8],
        at offset: Int
    ) -> (message: ReceiverStreamMessage, consumed: Int)? {
        guard bytes[offset] == 0x24 else { return nil }
        var end = offset + 1
        while end < bytes.count,
              bytes[end] != 0x0a,
              bytes[end] != 0x0d {
            // Bound malformed lines and avoid swallowing a following binary frame.
            guard end - offset < 1024,
                  bytes[end] >= 0x20, bytes[end] <= 0x7e,
                  bytes[end] != 0x24 else { return nil }
            end += 1
        }
        guard end > offset + 1 else { return nil }
        let lineBytes = Array(bytes[offset..<end])
        let line = String(decoding: lineBytes, as: UTF8.self)
        // A checksum terminates a sentence even when the capture ends before CR/LF.
        guard end < bytes.count || (line.lastIndex(of: "*").map {
            line.distance(from: $0, to: line.endIndex) == 3
        } ?? false) else { return nil }
        guard let sentence = NMEASentence.parse(line) else {
            return nil
        }
        var consumedEnd = end
        while consumedEnd < bytes.count,
              bytes[consumedEnd] == 0x0a || bytes[consumedEnd] == 0x0d {
            consumedEnd += 1
        }
        return (
            ReceiverStreamMessage(
                sentence: sentence,
                offset: offset,
                byteCount: consumedEnd - offset
            ),
            consumedEnd - offset
        )
    }

    private static func parseRTCM3(
        in bytes: [UInt8],
        at offset: Int
    ) -> (message: ReceiverStreamMessage, consumed: Int)? {
        guard offset + 6 <= bytes.count,
              bytes[offset] == 0xd3 else {
            return nil
        }
        guard (bytes[offset + 1] & 0xfc) == 0 else {
            return nil
        }
        let payloadLength = (Int(bytes[offset + 1] & 0x03) << 8) |
            Int(bytes[offset + 2])
        let frameLength = 3 + payloadLength + 3
        guard payloadLength <= 1023,
              offset + frameLength <= bytes.count else {
            return nil
        }
        let crcOffset = offset + 3 + payloadLength
        let expectedCRC = (UInt32(bytes[crcOffset]) << 16) |
            (UInt32(bytes[crcOffset + 1]) << 8) |
            UInt32(bytes[crcOffset + 2])
        let calculatedCRC = crc24q(bytes[offset..<crcOffset])
        let messageType = payloadLength >= 2
            ? ((Int(bytes[offset + 3]) << 4) | (Int(bytes[offset + 4]) >> 4))
            : nil
        return (
            ReceiverStreamMessage.rtcm3(
                offset: offset,
                payloadLength: payloadLength,
                byteCount: frameLength,
                messageType: messageType,
                expectedCRC: expectedCRC,
                calculatedCRC: calculatedCRC
            ),
            frameLength
        )
    }

    private static func crc24q(_ bytes: ArraySlice<UInt8>) -> UInt32 {
        var crc: UInt32 = 0
        for byte in bytes {
            crc ^= UInt32(byte) << 16
            for _ in 0..<8 {
                crc <<= 1
                if (crc & 0x01000000) != 0 {
                    crc ^= 0x01864cfb
                }
            }
            crc &= 0x00ffffff
        }
        return crc
    }
}

struct NMEASentence: Identifiable, Equatable, Sendable {
    let raw: String
    let talker: String
    let formatter: String
    let fields: [String]
    let expectedChecksum: UInt8?
    let computedChecksum: UInt8
    let checksumValid: Bool

    var id: String {
        raw
    }

    var label: String {
        talker + formatter
    }

    var summary: String {
        switch formatter {
        case "GGA":
            return ggaSummary
        case "RMC":
            return rmcSummary
        case "GSA":
            return gsaSummary
        case "GSV":
            return gsvSummary
        case "GLL":
            return gllSummary
        case "VTG":
            return vtgSummary
        case "GNS":
            return gnsSummary
        case "GST":
            return gstSummary
        case "TXT":
            return txtSummary
        case "HDT":
            return hdtSummary
        case "THS":
            return thsSummary
        case "ZDA":
            return zdaSummary
        default:
            return "\(label) with \(fields.count) field(s)."
        }
    }

    var gsvSatelliteSignals: [ReceiverSatelliteSignal] {
        guard formatter == "GSV" else { return [] }
        var signals: [ReceiverSatelliteSignal] = []
        var index = 3
        while index + 3 < fields.count {
            guard let satelliteID = field(index) else {
                index += 4
                continue
            }
            signals.append(
                ReceiverSatelliteSignal(
                    source: "NMEA \(label)",
                    constellation: nmeaConstellationName,
                    satelliteID: satelliteID,
                    cn0: intField(index + 3),
                    elevation: intField(index + 1),
                    azimuth: intField(index + 2),
                    usedInFix: nil,
                    quality: "reported",
                    flags: nil
                )
            )
            index += 4
        }
        return signals
    }

    static func parse(_ line: String) -> NMEASentence? {
        let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
        guard trimmed.hasPrefix("$") else { return nil }
        let withoutDollar = trimmed.dropFirst()
        let parts = withoutDollar.split(
            separator: "*",
            maxSplits: 1,
            omittingEmptySubsequences: false
        )
        guard let body = parts.first, body.count >= 5 else {
            return nil
        }

        let fieldParts = body.split(
            separator: ",",
            omittingEmptySubsequences: false
        ).map(String.init)
        guard let sentenceType = fieldParts.first,
              sentenceType.count == 5,
              sentenceType.utf8.allSatisfy({ (0x41...0x5a).contains($0) || (0x30...0x39).contains($0) }) else {
            return nil
        }

        let typeIndex = sentenceType.index(
            sentenceType.startIndex,
            offsetBy: 2
        )
        let talker = String(sentenceType[..<typeIndex])
        let formatter = String(sentenceType[typeIndex...])
        let fields = Array(fieldParts.dropFirst())
        let computed = checksum(for: String(body))
        let expected = parts.count == 2 && parts[1].count == 2
            ? UInt8(parts[1], radix: 16) : nil

        return NMEASentence(
            raw: trimmed,
            talker: talker,
            formatter: formatter,
            fields: fields,
            expectedChecksum: expected,
            computedChecksum: computed,
            checksumValid: expected.map { $0 == computed } ?? false
        )
    }

    private static func checksum(for body: String) -> UInt8 {
        body.utf8.reduce(UInt8(0)) { partial, byte in
            partial ^ byte
        }
    }

    private var ggaSummary: String {
        let time = field(0, fallback: "unknown time")
        let latitude = coordinate(value: field(1), hemisphere: field(2))
        let longitude = coordinate(value: field(3), hemisphere: field(4))
        let quality = ggaQuality(field(5))
        let satellites = field(6, fallback: "?")
        let altitude = field(8, fallback: "?") + " " + field(9, fallback: "m")
        return "Fix \(quality), \(satellites) satellites, \(latitude), \(longitude), altitude \(altitude), time \(time)."
    }

    private var rmcSummary: String {
        let time = field(0, fallback: "unknown time")
        let status = field(1) == "A" ? "active" : "void"
        let latitude = coordinate(value: field(2), hemisphere: field(3))
        let longitude = coordinate(value: field(4), hemisphere: field(5))
        let speed = field(6, fallback: "?")
        let date = field(8, fallback: "unknown date")
        return "Recommended minimum \(status), \(latitude), \(longitude), \(speed) knots, date \(date), time \(time)."
    }

    private var gsaSummary: String {
        let mode = field(0, fallback: "?")
        let fixType = gsaFixType(field(1))
        let pdop = field(14, fallback: "?")
        let hdop = field(15, fallback: "?")
        let vdop = field(16, fallback: "?")
        return "DOP mode \(mode), fix \(fixType), PDOP \(pdop), HDOP \(hdop), VDOP \(vdop)."
    }

    private var gsvSummary: String {
        let messageNumber = field(1, fallback: "?")
        let messageCount = field(0, fallback: "?")
        let satellites = field(2, fallback: "?")
        return "Satellites in view \(satellites), message \(messageNumber) of \(messageCount)."
    }

    private var gllSummary: String {
        let latitude = coordinate(value: field(0), hemisphere: field(1))
        let longitude = coordinate(value: field(2), hemisphere: field(3))
        let time = field(4, fallback: "unknown time")
        let status = field(5) == "A" ? "active" : "void"
        let mode = field(6, fallback: "unknown mode")
        return "Geographic position \(status), \(latitude), \(longitude), time \(time), mode \(mode)."
    }

    private var vtgSummary: String {
        let trueCourse = field(0, fallback: "?")
        let magneticCourse = field(2, fallback: "?")
        let speedKnots = field(4, fallback: "?")
        let speedKilometers = field(6, fallback: "?")
        let mode = field(8, fallback: "unknown mode")
        return "Course \(trueCourse)° true, \(magneticCourse)° magnetic, speed \(speedKnots) knots, \(speedKilometers) km/h, mode \(mode)."
    }

    private var gnsSummary: String {
        let time = field(0, fallback: "unknown time")
        let latitude = coordinate(value: field(1), hemisphere: field(2))
        let longitude = coordinate(value: field(3), hemisphere: field(4))
        let mode = field(5, fallback: "unknown mode")
        let satellites = field(6, fallback: "?")
        let hdop = field(7, fallback: "?")
        let altitude = field(8, fallback: "?") + " m"
        return "GNSS fix mode \(mode), \(satellites) satellites, HDOP \(hdop), \(latitude), \(longitude), altitude \(altitude), time \(time)."
    }

    private var gstSummary: String {
        let time = field(0, fallback: "unknown time")
        let rms = field(1, fallback: "?")
        let major = field(2, fallback: "?")
        let minor = field(3, fallback: "?")
        let orientation = field(4, fallback: "?")
        let latitudeSigma = field(5, fallback: "?")
        let longitudeSigma = field(6, fallback: "?")
        let altitudeSigma = field(7, fallback: "?")
        return "Pseudo-range noise RMS \(rms) m, error ellipse \(major)/\(minor) m at \(orientation)°, lat sigma \(latitudeSigma) m, lon sigma \(longitudeSigma) m, alt sigma \(altitudeSigma) m, time \(time)."
    }

    private var txtSummary: String {
        let messageNumber = field(1, fallback: "?")
        let messageCount = field(0, fallback: "?")
        let messageType = field(2, fallback: "?")
        let text = field(3, fallback: "empty text")
        return "Text message \(messageNumber) of \(messageCount), type \(messageType): \(text)"
    }

    private var hdtSummary: String {
        let heading = field(0, fallback: "?")
        return "Heading \(heading)° true."
    }

    private var thsSummary: String {
        let heading = field(0, fallback: "?")
        let status = thsStatus(field(1))
        return "True heading \(heading)°, status \(status)."
    }

    private var zdaSummary: String {
        let time = field(0, fallback: "unknown time")
        let day = field(1, fallback: "??")
        let month = field(2, fallback: "??")
        let year = field(3, fallback: "????")
        return "UTC date \(year)-\(month)-\(day), time \(time)."
    }

    private func field(_ index: Int) -> String? {
        guard fields.indices.contains(index),
              !fields[index].isEmpty else {
            return nil
        }
        return fields[index]
    }

    private func field(_ index: Int, fallback: String) -> String {
        field(index) ?? fallback
    }

    private func intField(_ index: Int) -> Int? {
        guard let text = field(index) else { return nil }
        return Int(text)
    }

    private func coordinate(value: String?, hemisphere: String?) -> String {
        guard let value,
              let hemisphere,
              let numeric = Double(value) else {
            return "coordinate unavailable"
        }
        let degreeDigits = hemisphere == "N" || hemisphere == "S" ? 2 : 3
        guard value.count > degreeDigits else {
            return "coordinate unavailable"
        }
        let degreeText = String(value.prefix(degreeDigits))
        guard let degrees = Double(degreeText) else {
            return "coordinate unavailable"
        }
        let minutes = numeric - degrees * 100.0
        var decimal = degrees + minutes / 60.0
        if hemisphere == "S" || hemisphere == "W" {
            decimal *= -1.0
        }
        return String(format: "%.6f° %@", decimal, hemisphere)
    }

    private func ggaQuality(_ code: String?) -> String {
        switch code {
        case "0": "invalid"
        case "1": "GPS"
        case "2": "DGPS"
        case "4": "RTK fixed"
        case "5": "RTK float"
        case "6": "estimated"
        default: code ?? "unknown"
        }
    }

    private func gsaFixType(_ code: String?) -> String {
        switch code {
        case "1": "none"
        case "2": "2-D"
        case "3": "3-D"
        default: code ?? "unknown"
        }
    }

    private func thsStatus(_ code: String?) -> String {
        switch code {
        case "A": "autonomous"
        case "E": "estimated"
        case "M": "manual"
        case "S": "simulated"
        case "V": "invalid"
        default: code ?? "unknown"
        }
    }

    private var nmeaConstellationName: String {
        switch talker {
        case "GP": "GPS"
        case "GL": "GLONASS"
        case "GA": "Galileo"
        case "GB", "BD": "BeiDou"
        case "GQ": "QZSS"
        case "GN": "Mixed GNSS"
        default: talker
        }
    }
}
