/* SPDX-License-Identifier: BSD-3-Clause */

import CoreFoundation
import Darwin
import Foundation
import IOKit
import IOKit.serial

struct TimeCardServiceDescriptor: Identifiable, Hashable, Sendable {
    let id: UInt64

    var displayName: String {
        String(format: "Time Card %016llX", id)
    }
}

struct TimeCardDeviceSnapshot: Equatable, Sendable {
    let service: TimeCardServiceDescriptor
    let abiVersion: UInt32
    let driverVersion: UInt32
    let vendorID: UInt16
    let deviceID: UInt16
    let pciRevision: UInt32
    let boardProfile: UInt32
    let layout: UInt32
    let advertisedMSIXVectors: UInt32
    let barSize: UInt64
    let clockOffset: UInt64
    let todOffset: UInt64
    let capabilities: UInt32
    let validFields: UInt64
    let clockVersion: UInt32
    let clockStatus: UInt32
    let clockSelect: UInt32
    let todVersion: UInt32
    let todStatus: UInt32
    let utcStatus: UInt32
    let leap: UInt32
    let gnssStatus: UInt32
    let satellites: UInt32
    let cardSeconds: UInt64
    let cardNanoseconds: UInt32
    let systemTimeBeforeNanoseconds: UInt64
    let systemTimeAfterNanoseconds: UInt64

    var boardName: String {
        switch boardProfile {
        case 1: "Meta/Facebook Time Card"
        case 2: "Celestica R4006"
        case 3: "Orolia/Safran ART"
        case 4: "ADVA Time Card"
        case 5: "ADVA Time Card X1"
        default: "Unknown Time Card"
        }
    }

    var pciIdentity: String {
        String(format: "%04x:%04x", vendorID, deviceID)
    }

    var driverVersionText: String {
        "\(driverVersion >> 16).\(driverVersion & 0xffff)"
    }

    var layoutName: String {
        switch layout {
        case 1: "Classic"
        case 2: "Shifted LitePCIe"
        case 3: "ART"
        default: "Unknown"
        }
    }

    var sampleWindowNanoseconds: UInt64 {
        guard systemTimeAfterNanoseconds >= systemTimeBeforeNanoseconds else {
            return 0
        }
        return systemTimeAfterNanoseconds - systemTimeBeforeNanoseconds
    }

    var systemMidpointNanoseconds: UInt64 {
        guard systemTimeAfterNanoseconds >= systemTimeBeforeNanoseconds else {
            return systemTimeBeforeNanoseconds
        }
        return systemTimeBeforeNanoseconds
            + (systemTimeAfterNanoseconds - systemTimeBeforeNanoseconds) / 2
    }

    var clockInSync: Bool? {
        guard hasValidField(1 << 1) else { return nil }
        return (clockStatus & 1) != 0
    }

    var configuredClockSource: UInt32? {
        guard hasValidField(1 << 2) else { return nil }
        return clockSelect & 0xff
    }

    var todTelemetryAvailable: Bool {
        hasValidField(1 << 5) && hasValidField(1 << 6)
    }

    var gnssTelemetryAvailable: Bool {
        hasValidField(1 << 7) && hasValidField(1 << 8)
    }

    var utcOffsetSeconds: UInt32? {
        guard todTelemetryAvailable else { return nil }
        return utcStatus & 0xff
    }

    var utcOffsetValid: Bool? {
        guard todTelemetryAvailable else { return nil }
        return (utcStatus & (1 << 8)) != 0
    }

    var leapInformationValid: Bool? {
        guard todTelemetryAvailable else { return nil }
        return (utcStatus & (1 << 16)) != 0
    }

    var seenSatellites: UInt32? {
        guard gnssTelemetryAvailable else { return nil }
        return satellites & 0xff
    }

    var lockedSatellites: UInt32? {
        guard gnssTelemetryAvailable else { return nil }
        return (satellites >> 8) & 0xff
    }

    var satelliteDataValid: Bool? {
        guard gnssTelemetryAvailable else { return nil }
        return (satellites & (1 << 16)) != 0
    }

    var gnssFixOK: Bool? {
        guard gnssTelemetryAvailable else { return nil }
        return (gnssStatus & (1 << 16)) != 0
    }

    var gnssFixValidityBitSet: Bool? {
        guard gnssTelemetryAvailable else { return nil }
        return (gnssStatus & (1 << 28)) != 0
    }

    var gnssFixCode: UInt32? {
        guard gnssTelemetryAvailable else { return nil }
        return (gnssStatus >> 17) & 0xff
    }

    var gnssFixName: String {
        guard let code = gnssFixCode else { return "Not available" }
        switch code {
        case 0: return "No fix"
        case 1: return "Dead reckoning"
        case 2: return "2-D fix"
        case 3: return "3-D fix"
        case 4: return "GNSS + dead reckoning"
        default: return "Unknown"
        }
    }

    var capabilityNames: [String] {
        var names: [String] = []
        if (capabilities & (1 << 0)) != 0 { names.append("Clock read") }
        if (capabilities & (1 << 1)) != 0 { names.append("Clock set") }
        if (capabilities & (1 << 2)) != 0 { names.append("Cross timestamp") }
        if (capabilities & (1 << 3)) != 0 { names.append("ToD") }
        if (capabilities & (1 << 4)) != 0 { names.append("SMA") }
        if (capabilities & (1 << 5)) != 0 { names.append("LEDs") }
        if (capabilities & (1 << 6)) != 0 { names.append("I2C") }
        if (capabilities & (1 << 7)) != 0 { names.append("Sensors") }
        if (capabilities & (1 << 8)) != 0 { names.append("UART") }
        if abiVersion >= 11 && (capabilities & (1 << 11)) != 0 { names.append("Fused IMU") }
        if supportsClockSource { names.append("Clock source") }
        if supportsFrequency { names.append("Frequency counters") }
        return names
    }

    var supportsUART: Bool {
        abiVersion >= 8 && (capabilities & (1 << 8)) != 0
    }

    var supportsClockSource: Bool { abiVersion >= 10 && capabilities & (1 << 9) != 0 }
    var supportsFrequency: Bool { abiVersion >= 10 && capabilities & (1 << 10) != 0 }

    var supportsUARTWrite: Bool {
        abiVersion >= 9 && supportsUART
    }

    func hasValidField(_ field: UInt64) -> Bool {
        (validFields & field) != 0
    }
}

struct TimeCardClockControlState: Equatable, Sendable, Codable {
    var size: UInt32 = 32
    var source: UInt32 = 0
    var activeSource: UInt32 = 0
    var supportedSources: UInt32 = 0
    var clockVersion: UInt32 = 0
    var control: UInt32 = 0
    var status: UInt32 = 0
    var reserved: UInt32 = 0

    static let knownSources: [UInt32] = [0, 1, 2, 3, 4, 5, 6, 0xfe, 0xff]
    static func name(_ source: UInt32) -> String {
        switch source {
        case 0: return "None"
        case 1: return "Time of Day"
        case 2: return "IRIG"
        case 3: return "PPS"
        case 4: return "PTP"
        case 5: return "RTC"
        case 6: return "DCF"
        case 7: return "NTP"
        case 8: return "SyncE"
        case 0xfd: return "Dynamic"
        case 0xfe: return "Registers"
        case 0xff: return "External"
        default: return String(format: "Unknown (0x%02x)", source)
        }
    }
    func supports(_ source: UInt32) -> Bool {
        let bit: UInt32
        switch source {
        case 0...6: bit = 1 << source
        case 0xfe: bit = 1 << 30
        case 0xff: bit = 1 << 31
        default: return false
        }
        return supportedSources & bit != 0
    }
    var availableSources: [UInt32] { Self.knownSources.filter { supports($0) } }
}

struct TimeCardFrequencyState: Equatable, Sendable, Identifiable, Codable {
    var size: UInt32 = 32
    var counter: UInt32 = 0
    var integrationSeconds: UInt32 = 0
    var flags: UInt32 = 0
    var frequencyHz: UInt32 = 0
    var control: UInt32 = 0
    var status: UInt32 = 0
    var reserved: UInt32 = 0
    var id: UInt32 { counter }
    var isEnabled: Bool { flags & 2 != 0 }
    var hasError: Bool { flags & 8 != 0 }
    var hasOverrun: Bool { flags & 16 != 0 }
    var measurementHz: UInt32? {
        guard flags & 7 == 7, integrationSeconds > 0, !hasError, !hasOverrun else { return nil }
        return frequencyHz
    }
    var stateLabel: String {
        if flags & 1 == 0 { return "Unavailable" }
        if !isEnabled { return "Disabled" }
        if hasError { return "Measurement error" }
        if hasOverrun { return "Counter overrun" }
        if integrationSeconds == 0 { return "Invalid integration interval" }
        return measurementHz == nil ? "Waiting for measurement" : "Valid measurement"
    }
}

private struct TimeCardClockSourceRequestRaw {
    var size: UInt32 = 16
    var source: UInt32
    var expectedSource: UInt32
    var reserved: UInt32 = 0
}

private struct TimeCardFrequencyRequestRaw {
    var size: UInt32 = 16
    var counter: UInt32
    var integrationSeconds: UInt32 = 0
    var expectedControl: UInt32 = 0
}

enum TimeCardSMADirection: UInt32, CaseIterable, Identifiable, Sendable {
    case input = 0
    case output = 1
    case disabled = 2

    var id: UInt32 { rawValue }

    var label: String {
        switch self {
        case .input: "Input"
        case .output: "Output"
        case .disabled: "Disabled"
        }
    }
}

struct TimeCardSMAFunction: Identifiable, Hashable, Sendable {
    let id: UInt32
    let label: String
}

struct TimeCardSMARoute: Identifiable, Equatable, Sendable {
    let connector: UInt32
    let direction: TimeCardSMADirection
    let function: UInt32
    let flags: UInt32
    let inputMap: UInt32
    let outputMap: UInt32

    var id: UInt32 { connector }

    var isPresent: Bool { (flags & (1 << 0)) != 0 }
    var isFixedDirection: Bool { (flags & (1 << 1)) != 0 }
    var isDisabled: Bool { (flags & (1 << 2)) != 0 }
    var isFixedFunction: Bool { (flags & (1 << 3)) != 0 }

    var functionName: String {
        TimeCardSMACatalog.name(for: function, direction: direction)
    }
}

enum TimeCardLEDKind: UInt32, CaseIterable, Identifiable, Sendable {
    case gnss1 = 0
    case gnss2 = 1
    case sma1 = 2
    case sma2 = 3
    case sma3 = 4
    case sma4 = 5

    var id: UInt32 { rawValue }

    var label: String {
        switch self {
        case .gnss1: "GNSS 1"
        case .gnss2: "GNSS 2"
        case .sma1: "SMA 1"
        case .sma2: "SMA 2"
        case .sma3: "SMA 3"
        case .sma4: "SMA 4"
        }
    }
}

struct TimeCardLEDState: Identifiable, Equatable, Sendable {
    let led: TimeCardLEDKind
    let flags: UInt32
    let red: UInt32
    let green: UInt32
    let blue: UInt32
    let globalCurrent: UInt32
    let muxChannelMask: UInt32
    let controllerStatus: UInt32
    let interruptStatus: UInt32
    let openOutputMask: UInt32
    let shortOutputMask: UInt32

    var id: UInt32 { led.rawValue }
    var isPresent: Bool { (flags & (1 << 0)) != 0 }
    var isEnabled: Bool { (flags & (1 << 1)) != 0 }
    var faultStateValid: Bool { (flags & (1 << 2)) != 0 }

    var rgbText: String {
        "\(red)/\(green)/\(blue)"
    }

    var color: ColorComponents {
        ColorComponents(red: red, green: green, blue: blue)
    }
}

struct ColorComponents: Equatable, Sendable {
    let red: UInt32
    let green: UInt32
    let blue: UInt32
}

struct TimeCardSerialPort: Identifiable, Equatable, Sendable {
    let calloutDevice: String
    let dialinDevice: String?
    let ttyDevice: String?
    let baseName: String?
    let bsdType: String?

    var id: String {
        calloutDevice
    }

    var displayName: String {
        if let baseName, !baseName.isEmpty {
            return baseName
        }
        if let ttyDevice, !ttyDevice.isEmpty {
            return ttyDevice
        }
        return URL(fileURLWithPath: calloutDevice).lastPathComponent
    }
}

struct TimeCardSerialCapture: Equatable, Sendable {
    let portPath: String
    let baudRate: UInt32
    let capturedAt: Date
    let durationSeconds: Double
    let data: [UInt8]

    var byteCount: Int {
        data.count
    }

    var text: String {
        String(decoding: data, as: UTF8.self)
    }

    var lines: [String] {
        text.split(whereSeparator: \.isNewline).map(String.init)
    }
}

enum TimeCardUARTPort: UInt32, CaseIterable, Identifiable, Sendable {
    case gnss = 0
    case gnss2 = 1
    case mac = 2
    case nmea = 3

    var id: UInt32 { rawValue }

    var label: String {
        switch self {
        case .gnss: "GNSS"
        case .gnss2: "GNSS 2"
        case .mac: "MAC or atomic"
        case .nmea: "NMEA"
        }
    }

    var detail: String {
        switch self {
        case .gnss: "Primary receiver UART"
        case .gnss2: "Secondary receiver UART when fitted"
        case .mac: "MAC or atomic-clock UART"
        case .nmea: "NMEA output UART"
        }
    }

    var supportsReceiverPolls: Bool {
        self == .gnss || self == .gnss2
    }
}

struct TimeCardUARTObservation: Equatable, Sendable {
    let port: TimeCardUARTPort
    let timeoutMilliseconds: UInt32
    let flags: UInt32
    let lineStatus: UInt32

    var isPresent: Bool { (flags & (1 << 0)) != 0 }
    var hasActivity: Bool { (flags & (1 << 1)) != 0 }
    var lineStatusText: String {
        String(format: "0x%02x", lineStatus & 0xff)
    }
    var summary: String {
        "\(port.label): \(hasActivity ? "activity" : "idle"), LSR \(lineStatusText)"
    }
}

struct TimeCardUARTReadResult: Equatable, Sendable {
    let port: TimeCardUARTPort
    let timeoutMilliseconds: UInt32
    let lineStatus: UInt32
    let data: [UInt8]

    init(
        port: TimeCardUARTPort,
        timeoutMilliseconds: UInt32,
        lineStatus: UInt32,
        data: [UInt8]
    ) {
        self.port = port
        self.timeoutMilliseconds = timeoutMilliseconds
        self.lineStatus = lineStatus
        self.data = data
    }

    init(rawBytes: [UInt8]) {
        let rawPort = TimeCardUARTTransferRawLayout.readUInt32(
            rawBytes,
            at: TimeCardUARTTransferRawLayout.portOffset
        )
        let reportedLength = TimeCardUARTTransferRawLayout.readUInt32(
            rawBytes,
            at: TimeCardUARTTransferRawLayout.lengthOffset
        )
        let available = max(
            0,
            rawBytes.count - TimeCardUARTTransferRawLayout.dataOffset
        )
        let dataLength = min(Int(reportedLength), available, 256)
        let dataStart = TimeCardUARTTransferRawLayout.dataOffset
        let dataEnd = dataStart + dataLength
        self.init(
            port: TimeCardUARTPort(rawValue: rawPort) ?? .gnss,
            timeoutMilliseconds: TimeCardUARTTransferRawLayout.readUInt32(
                rawBytes,
                at: TimeCardUARTTransferRawLayout.timeoutOffset
            ),
            lineStatus: TimeCardUARTTransferRawLayout.readUInt32(
                rawBytes,
                at: TimeCardUARTTransferRawLayout.lineStatusOffset
            ),
            data: Array(rawBytes[dataStart..<dataEnd])
        )
    }

    var byteCount: Int {
        data.count
    }

    var lineStatusText: String {
        String(format: "0x%02x", lineStatus & 0xff)
    }

    var text: String {
        String(decoding: data, as: UTF8.self)
    }

    var dataHex: String {
        data.map { String(format: "%02x", $0) }.joined(separator: " ")
    }

    var hexDumpLines: [String] {
        guard !data.isEmpty else { return [] }
        return stride(from: 0, to: data.count, by: 16).map { offset in
            let chunk = data[offset..<min(offset + 16, data.count)]
            let hex = chunk
                .map { String(format: "%02x", $0) }
                .joined(separator: " ")
            return String(format: "%04x: %@", offset, hex)
        }
    }
}

struct TimeCardUARTCapture: Equatable, Sendable {
    let port: TimeCardUARTPort
    let baudRate: UInt32
    let capturedAt: Date
    let durationSeconds: Double
    let requestedDurationSeconds: Double
    let readCount: Int
    let emptyReadCount: Int
    let lastLineStatus: UInt32
    let stoppedByLimit: Bool
    let cancelled: Bool
    let data: [UInt8]

    var byteCount: Int {
        data.count
    }

    var lineStatusText: String {
        String(format: "0x%02x", lastLineStatus & 0xff)
    }

    var text: String {
        String(decoding: data, as: UTF8.self)
    }

    var dataHex: String {
        data.map { String(format: "%02x", $0) }.joined(separator: " ")
    }

    var stopReason: String {
        if cancelled { return "Stopped" }
        if stoppedByLimit { return "Byte limit" }
        return "Timed capture"
    }

    var hexDumpLines: [String] {
        guard !data.isEmpty else { return [] }
        return stride(from: 0, to: data.count, by: 16).map { offset in
            let chunk = data[offset..<min(offset + 16, data.count)]
            let hex = chunk
                .map { String(format: "%02x", $0) }
                .joined(separator: " ")
            let paddedHex = hex.padding(
                toLength: 47,
                withPad: " ",
                startingAt: 0
            )
            let ascii = chunk.map { byte -> Character in
                byte >= 0x20 && byte <= 0x7e
                    ? Character(UnicodeScalar(byte))
                    : "."
            }
            return String(format: "%04x: %@  %@", offset, paddedHex, String(ascii))
        }
    }
}

struct TimeCardUARTWriteResult: Equatable, Sendable {
    let port: TimeCardUARTPort
    let timeoutMilliseconds: UInt32
    let lineStatus: UInt32
    let requestedByteCount: Int
    let byteCount: Int

    init(
        port: TimeCardUARTPort,
        timeoutMilliseconds: UInt32,
        lineStatus: UInt32,
        requestedByteCount: Int,
        byteCount: Int
    ) {
        self.port = port
        self.timeoutMilliseconds = timeoutMilliseconds
        self.lineStatus = lineStatus
        self.requestedByteCount = requestedByteCount
        self.byteCount = byteCount
    }

    init(rawBytes: [UInt8], requestedByteCount: Int) {
        let rawPort = TimeCardUARTTransferRawLayout.readUInt32(
            rawBytes,
            at: TimeCardUARTTransferRawLayout.portOffset
        )
        self.init(
            port: TimeCardUARTPort(rawValue: rawPort) ?? .gnss,
            timeoutMilliseconds: TimeCardUARTTransferRawLayout.readUInt32(
                rawBytes,
                at: TimeCardUARTTransferRawLayout.timeoutOffset
            ),
            lineStatus: TimeCardUARTTransferRawLayout.readUInt32(
                rawBytes,
                at: TimeCardUARTTransferRawLayout.lineStatusOffset
            ),
            requestedByteCount: requestedByteCount,
            byteCount: Int(
                TimeCardUARTTransferRawLayout.readUInt32(
                    rawBytes,
                    at: TimeCardUARTTransferRawLayout.lengthOffset
                )
            )
        )
    }

    var complete: Bool {
        byteCount == requestedByteCount
    }

    var lineStatusText: String {
        String(format: "0x%02x", lineStatus & 0xff)
    }

    var summary: String {
        "\(port.label): wrote \(byteCount)/\(requestedByteCount) byte(s), LSR \(lineStatusText)"
    }
}

struct TimeCardI2CStatusSnapshot: Equatable, Sendable {
    let flags: UInt32
    let offset: UInt64
    let control: UInt32
    let status: UInt32
    let interruptStatus: UInt32
    let interruptEnable: UInt32
    let txFifoOccupancy: UInt32
    let rxFifoOccupancy: UInt32
    let knownDeviceMask: UInt32

    var isPresent: Bool { (flags & (1 << 0)) != 0 }
    var isEnabled: Bool { (flags & (1 << 1)) != 0 }
    var isBusBusy: Bool { (flags & (1 << 2)) != 0 }
    var isReceiveEmpty: Bool { (flags & (1 << 3)) != 0 }
    var isTransmitEmpty: Bool { (flags & (1 << 4)) != 0 }

    var knownDeviceNames: [String] {
        var names: [String] = []
        if (knownDeviceMask & (1 << 0)) != 0 { names.append("Mux") }
        if (knownDeviceMask & (1 << 1)) != 0 { names.append("LED") }
        return names
    }
}

struct TimeCardI2CMuxSnapshot: Equatable, Sendable {
    let isPresent: Bool
    let channelMask: UInt32
    let controllerStatus: UInt32
    let interruptStatus: UInt32
}

struct TimeCardI2CProbeResult: Identifiable, Equatable, Sendable {
    let address: UInt32
    let isPresent: Bool
    let controllerStatus: UInt32
    let interruptStatus: UInt32

    var id: UInt32 { address }

    var addressText: String {
        String(format: "0x%02x", address & 0xff)
    }
}

struct TimeCardI2CTransferSnapshot: Equatable, Sendable {
    let address: UInt32
    let length: UInt32
    let controllerStatus: UInt32
    let interruptStatus: UInt32
    let data: [UInt8]

    init(
        address: UInt32,
        length: UInt32,
        controllerStatus: UInt32,
        interruptStatus: UInt32,
        data: [UInt8]
    ) {
        self.address = address
        self.length = length
        self.controllerStatus = controllerStatus
        self.interruptStatus = interruptStatus
        self.data = data
    }

    init(rawBytes: [UInt8]) {
        let reportedLength = TimeCardI2CTransferRawLayout.readUInt32(
            rawBytes,
            at: TimeCardI2CTransferRawLayout.lengthOffset
        )
        let available = max(
            0,
            rawBytes.count - TimeCardI2CTransferRawLayout.dataOffset
        )
        let dataLength = min(Int(reportedLength), available, 256)
        let dataStart = TimeCardI2CTransferRawLayout.dataOffset
        let dataEnd = dataStart + dataLength
        self.init(
            address: TimeCardI2CTransferRawLayout.readUInt32(
                rawBytes,
                at: TimeCardI2CTransferRawLayout.addressOffset
            ),
            length: reportedLength,
            controllerStatus: TimeCardI2CTransferRawLayout.readUInt32(
                rawBytes,
                at: TimeCardI2CTransferRawLayout.controllerStatusOffset
            ),
            interruptStatus: TimeCardI2CTransferRawLayout.readUInt32(
                rawBytes,
                at: TimeCardI2CTransferRawLayout.interruptStatusOffset
            ),
            data: Array(rawBytes[dataStart..<dataEnd])
        )
    }

    var addressText: String {
        String(format: "0x%02x", address & 0xff)
    }

    var dataHex: String {
        data.map { String(format: "%02x", $0) }.joined(separator: " ")
    }

    var asciiText: String {
        String(
            data.map { byte in
                if byte >= 0x20 && byte <= 0x7e {
                    return Character(UnicodeScalar(UInt32(byte))!)
                }
                return "."
            }
        )
    }

    var hexDumpLines: [String] {
        guard !data.isEmpty else { return [] }
        return stride(from: 0, to: data.count, by: 16).map { offset in
            let chunk = data[offset..<min(offset + 16, data.count)]
            let hex = chunk
                .map { String(format: "%02x", $0) }
                .joined(separator: " ")
            return String(format: "%04x: %@", offset, hex)
        }
    }
}

enum TimeCardSMACatalog {
    static let inputFunctions: [TimeCardSMAFunction] = [
        .init(id: 0x0000, label: "10 MHz"),
        .init(id: 0x0001, label: "PPS 1"),
        .init(id: 0x0002, label: "PPS 2"),
        .init(id: 0x0004, label: "Timestamp 1"),
        .init(id: 0x0008, label: "Timestamp 2"),
        .init(id: 0x0010, label: "IRIG-B"),
        .init(id: 0x0020, label: "DCF77"),
        .init(id: 0x0040, label: "Timestamp 3"),
        .init(id: 0x0080, label: "Timestamp 4"),
        .init(id: 0x0100, label: "Frequency 1"),
        .init(id: 0x0200, label: "Frequency 2"),
        .init(id: 0x0400, label: "Frequency 3"),
        .init(id: 0x0800, label: "Frequency 4"),
    ]

    static let outputFunctions: [TimeCardSMAFunction] = [
        .init(id: 0x0000, label: "10 MHz"),
        .init(id: 0x0001, label: "PHC"),
        .init(id: 0x0002, label: "MAC"),
        .init(id: 0x0004, label: "GNSS 1"),
        .init(id: 0x0008, label: "GNSS 2"),
        .init(id: 0x0010, label: "IRIG-B"),
        .init(id: 0x0020, label: "DCF77"),
        .init(id: 0x0040, label: "Generator 1"),
        .init(id: 0x0080, label: "Generator 2"),
        .init(id: 0x0100, label: "Generator 3"),
        .init(id: 0x0200, label: "Generator 4"),
        .init(id: 0x2000, label: "Ground"),
        .init(id: 0x4000, label: "VCC"),
    ]

    static func functions(for direction: TimeCardSMADirection)
        -> [TimeCardSMAFunction] {
        switch direction {
        case .input: inputFunctions
        case .output: outputFunctions
        case .disabled: []
        }
    }

    static func name(for value: UInt32, direction: TimeCardSMADirection)
        -> String {
        functions(for: direction).first(where: { $0.id == value })?.label
            ?? String(format: "0x%04x", value)
    }
}

enum TimeCardSensorKind: UInt32, Sendable {
    case unknown = 0
    case lm75b = 1
    case sht3x = 2
    case icp10100 = 3
    case bme280 = 4
    case ina219 = 5
    case bno08x = 6
    case bno055 = 7

    var label: String {
        switch self {
        case .unknown: "Unknown"
        case .lm75b: "LM75B"
        case .sht3x: "SHT3x"
        case .icp10100: "ICP-10100"
        case .bme280: "BME280/BMP280"
        case .ina219: "INA219"
        case .bno08x: "BNO08x"
        case .bno055: "BNO055"
        }
    }
}

struct TimeCardSensorReading: Identifiable, Equatable, Sendable {
    let kind: TimeCardSensorKind
    let flags: UInt32
    let muxChannelMask: UInt32
    let address: UInt32
    let temperatureMilliCelsius: Int32
    let humidityMilliPercent: UInt32
    let pressureRaw: UInt32
    let raw0: UInt32
    let raw1: UInt32
    let raw2: UInt32

    var id: String {
        "\(kind.rawValue)-\(muxChannelMask)-\(address)"
    }

    var isPresent: Bool { hasFlag(1 << 0) }
    var isValid: Bool { hasFlag(1 << 1) }
    var isConfigured: Bool { hasFlag(1 << 2) }
    var isConversionReady: Bool { hasFlag(1 << 3) }
    var isOverflowed: Bool { hasFlag(1 << 4) }
    var hasHumidity: Bool { hasFlag(1 << 6) }
    var hasTemperature: Bool { hasFlag(1 << 7) }
    var hasValidCRC: Bool { hasFlag(1 << 9) }
    var hasPressure: Bool { hasFlag(1 << 10) }
    var isCalibrated: Bool { hasFlag(1 << 11) }
    var isIMU: Bool { hasFlag(1 << 12) }

    var temperatureCelsius: Double? {
        hasTemperature ? Double(temperatureMilliCelsius) / 1000.0 : nil
    }

    var humidityPercent: Double? {
        hasHumidity ? Double(humidityMilliPercent) / 1000.0 : nil
    }

    var productID: UInt32? {
        kind == .icp10100 ? raw2 : nil
    }

    func compensatedPressurePascals(calibration: [Int32]) -> Double? {
        guard kind == .icp10100, isValid, isCalibrated,
              hasPressure, calibration.count >= 4 else {
            return nil
        }

        let t = Double(raw1) - 32768.0
        let quadratic = t * t / 16777216.0
        let s1 = 3.5 * 1048576.0 + Double(calibration[0]) * quadratic
        let s2 = 2048.0 * Double(calibration[3]) +
            Double(calibration[1]) * quadratic
        let s3 = 11.5 * 1048576.0 + Double(calibration[2]) * quadratic
        let denominator = s3 * (45000.0 - 80000.0) +
            s1 * (80000.0 - 105000.0) + s2 * (105000.0 - 45000.0)
        guard abs(denominator) >= 0.000001,
              abs(s1 - s2) >= 0.000001 else {
            return nil
        }

        let c = (
            s1 * s2 * (45000.0 - 80000.0) +
            s2 * s3 * (80000.0 - 105000.0) +
            s3 * s1 * (105000.0 - 45000.0)
        ) / denominator
        let a = (
            45000.0 * s1 - 80000.0 * s2 - 35000.0 * c
        ) / (s1 - s2)
        let b = (45000.0 - a) * (s1 + c)
        let pressure = a + b / (c + Double(pressureRaw))
        return pressure.isFinite && pressure >= 10000.0 && pressure <= 130000.0
            ? pressure : nil
    }

    private func hasFlag(_ flag: UInt32) -> Bool {
        (flags & flag) != 0
    }
}

struct TimeCardSensorSnapshot: Equatable, Sendable {
    let flags: UInt32
    let boardProfile: UInt32
    let capabilities: UInt32
    let muxChannelMask: UInt32
    let restoredMuxChannelMask: UInt32
    let controllerStatus: UInt32
    let interruptStatus: UInt32
    let icp10100Otp: [Int32]
    let readings: [TimeCardSensorReading]

    var isPresent: Bool { hasFlag(1 << 0) }
    var isValid: Bool { hasFlag(1 << 1) }
    var muxWasRestored: Bool { muxChannelMask == restoredMuxChannelMask }
    var validReadings: [TimeCardSensorReading] {
        readings.filter(\.isValid)
    }
    var boardTemperatures: [TimeCardSensorReading] {
        readings.filter { $0.kind == .lm75b }
    }
    var humidityReading: TimeCardSensorReading? {
        readings.first { $0.kind == .sht3x }
    }
    var pressureReading: TimeCardSensorReading? {
        readings.first { $0.kind == .icp10100 }
    }
    var pressurePascals: Double? {
        pressureReading?.compensatedPressurePascals(calibration: icp10100Otp)
    }
    var dewPointCelsius: Double? {
        guard let reading = humidityReading,
              let temperature = reading.temperatureCelsius,
              let humidity = reading.humidityPercent,
              humidity > 0.0 else {
            return nil
        }
        let gamma = log(humidity / 100.0) +
            17.62 * temperature / (243.12 + temperature)
        return 243.12 * gamma / (17.62 - gamma)
    }

    var capabilityNames: [String] {
        var names: [String] = []
        if (capabilities & (1 << 0)) != 0 { names.append("BME280/BMP280") }
        if (capabilities & (1 << 1)) != 0 { names.append("INA219") }
        if (capabilities & (1 << 2)) != 0 { names.append("BNO055") }
        if (capabilities & (1 << 3)) != 0 { names.append("BNO08x") }
        if (capabilities & (1 << 4)) != 0 { names.append("LM75B") }
        if (capabilities & (1 << 5)) != 0 { names.append("SHT3x") }
        if (capabilities & (1 << 6)) != 0 { names.append("ICP-10100") }
        return names
    }

    private func hasFlag(_ flag: UInt32) -> Bool {
        (flags & flag) != 0
    }
}

enum TimeCardClientError: Error, Equatable, LocalizedError, Sendable {
    case matchingDictionary
    case discoveryFailed(Int32)
    case serviceNotFound
    case serviceDisappeared
    case openFailed(Int32)
    case methodFailed(selector: UInt32, result: Int32)
    case unexpectedOutput(selector: UInt32, expected: Int, actual: Int)
    case invalidClientLayout
    case incompatibleABI(UInt32)
    case invalidTimestamp
    case invalidI2CRequest(String)
    case invalidUARTRequest(String)
    case invalidTimingRequest(String)
    case serialUnsupportedBaud(UInt32)
    case serialOpenFailed(path: String, errnoCode: Int32)
    case serialConfigureFailed(path: String, operation: String, errnoCode: Int32)

    var errorDescription: String? {
        switch self {
        case .matchingDictionary:
            "Unable to create an IOKit service match."
        case .discoveryFailed(let result):
            "Time Card discovery failed (\(Self.hex(result)))."
        case .serviceNotFound:
            "No active Time Card DriverKit service was found."
        case .serviceDisappeared:
            "The selected Time Card disconnected before it could be opened."
        case .openFailed(let result):
            "The Time Card user client could not be opened (\(Self.hex(result)))."
        case .methodFailed(let selector, let result) where (selector == 19 || selector == 21) && result == kIOReturnBusy:
            "Timing state changed since it was read. Nothing was written. Refresh and try again."
        case .methodFailed(let selector, let result) where (selector == 19 || selector == 21) && result == kIOReturnIOError:
            "Timing readback failed. The previous setting was restored and verified."
        case .methodFailed(let selector, let result) where (selector == 19 || selector == 21) && result == kIOReturnError:
            "Timing readback and rollback verification failed. Check the card state before further changes."
        case .methodFailed(let selector, let result):
            "Driver method \(selector) failed (\(Self.hex(result)))."
        case .unexpectedOutput(let selector, let expected, let actual):
            "Driver method \(selector) returned \(actual) bytes; expected \(expected)."
        case .invalidClientLayout:
            "The Control Center ABI structure layout is invalid."
        case .invalidTimingRequest(let reason):
            "Timing control: \(reason)"
        case .incompatibleABI(let version):
            "Driver ABI \(version) is not supported by this Control Center."
        case .invalidTimestamp:
            "The driver returned an invalid cross timestamp."
        case .invalidI2CRequest(let reason):
            reason
        case .invalidUARTRequest(let reason):
            reason
        case .serialUnsupportedBaud(let baud):
            "Unsupported serial baud rate \(baud)."
        case .serialOpenFailed(let path, let errnoCode):
            "Could not open \(path) for serial preview (\(Self.posixMessage(errnoCode)))."
        case .serialConfigureFailed(let path, let operation, let errnoCode):
            "Could not \(operation) \(path) (\(Self.posixMessage(errnoCode)))."
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .serviceNotFound:
            "Install or update the driver, approve it in System Settings, and restart if requested."
        case .openFailed(let result)
            where result == kIOReturnNotPrivileged
                || result == kIOReturnNotPermitted:
            "The app provisioning profile must include com.apple.developer.driverkit.userclient-access for the Time Card driver."
        case .openFailed:
            "Verify that the selected driver service is active, then retry."
        case .serviceDisappeared:
            "Reconnect the device or wait for the driver to finish restarting."
        case .incompatibleABI:
            "Install the matching Control Center and driver versions together."
        default:
            nil
        }
    }

    private static func hex(_ value: Int32) -> String {
        String(format: "0x%08x", UInt32(bitPattern: value))
    }

    private static func posixMessage(_ errnoCode: Int32) -> String {
        String(cString: strerror(errnoCode))
    }
}

enum TimeCardClient {
    private static let uartSessionLock = NSRecursiveLock()
    static func withUARTSession<T>(_ operation: () throws -> T) rethrows -> T {
        uartSessionLock.lock()
        defer { uartSessionLock.unlock() }
        return try operation()
    }
    private static let serviceClass = "IOUserService"
    private static let userClassValue = "TimeCardDriver"
    private static let minimumSupportedABIVersion: UInt32 = 7
    private static let supportedABIVersion: UInt32 = 11

    static var localABILayoutIsValid: Bool {
        MemoryLayout<TimeCardClockControlState>.size == 32
            && MemoryLayout<TimeCardClockControlState>.offset(of: \.reserved) == 28
            && MemoryLayout<TimeCardFrequencyState>.size == 32
            && MemoryLayout<TimeCardFrequencyState>.offset(of: \.reserved) == 28
            && MemoryLayout<TimeCardClockSourceRequestRaw>.size == 16
            && MemoryLayout<TimeCardFrequencyRequestRaw>.size == 16
            && MemoryLayout<TimeCardTimeRaw>.size == 16
            && MemoryLayout<TimeCardTimeRaw>.offset(of: \.seconds) == 0
            && MemoryLayout<TimeCardTimeRaw>.offset(of: \.nanoseconds) == 8
            && MemoryLayout<TimeCardTimeRaw>.offset(of: \.reserved) == 12
            && MemoryLayout<TimeCardCrossTimestampRaw>.size == 32
            && MemoryLayout<TimeCardCrossTimestampRaw>.offset(of: \.cardTime) == 0
            && MemoryLayout<TimeCardCrossTimestampRaw>.offset(
                of: \.systemTimeBeforeNanoseconds
            ) == 16
            && MemoryLayout<TimeCardCrossTimestampRaw>.offset(
                of: \.systemTimeAfterNanoseconds
            ) == 24
            && MemoryLayout<TimeCardInfoRaw>.size == 112
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.abiVersion) == 0
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.driverVersion) == 4
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.vendorID) == 8
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.deviceID) == 10
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.layout) == 12
            && MemoryLayout<TimeCardInfoRaw>.offset(
                of: \.advertisedMSIXVectors
            ) == 16
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.barSize) == 24
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.clockOffset) == 32
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.todOffset) == 40
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.clockVersion) == 48
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.clockStatus) == 52
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.clockSelect) == 56
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.todVersion) == 60
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.todStatus) == 64
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.utcStatus) == 68
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.leap) == 72
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.gnssStatus) == 76
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.satellites) == 80
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.boardProfile) == 84
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.capabilities) == 88
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.validFields) == 96
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.pciRevision) == 104
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.reserved) == 108
            && MemoryLayout<TimeCardSMARaw>.size == 32
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.connector) == 4
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.direction) == 8
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.function) == 12
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.flags) == 16
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.inputMap) == 20
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.outputMap) == 24
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.reserved) == 28
            && MemoryLayout<TimeCardLEDRaw>.size == 48
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.led) == 4
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.flags) == 8
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.red) == 12
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.green) == 16
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.blue) == 20
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.globalCurrent) == 24
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.muxChannelMask) == 28
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.controllerStatus) == 32
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.interruptStatus) == 36
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.openOutputMask) == 40
            && MemoryLayout<TimeCardLEDRaw>.offset(of: \.shortOutputMask) == 44
            && MemoryLayout<TimeCardI2CStatusRaw>.size == 48
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.flags) == 4
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.offset) == 8
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.control) == 16
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.status) == 20
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.interruptStatus) == 24
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.interruptEnable) == 28
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.txFifoOccupancy) == 32
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.rxFifoOccupancy) == 36
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.knownDeviceMask) == 40
            && MemoryLayout<TimeCardI2CStatusRaw>.offset(of: \.reserved) == 44
            && MemoryLayout<TimeCardI2CProbeRaw>.size == 32
            && MemoryLayout<TimeCardI2CProbeRaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardI2CProbeRaw>.offset(of: \.address) == 4
            && MemoryLayout<TimeCardI2CProbeRaw>.offset(of: \.present) == 8
            && MemoryLayout<TimeCardI2CProbeRaw>.offset(of: \.controllerStatus) == 12
            && MemoryLayout<TimeCardI2CProbeRaw>.offset(of: \.interruptStatus) == 16
            && MemoryLayout<TimeCardI2CProbeRaw>.offset(of: \.reserved0) == 20
            && MemoryLayout<TimeCardI2CReadRequestRaw>.size == 32
            && MemoryLayout<TimeCardI2CReadRequestRaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardI2CReadRequestRaw>.offset(of: \.address) == 4
            && MemoryLayout<TimeCardI2CReadRequestRaw>.offset(of: \.subaddressLength) == 8
            && MemoryLayout<TimeCardI2CReadRequestRaw>.offset(of: \.subaddress) == 12
            && MemoryLayout<TimeCardI2CReadRequestRaw>.offset(of: \.length) == 16
            && MemoryLayout<TimeCardI2CReadRequestRaw>.offset(of: \.reserved0) == 20
            && MemoryLayout<TimeCardI2CMuxRaw>.size == 32
            && MemoryLayout<TimeCardI2CMuxRaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardI2CMuxRaw>.offset(of: \.present) == 4
            && MemoryLayout<TimeCardI2CMuxRaw>.offset(of: \.channelMask) == 8
            && MemoryLayout<TimeCardI2CMuxRaw>.offset(of: \.controllerStatus) == 12
            && MemoryLayout<TimeCardI2CMuxRaw>.offset(of: \.interruptStatus) == 16
            && MemoryLayout<TimeCardI2CMuxRaw>.offset(of: \.reserved0) == 20
            && MemoryLayout<TimeCardUARTConfigRaw>.size == 8
            && MemoryLayout<TimeCardUARTConfigRaw>.offset(of: \.port) == 0
            && MemoryLayout<TimeCardUARTConfigRaw>.offset(of: \.baud) == 4
            && MemoryLayout<TimeCardUARTReadRequestRaw>.size == 16
            && MemoryLayout<TimeCardUARTReadRequestRaw>.offset(of: \.port) == 0
            && MemoryLayout<TimeCardUARTReadRequestRaw>.offset(of: \.maximumBytes) == 4
            && MemoryLayout<TimeCardUARTReadRequestRaw>.offset(of: \.timeoutMilliseconds) == 8
            && MemoryLayout<TimeCardUARTReadRequestRaw>.offset(of: \.reserved) == 12
            && TimeCardUARTTransferRawLayout.size == 272
            && TimeCardUARTTransferRawLayout.dataOffset == 16
            && MemoryLayout<TimeCardUARTObserveRaw>.size == 32
            && MemoryLayout<TimeCardUARTObserveRaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardUARTObserveRaw>.offset(of: \.port) == 4
            && MemoryLayout<TimeCardUARTObserveRaw>.offset(of: \.timeoutMilliseconds) == 8
            && MemoryLayout<TimeCardUARTObserveRaw>.offset(of: \.flags) == 12
            && MemoryLayout<TimeCardUARTObserveRaw>.offset(of: \.lineStatus) == 16
            && MemoryLayout<TimeCardUARTObserveRaw>.offset(of: \.reserved0) == 20
            && MemoryLayout<TimeCardSensorReadingRaw>.size == 48
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.type) == 4
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.flags) == 8
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.muxChannelMask) == 12
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.address) == 16
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.temperatureMilliCelsius) == 20
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.humidityMilliPercent) == 24
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.pressureRaw) == 28
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.raw0) == 32
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.raw1) == 36
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.raw2) == 40
            && MemoryLayout<TimeCardSensorReadingRaw>.offset(of: \.reserved) == 44
            && MemoryLayout<TimeCardSensorTelemetryRaw>.size == 832
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.flags) == 4
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.boardProfile) == 8
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.capabilities) == 12
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.muxChannelMask) == 16
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.restoredMuxChannelMask) == 20
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.controllerStatus) == 24
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.interruptStatus) == 28
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.readingCount) == 32
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.icp10100Otp0) == 36
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.reserved0) == 52
            && MemoryLayout<TimeCardSensorTelemetryRaw>.offset(of: \.reading0) == 64
    }

    static func discoverServices() throws -> [TimeCardServiceDescriptor] {
        let userClassKey = "IOUserClass" as CFString
        guard let matching = IOServiceMatching(serviceClass) else {
            throw TimeCardClientError.matchingDictionary
        }

        var iterator: io_iterator_t = 0
        let result = IOServiceGetMatchingServices(
            kIOMainPortDefault, matching, &iterator
        )
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.discoveryFailed(result)
        }
        defer { IOObjectRelease(iterator) }

        var services: [TimeCardServiceDescriptor] = []
        while true {
            let service = IOIteratorNext(iterator)
            guard service != IO_OBJECT_NULL else { break }
            defer { IOObjectRelease(service) }

            guard let property = IORegistryEntryCreateCFProperty(
                service, userClassKey, kCFAllocatorDefault, 0
            )?.takeRetainedValue() as? String,
                property == userClassValue else {
                continue
            }

            var registryID: UInt64 = 0
            guard IORegistryEntryGetRegistryEntryID(service, &registryID)
                == KERN_SUCCESS else {
                continue
            }
            services.append(TimeCardServiceDescriptor(id: registryID))
        }

        return services.sorted { $0.id < $1.id }
    }

    static func listSerialPorts() -> [TimeCardSerialPort] {
        let matching = IOServiceMatching(
            kIOSerialBSDServiceValue
        ) as NSMutableDictionary
        matching[kIOSerialBSDTypeKey] = kIOSerialBSDAllTypes

        var iterator: io_iterator_t = 0
        let result = IOServiceGetMatchingServices(
            kIOMainPortDefault, matching, &iterator
        )
        guard result == KERN_SUCCESS else {
            return []
        }
        defer { IOObjectRelease(iterator) }

        var ports: [TimeCardSerialPort] = []
        while true {
            let service = IOIteratorNext(iterator)
            guard service != IO_OBJECT_NULL else { break }
            defer { IOObjectRelease(service) }

            guard let calloutDevice = serialStringProperty(
                service,
                key: kIOCalloutDeviceKey
            ) else {
                continue
            }
            ports.append(
                TimeCardSerialPort(
                    calloutDevice: calloutDevice,
                    dialinDevice: serialStringProperty(
                        service,
                        key: kIODialinDeviceKey
                    ),
                    ttyDevice: serialStringProperty(
                        service,
                        key: kIOTTYDeviceKey
                    ),
                    baseName: serialStringProperty(
                        service,
                        key: kIOTTYBaseNameKey
                    ),
                    bsdType: serialStringProperty(
                        service,
                        key: kIOSerialBSDTypeKey
                    )
                )
            )
        }

        return ports.sorted {
            $0.displayName.localizedStandardCompare($1.displayName)
                == .orderedAscending
        }
    }

    static func captureSerialPreview(
        portPath: String,
        baudRate: UInt32,
        durationSeconds: Double = 1.5,
        maxBytes: Int = 8192
    ) throws -> TimeCardSerialCapture {
        guard let speed = serialSpeed(for: baudRate) else {
            throw TimeCardClientError.serialUnsupportedBaud(baudRate)
        }

        let boundedDuration = min(max(durationSeconds, 0.1), 5.0)
        let boundedMaxBytes = min(max(maxBytes, 1), 65_536)
        let fileDescriptor = Darwin.open(
            portPath,
            O_RDONLY | O_NOCTTY | O_NONBLOCK
        )
        guard fileDescriptor >= 0 else {
            throw TimeCardClientError.serialOpenFailed(
                path: portPath,
                errnoCode: errno
            )
        }
        defer { Darwin.close(fileDescriptor) }

        var originalSettings = termios()
        guard tcgetattr(fileDescriptor, &originalSettings) == 0 else {
            throw TimeCardClientError.serialConfigureFailed(
                path: portPath,
                operation: "read settings for",
                errnoCode: errno
            )
        }

        var settings = originalSettings
        var settingsToRestore = originalSettings
        defer { _ = tcsetattr(fileDescriptor, TCSANOW, &settingsToRestore) }

        cfmakeraw(&settings)
        settings.c_cflag |= tcflag_t(CLOCAL | CREAD)
        settings.c_cflag &= ~tcflag_t(CSIZE | PARENB | CSTOPB)
        settings.c_cflag |= tcflag_t(CS8)
        withUnsafeMutableBytes(of: &settings.c_cc) { controlCharacters in
            controlCharacters[Int(VMIN)] = 0
            controlCharacters[Int(VTIME)] = 1
        }
        guard cfsetispeed(&settings, speed) == 0,
              cfsetospeed(&settings, speed) == 0 else {
            throw TimeCardClientError.serialConfigureFailed(
                path: portPath,
                operation: "set baud rate for",
                errnoCode: errno
            )
        }
        guard tcsetattr(fileDescriptor, TCSANOW, &settings) == 0 else {
            throw TimeCardClientError.serialConfigureFailed(
                path: portPath,
                operation: "apply settings to",
                errnoCode: errno
            )
        }
        tcflush(fileDescriptor, TCIOFLUSH)

        let capturedAt = Date()
        let deadline = capturedAt.addingTimeInterval(boundedDuration)
        var captured: [UInt8] = []
        var buffer = [UInt8](repeating: 0, count: 512)
        while Date() < deadline && captured.count < boundedMaxBytes {
            let requested = min(buffer.count, boundedMaxBytes - captured.count)
            let bytesRead = buffer.withUnsafeMutableBytes {
                Darwin.read(fileDescriptor, $0.baseAddress, requested)
            }
            if bytesRead > 0 {
                captured.append(contentsOf: buffer.prefix(bytesRead))
            } else if bytesRead == 0 ||
                errno == EAGAIN || errno == EWOULDBLOCK {
                usleep(20_000)
            } else {
                throw TimeCardClientError.serialConfigureFailed(
                    path: portPath,
                    operation: "read preview from",
                    errnoCode: errno
                )
            }
        }

        return TimeCardSerialCapture(
            portPath: portPath,
            baudRate: baudRate,
            capturedAt: capturedAt,
            durationSeconds: boundedDuration,
            data: captured
        )
    }

    static func readSnapshot(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> TimeCardDeviceSnapshot {
        guard localABILayoutIsValid else {
            throw TimeCardClientError.invalidClientLayout
        }
        guard let matching = IORegistryEntryIDMatching(descriptor.id) else {
            throw TimeCardClientError.matchingDictionary
        }
        let service = IOServiceGetMatchingService(kIOMainPortDefault, matching)
        guard service != IO_OBJECT_NULL else {
            throw TimeCardClientError.serviceDisappeared
        }
        defer { IOObjectRelease(service) }

        var connection: io_connect_t = 0
        let openResult = IOServiceOpen(
            service, mach_task_self_, 0, &connection
        )
        guard openResult == KERN_SUCCESS else {
            throw TimeCardClientError.openFailed(openResult)
        }
        defer { IOServiceClose(connection) }

        var info = TimeCardInfoRaw()
        try callOutput(
            connection: connection, selector: 0, output: &info
        )
        guard info.abiVersion >= minimumSupportedABIVersion &&
              info.abiVersion <= supportedABIVersion else {
            throw TimeCardClientError.incompatibleABI(info.abiVersion)
        }

        var timestamp = TimeCardCrossTimestampRaw()
        try callOutput(
            connection: connection, selector: 3, output: &timestamp
        )
        guard timestamp.cardTime.nanoseconds < 1_000_000_000,
              timestamp.systemTimeAfterNanoseconds
                >= timestamp.systemTimeBeforeNanoseconds else {
            throw TimeCardClientError.invalidTimestamp
        }

        return TimeCardDeviceSnapshot(
            service: descriptor,
            abiVersion: info.abiVersion,
            driverVersion: info.driverVersion,
            vendorID: info.vendorID,
            deviceID: info.deviceID,
            pciRevision: info.pciRevision,
            boardProfile: info.boardProfile,
            layout: info.layout,
            advertisedMSIXVectors: info.advertisedMSIXVectors,
            barSize: info.barSize,
            clockOffset: info.clockOffset,
            todOffset: info.todOffset,
            capabilities: info.capabilities,
            validFields: info.validFields,
            clockVersion: info.clockVersion,
            clockStatus: info.clockStatus,
            clockSelect: info.clockSelect,
            todVersion: info.todVersion,
            todStatus: info.todStatus,
            utcStatus: info.utcStatus,
            leap: info.leap,
            gnssStatus: info.gnssStatus,
            satellites: info.satellites,
            cardSeconds: timestamp.cardTime.seconds,
            cardNanoseconds: timestamp.cardTime.nanoseconds,
            systemTimeBeforeNanoseconds:
                timestamp.systemTimeBeforeNanoseconds,
            systemTimeAfterNanoseconds:
                timestamp.systemTimeAfterNanoseconds
        )
    }

    static func setCardFromSystem(
        for descriptor: TimeCardServiceDescriptor
    ) throws {
        guard localABILayoutIsValid else {
            throw TimeCardClientError.invalidClientLayout
        }
        guard let matching = IORegistryEntryIDMatching(descriptor.id) else {
            throw TimeCardClientError.matchingDictionary
        }
        let service = IOServiceGetMatchingService(kIOMainPortDefault, matching)
        guard service != IO_OBJECT_NULL else {
            throw TimeCardClientError.serviceDisappeared
        }
        defer { IOObjectRelease(service) }

        var connection: io_connect_t = 0
        let openResult = IOServiceOpen(
            service, mach_task_self_, 0, &connection
        )
        guard openResult == KERN_SUCCESS else {
            throw TimeCardClientError.openFailed(openResult)
        }
        defer { IOServiceClose(connection) }

        let now = Date().timeIntervalSince1970
        let seconds = floor(now)
        let nanoseconds = (now - seconds) * 1_000_000_000
        var time = TimeCardTimeRaw(
            seconds: UInt64(seconds),
            nanoseconds: UInt32(nanoseconds.rounded()),
            reserved: 0
        )
        try callInput(connection: connection, selector: 2, input: &time)
    }

    static func querySMARoutes(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> [TimeCardSMARoute] {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        return try (1...4).map { connector in
            var raw = TimeCardSMARaw(
                size: UInt32(MemoryLayout<TimeCardSMARaw>.size),
                connector: UInt32(connector)
            )
            try callInOut(connection: connection, selector: 4, value: &raw)
            return raw.route
        }
    }

    static func setSMARoute(
        for descriptor: TimeCardServiceDescriptor,
        connector: UInt32,
        direction: TimeCardSMADirection,
        function: UInt32
    ) throws -> TimeCardSMARoute {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardSMARaw(
            size: UInt32(MemoryLayout<TimeCardSMARaw>.size),
            connector: connector,
            direction: direction.rawValue,
            function: function
        )
        try callInOut(connection: connection, selector: 5, value: &raw)
        return raw.route
    }

    static func queryI2CStatus(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> TimeCardI2CStatusSnapshot {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardI2CStatusRaw()
        raw.size = UInt32(MemoryLayout<TimeCardI2CStatusRaw>.size)
        try callOutput(connection: connection, selector: 8, output: &raw)
        return raw.snapshot
    }

    static func probeI2C(
        for descriptor: TimeCardServiceDescriptor,
        address: UInt32
    ) throws -> TimeCardI2CProbeResult {
        try validateI2CAddress(address)
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardI2CProbeRaw(
            size: UInt32(MemoryLayout<TimeCardI2CProbeRaw>.size),
            address: address
        )
        try callInOut(connection: connection, selector: 9, value: &raw)
        return raw.result
    }

    static func scanI2CBus(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> [TimeCardI2CProbeResult] {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        return try (0x08...0x77).map { address in
            var raw = TimeCardI2CProbeRaw(
                size: UInt32(MemoryLayout<TimeCardI2CProbeRaw>.size),
                address: UInt32(address)
            )
            try callInOut(connection: connection, selector: 9, value: &raw)
            return raw.result
        }
    }

    static func readI2C(
        for descriptor: TimeCardServiceDescriptor,
        address: UInt32,
        subaddress: UInt32,
        subaddressLength: UInt32,
        length: UInt32
    ) throws -> TimeCardI2CTransferSnapshot {
        try validateI2CAddress(address)
        guard subaddressLength <= 2 else {
            throw TimeCardClientError.invalidI2CRequest(
                "I2C subaddress length must be 0, 1, or 2 bytes."
            )
        }
        guard length > 0 && length <= 255 else {
            throw TimeCardClientError.invalidI2CRequest(
                "I2C read length must be between 1 and 255 bytes."
            )
        }
        if subaddressLength == 1 && subaddress > 0xff {
            throw TimeCardClientError.invalidI2CRequest(
                "One-byte I2C subaddresses must fit in 0x00 through 0xff."
            )
        }
        if subaddressLength == 2 && subaddress > 0xffff {
            throw TimeCardClientError.invalidI2CRequest(
                "Two-byte I2C subaddresses must fit in 0x0000 through 0xffff."
            )
        }

        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var request = TimeCardI2CReadRequestRaw(
            size: UInt32(MemoryLayout<TimeCardI2CReadRequestRaw>.size),
            address: address,
            subaddressLength: subaddressLength,
            subaddress: subaddress,
            length: length
        )
        var output = [UInt8](
            repeating: 0,
            count: TimeCardI2CTransferRawLayout.size
        )
        try callInOutBytes(
            connection: connection,
            selector: 10,
            input: &request,
            output: &output
        )
        return TimeCardI2CTransferSnapshot(rawBytes: output)
    }

    static func queryI2CMux(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> TimeCardI2CMuxSnapshot {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardI2CMuxRaw()
        raw.size = UInt32(MemoryLayout<TimeCardI2CMuxRaw>.size)
        try callOutput(connection: connection, selector: 11, output: &raw)
        return raw.snapshot
    }

    static func setI2CMux(
        for descriptor: TimeCardServiceDescriptor,
        channelMask: UInt32
    ) throws -> TimeCardI2CMuxSnapshot {
        guard (channelMask & ~0x0f) == 0 else {
            throw TimeCardClientError.invalidI2CRequest(
                "I2C mux channel mask must fit in 0x00 through 0x0f."
            )
        }

        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardI2CMuxRaw(
            size: UInt32(MemoryLayout<TimeCardI2CMuxRaw>.size),
            channelMask: channelMask
        )
        try callInOut(connection: connection, selector: 12, value: &raw)
        return raw.snapshot
    }

    static func setLEDState(
        for descriptor: TimeCardServiceDescriptor,
        led: TimeCardLEDKind,
        red: UInt32,
        green: UInt32,
        blue: UInt32,
        globalCurrent: UInt32
    ) throws -> TimeCardLEDState {
        guard red <= 255 && green <= 255 && blue <= 255 else {
            throw TimeCardClientError.invalidI2CRequest(
                "LED red, green, and blue values must be between 0 and 255."
            )
        }
        guard globalCurrent <= 255 else {
            throw TimeCardClientError.invalidI2CRequest(
                "LED current must be between 0 and 255."
            )
        }

        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardLEDRaw(
            size: UInt32(MemoryLayout<TimeCardLEDRaw>.size),
            led: led.rawValue,
            red: red,
            green: green,
            blue: blue,
            globalCurrent: globalCurrent
        )
        try callInOut(connection: connection, selector: 7, value: &raw)
        return raw.state
    }

    static func queryLEDStates(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> [TimeCardLEDState] {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        return try TimeCardLEDKind.allCases.map { led in
            var raw = TimeCardLEDRaw(
                size: UInt32(MemoryLayout<TimeCardLEDRaw>.size),
                led: led.rawValue
            )
            do {
                try callInOut(connection: connection, selector: 6, value: &raw)
            } catch TimeCardClientError.methodFailed(let selector, let result)
                where selector == 6 && result == kIOReturnUnsupported {
                raw.flags = 0
            }
            return raw.state
        }
    }

    static func queryIMU(for descriptor: TimeCardServiceDescriptor, mode: UInt32 = 0) throws -> MotionSample {
        guard mode <= 2 else { throw MotionError.invalid("Invalid motion operation.") }
        let snapshot = try readSnapshot(for: descriptor)
        guard snapshot.abiVersion >= 11, snapshot.capabilities & (1 << 11) != 0 else {
            throw MotionError.invalid("Live motion requires active driver build 26 / ABI v11 and a supported Meta/Celestica sensor route.")
        }
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }
        var input = [UInt8](repeating: 0, count: 16)
        TimeCardUARTTransferRawLayout.writeUInt32(16, into: &input, at: 0)
        TimeCardUARTTransferRawLayout.writeUInt32(mode, into: &input, at: 4)
        var output = [UInt8](repeating: 0, count: 144)
        try callInOutRawBytes(connection: connection, selector: 22, input: input, output: &output)
        return try MotionSample(bytes: output)
    }

    static func querySensors(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> TimeCardSensorSnapshot {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardSensorTelemetryRaw()
        raw.size = UInt32(MemoryLayout<TimeCardSensorTelemetryRaw>.size)
        try callOutput(connection: connection, selector: 13, output: &raw)
        return raw.snapshot
    }

    static func observeUART(
        for descriptor: TimeCardServiceDescriptor,
        port: TimeCardUARTPort,
        timeoutMilliseconds: UInt32
    ) throws -> TimeCardUARTObservation {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardUARTObserveRaw(
            size: UInt32(MemoryLayout<TimeCardUARTObserveRaw>.size),
            port: port.rawValue,
            timeoutMilliseconds: min(timeoutMilliseconds, 5_000)
        )
        try callInOut(connection: connection, selector: 14, value: &raw)
        return raw.observation
    }

    static func configureUART(
        for descriptor: TimeCardServiceDescriptor,
        port: TimeCardUARTPort,
        baudRate: UInt32
    ) throws {
        uartSessionLock.lock()
        defer { uartSessionLock.unlock() }
        try validateUARTBaudRate(baudRate)
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardUARTConfigRaw(
            port: port.rawValue,
            baud: baudRate
        )
        try callInput(connection: connection, selector: 15, input: &raw)
    }

    static func readUART(
        for descriptor: TimeCardServiceDescriptor,
        port: TimeCardUARTPort,
        maximumBytes: UInt32,
        timeoutMilliseconds: UInt32
    ) throws -> TimeCardUARTReadResult {
        uartSessionLock.lock()
        defer { uartSessionLock.unlock() }
        guard maximumBytes > 0 && maximumBytes <= 256 else {
            throw TimeCardClientError.invalidUARTRequest(
                "UART read length must be between 1 and 256 bytes."
            )
        }
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var request = TimeCardUARTReadRequestRaw(
            port: port.rawValue,
            maximumBytes: maximumBytes,
            timeoutMilliseconds: min(timeoutMilliseconds, 5_000)
        )
        var output = [UInt8](
            repeating: 0,
            count: TimeCardUARTTransferRawLayout.size
        )
        try callInOutBytes(
            connection: connection,
            selector: 16,
            input: &request,
            output: &output
        )
        return TimeCardUARTReadResult(rawBytes: output)
    }

    static func captureUART(
        for descriptor: TimeCardServiceDescriptor,
        port: TimeCardUARTPort,
        baudRate: UInt32,
        durationSeconds: Double = 5.0,
        maxBytes: Int = 65_536,
        readTimeoutMilliseconds: UInt32 = 250,
        progress: ((TimeCardUARTCapture) -> Void)? = nil
    ) throws -> TimeCardUARTCapture {
        uartSessionLock.lock()
        defer { uartSessionLock.unlock() }
        let boundedDuration = min(max(durationSeconds, 0.5), 60.0)
        let boundedMaxBytes = min(max(maxBytes, 1), 262_144)
        try configureUART(for: descriptor, port: port, baudRate: baudRate)

        let capturedAt = Date()
        let deadline = capturedAt.addingTimeInterval(boundedDuration)
        var captured: [UInt8] = []
        var readCount = 0
        var emptyReadCount = 0
        var lastLineStatus: UInt32 = 0

        func captureSnapshot(cancelled: Bool) -> TimeCardUARTCapture {
            TimeCardUARTCapture(
                port: port,
                baudRate: baudRate,
                capturedAt: capturedAt,
                durationSeconds: Date().timeIntervalSince(capturedAt),
                requestedDurationSeconds: boundedDuration,
                readCount: readCount,
                emptyReadCount: emptyReadCount,
                lastLineStatus: lastLineStatus,
                stoppedByLimit: captured.count >= boundedMaxBytes,
                cancelled: cancelled,
                data: captured
            )
        }

        while Date() < deadline && captured.count < boundedMaxBytes &&
            !Task.isCancelled {
            let remainingBytes = boundedMaxBytes - captured.count
            let maximumBytes = UInt32(min(256, remainingBytes))
            let remainingMilliseconds = max(
                1,
                Int(deadline.timeIntervalSinceNow * 1_000.0)
            )
            let timeout = UInt32(
                min(Int(max(readTimeoutMilliseconds, 1)), remainingMilliseconds)
            )
            let transfer = try readUART(
                for: descriptor,
                port: port,
                maximumBytes: maximumBytes,
                timeoutMilliseconds: timeout
            )
            readCount += 1
            lastLineStatus = transfer.lineStatus
            if transfer.data.isEmpty {
                emptyReadCount += 1
            } else {
                captured.append(contentsOf: transfer.data.prefix(remainingBytes))
            }
            progress?(captureSnapshot(cancelled: false))
        }

        return captureSnapshot(cancelled: Task.isCancelled)
    }

    static func writeUART(
        for descriptor: TimeCardServiceDescriptor,
        port: TimeCardUARTPort,
        bytes: [UInt8],
        timeoutMilliseconds: UInt32
    ) throws -> TimeCardUARTWriteResult {
        uartSessionLock.lock()
        defer { uartSessionLock.unlock() }
        guard !bytes.isEmpty && bytes.count <= 256 else {
            throw TimeCardClientError.invalidUARTRequest(
                "UART write length must be between 1 and 256 bytes."
            )
        }
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var input = [UInt8](
            repeating: 0,
            count: TimeCardUARTTransferRawLayout.size
        )
        TimeCardUARTTransferRawLayout.writeUInt32(
            port.rawValue,
            into: &input,
            at: TimeCardUARTTransferRawLayout.portOffset
        )
        TimeCardUARTTransferRawLayout.writeUInt32(
            UInt32(bytes.count),
            into: &input,
            at: TimeCardUARTTransferRawLayout.lengthOffset
        )
        TimeCardUARTTransferRawLayout.writeUInt32(
            min(timeoutMilliseconds, 5_000),
            into: &input,
            at: TimeCardUARTTransferRawLayout.timeoutOffset
        )
        let dataStart = TimeCardUARTTransferRawLayout.dataOffset
        let dataEnd = dataStart + bytes.count
        input.replaceSubrange(dataStart..<dataEnd, with: bytes)

        var output = [UInt8](
            repeating: 0,
            count: TimeCardUARTTransferRawLayout.size
        )
        try callInOutRawBytes(
            connection: connection,
            selector: 17,
            input: input,
            output: &output
        )
        return TimeCardUARTWriteResult(
            rawBytes: output,
            requestedByteCount: bytes.count
        )
    }

    private static func validateI2CAddress(_ address: UInt32) throws {
        guard address >= 0x08 && address <= 0x77 else {
            throw TimeCardClientError.invalidI2CRequest(
                "I2C address must be a 7-bit address from 0x08 through 0x77."
            )
        }
    }

    private static func validateUARTBaudRate(_ baudRate: UInt32) throws {
        guard baudRate > 0 && baudRate <= 3_000_000 else {
            throw TimeCardClientError.invalidUARTRequest(
                "UART baud rate must be between 1 and 3000000."
            )
        }
    }

    private static func serialStringProperty(
        _ service: io_object_t,
        key: String
    ) -> String? {
        IORegistryEntryCreateCFProperty(
            service,
            key as CFString,
            kCFAllocatorDefault,
            0
        )?.takeRetainedValue() as? String
    }

    private static func serialSpeed(for baudRate: UInt32) -> speed_t? {
        switch baudRate {
        case 1_200: speed_t(B1200)
        case 2_400: speed_t(B2400)
        case 4_800: speed_t(B4800)
        case 9_600: speed_t(B9600)
        case 19_200: speed_t(B19200)
        case 38_400: speed_t(B38400)
        case 57_600: speed_t(B57600)
        case 115_200: speed_t(B115200)
        case 230_400: speed_t(B230400)
        default: nil
        }
    }

    static func queryClockControl(for descriptor: TimeCardServiceDescriptor) throws -> TimeCardClockControlState {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }
        try requireTimingCapability(connection: connection, bit: 9)
        var output = TimeCardClockControlState()
        try callOutput(connection: connection, selector: 18, output: &output)
        guard output.size == 32, output.reserved == 0 else { throw TimeCardClientError.invalidClientLayout }
        return output
    }

    static func setClockSource(for descriptor: TimeCardServiceDescriptor, source: UInt32,
                               expectedSource: UInt32) throws -> TimeCardClockControlState {
        guard TimeCardClockControlState.knownSources.contains(source), expectedSource <= 255 else {
            throw TimeCardClientError.invalidTimingRequest("Unsupported clock source.")
        }
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }
        try requireTimingCapability(connection: connection, bit: 9)
        var input = TimeCardClockSourceRequestRaw(source: source, expectedSource: expectedSource)
        var output = TimeCardClockControlState()
        try callInputOutput(connection: connection, selector: 19, input: &input, output: &output)
        guard output.size == 32, output.reserved == 0, output.source == source else {
            throw TimeCardClientError.invalidTimingRequest("Clock-source readback did not match.")
        }
        return output
    }

    static func queryFrequencies(for descriptor: TimeCardServiceDescriptor) throws -> [TimeCardFrequencyState] {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }
        try requireTimingCapability(connection: connection, bit: 10)
        return try (1...4).map { counter in
            var input = TimeCardFrequencyRequestRaw(counter: UInt32(counter))
            var output = TimeCardFrequencyState()
            try callInputOutput(connection: connection, selector: 20, input: &input, output: &output)
            guard output.size == 32, output.counter == counter, output.reserved == 0 else {
                throw TimeCardClientError.invalidClientLayout
            }
            return output
        }
    }

    static func setFrequency(for descriptor: TimeCardServiceDescriptor, counter: UInt32,
                             seconds: UInt32, expectedControl: UInt32) throws -> TimeCardFrequencyState {
        guard (1...4).contains(counter), seconds <= 255, expectedControl & ~0xff01 == 0 else {
            throw TimeCardClientError.invalidTimingRequest("Counter must be 1...4 and interval 0...255 seconds.")
        }
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }
        try requireTimingCapability(connection: connection, bit: 10)
        var input = TimeCardFrequencyRequestRaw(counter: counter, integrationSeconds: seconds,
                                                expectedControl: expectedControl)
        var output = TimeCardFrequencyState()
        try callInputOutput(connection: connection, selector: 21, input: &input, output: &output)
        let desired: UInt32 = seconds == 0 ? 0 : seconds << 8 | 1
        guard output.size == 32, output.counter == counter, output.reserved == 0, output.control == desired else {
            throw TimeCardClientError.invalidTimingRequest("Counter readback did not match.")
        }
        return output
    }

    private static func requireTimingCapability(connection: io_connect_t, bit: UInt64) throws {
        guard localABILayoutIsValid else { throw TimeCardClientError.invalidClientLayout }
        var info = TimeCardInfoRaw()
        try callOutput(connection: connection, selector: 0, output: &info)
        guard info.abiVersion >= 10, info.abiVersion <= supportedABIVersion,
              info.capabilities & (1 << bit) != 0 else {
            throw TimeCardClientError.invalidTimingRequest("Unavailable for this driver or FPGA image. No timing registers were probed.")
        }
    }

    private static func callInputOutput<I, O>(connection: io_connect_t, selector: UInt32,
                                             input: inout I, output: inout O) throws {
        var outputSize = MemoryLayout<O>.size
        let result = withUnsafeBytes(of: &input) { inputBytes in
            withUnsafeMutableBytes(of: &output) { outputBytes in
                IOConnectCallStructMethod(connection, selector, inputBytes.baseAddress, inputBytes.count,
                                           outputBytes.baseAddress, &outputSize)
            }
        }
        guard result == KERN_SUCCESS else { throw TimeCardClientError.methodFailed(selector: selector, result: result) }
        guard outputSize == MemoryLayout<O>.size else {
            throw TimeCardClientError.unexpectedOutput(selector: selector, expected: MemoryLayout<O>.size, actual: outputSize)
        }
    }

    private static func openConnection(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> io_connect_t {
        guard let matching = IORegistryEntryIDMatching(descriptor.id) else {
            throw TimeCardClientError.matchingDictionary
        }
        let service = IOServiceGetMatchingService(kIOMainPortDefault, matching)
        guard service != IO_OBJECT_NULL else {
            throw TimeCardClientError.serviceDisappeared
        }
        defer { IOObjectRelease(service) }

        var connection: io_connect_t = 0
        let openResult = IOServiceOpen(
            service, mach_task_self_, 0, &connection
        )
        guard openResult == KERN_SUCCESS else {
            throw TimeCardClientError.openFailed(openResult)
        }
        return connection
    }

    private static func callOutput<T>(
        connection: io_connect_t,
        selector: UInt32,
        output: inout T
    ) throws {
        var outputSize = MemoryLayout<T>.size
        let result = withUnsafeMutableBytes(of: &output) { bytes in
            IOConnectCallStructMethod(
                connection,
                selector,
                nil,
                0,
                bytes.baseAddress,
                &outputSize
            )
        }
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.methodFailed(
                selector: selector, result: result
            )
        }
        guard outputSize == MemoryLayout<T>.size else {
            throw TimeCardClientError.unexpectedOutput(
                selector: selector,
                expected: MemoryLayout<T>.size,
                actual: outputSize
            )
        }
    }

    private static func callInput<T>(
        connection: io_connect_t,
        selector: UInt32,
        input: inout T
    ) throws {
        let result = withUnsafeBytes(of: &input) { bytes in
            IOConnectCallStructMethod(
                connection,
                selector,
                bytes.baseAddress,
                bytes.count,
                nil,
                nil
            )
        }
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.methodFailed(
                selector: selector, result: result
            )
        }
    }

    private static func callInOut<T>(
        connection: io_connect_t,
        selector: UInt32,
        value: inout T
    ) throws {
        var input = value
        var outputSize = MemoryLayout<T>.size
        let result = withUnsafeMutableBytes(of: &value) { outputBytes in
            withUnsafeBytes(of: &input) { inputBytes in
                IOConnectCallStructMethod(
                    connection,
                    selector,
                    inputBytes.baseAddress,
                    inputBytes.count,
                    outputBytes.baseAddress,
                    &outputSize
                )
            }
        }
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.methodFailed(
                selector: selector, result: result
            )
        }
        guard outputSize == MemoryLayout<T>.size else {
            throw TimeCardClientError.unexpectedOutput(
                selector: selector,
                expected: MemoryLayout<T>.size,
                actual: outputSize
            )
        }
    }

    private static func callInOutBytes<T>(
        connection: io_connect_t,
        selector: UInt32,
        input: inout T,
        output: inout [UInt8]
    ) throws {
        var inputCopy = input
        var outputSize = output.count
        let result = output.withUnsafeMutableBytes { outputBytes in
            withUnsafeBytes(of: &inputCopy) { inputBytes in
                IOConnectCallStructMethod(
                    connection,
                    selector,
                    inputBytes.baseAddress,
                    inputBytes.count,
                    outputBytes.baseAddress,
                    &outputSize
                )
            }
        }
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.methodFailed(
                selector: selector, result: result
            )
        }
        guard outputSize == output.count else {
            throw TimeCardClientError.unexpectedOutput(
                selector: selector,
                expected: output.count,
                actual: outputSize
            )
        }
    }

    private static func callInOutRawBytes(
        connection: io_connect_t,
        selector: UInt32,
        input: [UInt8],
        output: inout [UInt8]
    ) throws {
        var outputSize = output.count
        let result = output.withUnsafeMutableBytes { outputBytes in
            input.withUnsafeBytes { inputBytes in
                IOConnectCallStructMethod(
                    connection,
                    selector,
                    inputBytes.baseAddress,
                    inputBytes.count,
                    outputBytes.baseAddress,
                    &outputSize
                )
            }
        }
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.methodFailed(
                selector: selector, result: result
            )
        }
        guard outputSize == output.count else {
            throw TimeCardClientError.unexpectedOutput(
                selector: selector,
                expected: output.count,
                actual: outputSize
            )
        }
    }
}

private struct TimeCardTimeRaw {
    var seconds: UInt64 = 0
    var nanoseconds: UInt32 = 0
    var reserved: UInt32 = 0
}

private struct TimeCardCrossTimestampRaw {
    var cardTime = TimeCardTimeRaw()
    var systemTimeBeforeNanoseconds: UInt64 = 0
    var systemTimeAfterNanoseconds: UInt64 = 0
}

private struct TimeCardInfoRaw {
    var abiVersion: UInt32 = 0
    var driverVersion: UInt32 = 0
    var vendorID: UInt16 = 0
    var deviceID: UInt16 = 0
    var layout: UInt32 = 0
    var advertisedMSIXVectors: UInt32 = 0
    var barSize: UInt64 = 0
    var clockOffset: UInt64 = 0
    var todOffset: UInt64 = 0
    var clockVersion: UInt32 = 0
    var clockStatus: UInt32 = 0
    var clockSelect: UInt32 = 0
    var todVersion: UInt32 = 0
    var todStatus: UInt32 = 0
    var utcStatus: UInt32 = 0
    var leap: UInt32 = 0
    var gnssStatus: UInt32 = 0
    var satellites: UInt32 = 0
    var boardProfile: UInt32 = 0
    var capabilities: UInt32 = 0
    var validFields: UInt64 = 0
    var pciRevision: UInt32 = 0
    var reserved: UInt32 = 0
}

private struct TimeCardSMARaw {
    var size: UInt32 = 0
    var connector: UInt32 = 0
    var direction: UInt32 = 0
    var function: UInt32 = 0
    var flags: UInt32 = 0
    var inputMap: UInt32 = 0
    var outputMap: UInt32 = 0
    var reserved: UInt32 = 0

    var route: TimeCardSMARoute {
        TimeCardSMARoute(
            connector: connector,
            direction: TimeCardSMADirection(rawValue: direction) ?? .disabled,
            function: function,
            flags: flags,
            inputMap: inputMap,
            outputMap: outputMap
        )
    }
}

private struct TimeCardLEDRaw {
    var size: UInt32 = 0
    var led: UInt32 = 0
    var flags: UInt32 = 0
    var red: UInt32 = 0
    var green: UInt32 = 0
    var blue: UInt32 = 0
    var globalCurrent: UInt32 = 0
    var muxChannelMask: UInt32 = 0
    var controllerStatus: UInt32 = 0
    var interruptStatus: UInt32 = 0
    var openOutputMask: UInt32 = 0
    var shortOutputMask: UInt32 = 0

    var state: TimeCardLEDState {
        TimeCardLEDState(
            led: TimeCardLEDKind(rawValue: led) ?? .gnss1,
            flags: flags,
            red: red,
            green: green,
            blue: blue,
            globalCurrent: globalCurrent,
            muxChannelMask: muxChannelMask,
            controllerStatus: controllerStatus,
            interruptStatus: interruptStatus,
            openOutputMask: openOutputMask,
            shortOutputMask: shortOutputMask
        )
    }
}

private struct TimeCardI2CStatusRaw {
    var size: UInt32 = 0
    var flags: UInt32 = 0
    var offset: UInt64 = 0
    var control: UInt32 = 0
    var status: UInt32 = 0
    var interruptStatus: UInt32 = 0
    var interruptEnable: UInt32 = 0
    var txFifoOccupancy: UInt32 = 0
    var rxFifoOccupancy: UInt32 = 0
    var knownDeviceMask: UInt32 = 0
    var reserved: UInt32 = 0

    var snapshot: TimeCardI2CStatusSnapshot {
        TimeCardI2CStatusSnapshot(
            flags: flags,
            offset: offset,
            control: control,
            status: status,
            interruptStatus: interruptStatus,
            interruptEnable: interruptEnable,
            txFifoOccupancy: txFifoOccupancy,
            rxFifoOccupancy: rxFifoOccupancy,
            knownDeviceMask: knownDeviceMask
        )
    }
}

private struct TimeCardI2CProbeRaw {
    var size: UInt32 = 0
    var address: UInt32 = 0
    var present: UInt32 = 0
    var controllerStatus: UInt32 = 0
    var interruptStatus: UInt32 = 0
    var reserved0: UInt32 = 0
    var reserved1: UInt32 = 0
    var reserved2: UInt32 = 0

    var result: TimeCardI2CProbeResult {
        TimeCardI2CProbeResult(
            address: address,
            isPresent: present != 0,
            controllerStatus: controllerStatus,
            interruptStatus: interruptStatus
        )
    }
}

private struct TimeCardI2CReadRequestRaw {
    var size: UInt32 = 0
    var address: UInt32 = 0
    var subaddressLength: UInt32 = 0
    var subaddress: UInt32 = 0
    var length: UInt32 = 0
    var reserved0: UInt32 = 0
    var reserved1: UInt32 = 0
    var reserved2: UInt32 = 0
}

private enum TimeCardI2CTransferRawLayout {
    static let size = 276
    static let addressOffset = 4
    static let lengthOffset = 8
    static let controllerStatusOffset = 12
    static let interruptStatusOffset = 16
    static let dataOffset = 20

    static func readUInt32(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        guard bytes.count >= offset + 4 else { return 0 }
        return UInt32(bytes[offset]) |
            (UInt32(bytes[offset + 1]) << 8) |
            (UInt32(bytes[offset + 2]) << 16) |
            (UInt32(bytes[offset + 3]) << 24)
    }

}

private struct TimeCardI2CMuxRaw {
    var size: UInt32 = 0
    var present: UInt32 = 0
    var channelMask: UInt32 = 0
    var controllerStatus: UInt32 = 0
    var interruptStatus: UInt32 = 0
    var reserved0: UInt32 = 0
    var reserved1: UInt32 = 0
    var reserved2: UInt32 = 0

    var snapshot: TimeCardI2CMuxSnapshot {
        TimeCardI2CMuxSnapshot(
            isPresent: present != 0,
            channelMask: channelMask,
            controllerStatus: controllerStatus,
            interruptStatus: interruptStatus
        )
    }
}

private struct TimeCardUARTConfigRaw {
    var port: UInt32 = 0
    var baud: UInt32 = 0
}

private struct TimeCardUARTReadRequestRaw {
    var port: UInt32 = 0
    var maximumBytes: UInt32 = 0
    var timeoutMilliseconds: UInt32 = 0
    var reserved: UInt32 = 0
}

private enum TimeCardUARTTransferRawLayout {
    static let size = 272
    static let portOffset = 0
    static let lengthOffset = 4
    static let timeoutOffset = 8
    static let lineStatusOffset = 12
    static let dataOffset = 16

    static func readUInt32(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        guard bytes.count >= offset + 4 else { return 0 }
        return UInt32(bytes[offset]) |
            (UInt32(bytes[offset + 1]) << 8) |
            (UInt32(bytes[offset + 2]) << 16) |
            (UInt32(bytes[offset + 3]) << 24)
    }

    static func writeUInt32(_ value: UInt32, into bytes: inout [UInt8], at offset: Int) {
        guard bytes.count >= offset + 4 else { return }
        bytes[offset] = UInt8(value & 0xff)
        bytes[offset + 1] = UInt8((value >> 8) & 0xff)
        bytes[offset + 2] = UInt8((value >> 16) & 0xff)
        bytes[offset + 3] = UInt8((value >> 24) & 0xff)
    }
}

private struct TimeCardUARTObserveRaw {
    var size: UInt32 = 0
    var port: UInt32 = 0
    var timeoutMilliseconds: UInt32 = 0
    var flags: UInt32 = 0
    var lineStatus: UInt32 = 0
    var reserved0: UInt32 = 0
    var reserved1: UInt32 = 0
    var reserved2: UInt32 = 0

    var observation: TimeCardUARTObservation {
        TimeCardUARTObservation(
            port: TimeCardUARTPort(rawValue: port) ?? .gnss,
            timeoutMilliseconds: timeoutMilliseconds,
            flags: flags,
            lineStatus: lineStatus
        )
    }
}

private struct TimeCardSensorReadingRaw {
    var size: UInt32 = 0
    var type: UInt32 = 0
    var flags: UInt32 = 0
    var muxChannelMask: UInt32 = 0
    var address: UInt32 = 0
    var temperatureMilliCelsius: Int32 = 0
    var humidityMilliPercent: UInt32 = 0
    var pressureRaw: UInt32 = 0
    var raw0: UInt32 = 0
    var raw1: UInt32 = 0
    var raw2: UInt32 = 0
    var reserved: UInt32 = 0

    var reading: TimeCardSensorReading {
        TimeCardSensorReading(
            kind: TimeCardSensorKind(rawValue: type) ?? .unknown,
            flags: flags,
            muxChannelMask: muxChannelMask,
            address: address,
            temperatureMilliCelsius: temperatureMilliCelsius,
            humidityMilliPercent: humidityMilliPercent,
            pressureRaw: pressureRaw,
            raw0: raw0,
            raw1: raw1,
            raw2: raw2
        )
    }
}

private struct TimeCardSensorTelemetryRaw {
    var size: UInt32 = 0
    var flags: UInt32 = 0
    var boardProfile: UInt32 = 0
    var capabilities: UInt32 = 0
    var muxChannelMask: UInt32 = 0
    var restoredMuxChannelMask: UInt32 = 0
    var controllerStatus: UInt32 = 0
    var interruptStatus: UInt32 = 0
    var readingCount: UInt32 = 0
    var icp10100Otp0: Int32 = 0
    var icp10100Otp1: Int32 = 0
    var icp10100Otp2: Int32 = 0
    var icp10100Otp3: Int32 = 0
    var reserved0: UInt32 = 0
    var reserved1: UInt32 = 0
    var reserved2: UInt32 = 0
    var reading0 = TimeCardSensorReadingRaw()
    var reading1 = TimeCardSensorReadingRaw()
    var reading2 = TimeCardSensorReadingRaw()
    var reading3 = TimeCardSensorReadingRaw()
    var reading4 = TimeCardSensorReadingRaw()
    var reading5 = TimeCardSensorReadingRaw()
    var reading6 = TimeCardSensorReadingRaw()
    var reading7 = TimeCardSensorReadingRaw()
    var reading8 = TimeCardSensorReadingRaw()
    var reading9 = TimeCardSensorReadingRaw()
    var reading10 = TimeCardSensorReadingRaw()
    var reading11 = TimeCardSensorReadingRaw()
    var reading12 = TimeCardSensorReadingRaw()
    var reading13 = TimeCardSensorReadingRaw()
    var reading14 = TimeCardSensorReadingRaw()
    var reading15 = TimeCardSensorReadingRaw()

    var snapshot: TimeCardSensorSnapshot {
        let rawReadings = [
            reading0, reading1, reading2, reading3,
            reading4, reading5, reading6, reading7,
            reading8, reading9, reading10, reading11,
            reading12, reading13, reading14, reading15,
        ]
        let cappedCount = min(Int(readingCount), rawReadings.count)
        return TimeCardSensorSnapshot(
            flags: flags,
            boardProfile: boardProfile,
            capabilities: capabilities,
            muxChannelMask: muxChannelMask,
            restoredMuxChannelMask: restoredMuxChannelMask,
            controllerStatus: controllerStatus,
            interruptStatus: interruptStatus,
            icp10100Otp: [
                icp10100Otp0, icp10100Otp1, icp10100Otp2, icp10100Otp3,
            ],
            readings: rawReadings.prefix(cappedCount).map(\.reading)
        )
    }
}
