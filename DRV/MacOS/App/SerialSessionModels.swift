/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation
import Darwin

enum SerialSessionError: LocalizedError {
    case invalid(String)
    case system(String, Int32)
    var errorDescription: String? {
        switch self {
        case .invalid(let text): text
        case .system(let operation, let code): "\(operation): \(String(cString: strerror(code))) (\(code))"
        }
    }
}

struct SerialLineSettings: Equatable, Codable, Sendable {
    enum Parity: String, CaseIterable, Codable { case none = "None", even = "Even", odd = "Odd" }
    enum Flow: String, CaseIterable, Codable { case none = "None", hardware = "RTS/CTS", software = "XON/XOFF", both = "RTS/CTS + XON/XOFF" }
    static let baudRates: [UInt32] = [1_200, 2_400, 4_800, 9_600, 19_200, 38_400, 57_600, 115_200, 230_400]
    var baud: UInt32 = 115_200
    var dataBits = 8
    var parity: Parity = .none
    var stopBits = 1
    var flow: Flow = .none
    var summary: String { "\(baud) · \(dataBits)\(parity.rawValue.prefix(1))\(stopBits) · \(flow.rawValue)" }
    // Clear every Darwin modem-flow mode, not just the modes offered by the UI.
    // Otherwise a previous client's DTR/DSR/DCD gating could silently survive.
    static let controlMask = tcflag_t(CSIZE | PARENB | PARODD | CSTOPB | CCTS_OFLOW | CRTS_IFLOW | CDTR_IFLOW | CDSR_OFLOW | CCAR_OFLOW)
    static let inputMask = tcflag_t(IXON | IXOFF | IXANY | INPCK)

    func configured(from original: termios) throws -> termios {
        guard Self.baudRates.contains(baud), (5...8).contains(dataBits), [1, 2].contains(stopBits) else {
            throw SerialSessionError.invalid("Unsupported serial line settings.")
        }
        var result = original
        cfmakeraw(&result)
        result.c_cflag &= ~Self.controlMask
        result.c_cflag |= tcflag_t(CLOCAL | CREAD)
        result.c_cflag |= [5: tcflag_t(CS5), 6: tcflag_t(CS6), 7: tcflag_t(CS7), 8: tcflag_t(CS8)][dataBits]!
        result.c_iflag &= ~Self.inputMask
        if parity != .none { result.c_cflag |= tcflag_t(PARENB); result.c_iflag |= tcflag_t(INPCK) }
        if parity == .odd { result.c_cflag |= tcflag_t(PARODD) }
        if stopBits == 2 { result.c_cflag |= tcflag_t(CSTOPB) }
        if flow == .hardware || flow == .both { result.c_cflag |= tcflag_t(CCTS_OFLOW | CRTS_IFLOW) }
        if flow == .software || flow == .both { result.c_iflag |= tcflag_t(IXON | IXOFF) }
        withUnsafeMutableBytes(of: &result.c_cc) {
            $0[Int(VMIN)] = 0; $0[Int(VTIME)] = 0
            $0[Int(VSTART)] = 0x11; $0[Int(VSTOP)] = 0x13
        }
        guard cfsetispeed(&result, speed_t(baud)) == 0, cfsetospeed(&result, speed_t(baud)) == 0 else {
            throw SerialSessionError.system("Set baud", errno)
        }
        return result
    }
}

enum SerialSendFormat: String, CaseIterable { case text = "UTF-8 text", hex = "Hex bytes" }
enum SerialLineEnding: String, CaseIterable { case none = "None", lf = "LF", cr = "CR", crlf = "CRLF"
    var bytes: [UInt8] { switch self { case .none: []; case .lf: [10]; case .cr: [13]; case .crlf: [13, 10] } }
}
enum SerialPayload {
    static let maximumBytes = 4096
    static func parse(_ input: String, format: SerialSendFormat, ending: SerialLineEnding) throws -> [UInt8] {
        guard input.utf8.count <= maximumBytes * 3 else { throw SerialSessionError.invalid("Input exceeds the 4 KiB send limit.") }
        var bytes: [UInt8]
        switch format {
        case .text: bytes = Array(input.utf8) + ending.bytes
        case .hex:
            let digits = Array(input.unicodeScalars.filter { !CharacterSet.whitespacesAndNewlines.contains($0) })
            guard !digits.isEmpty, digits.count.isMultiple(of: 2), digits.allSatisfy({
                (48...57).contains($0.value) || (65...70).contains($0.value) || (97...102).contains($0.value)
            }) else { throw SerialSessionError.invalid("Hex input needs complete byte pairs, with optional whitespace. Do not include 0x prefixes.") }
            bytes = []; var index = 0
            while index < digits.count {
                bytes.append(UInt8(String(String.UnicodeScalarView(digits[index...index + 1])), radix: 16)!)
                index += 2
            }
        }
        guard !bytes.isEmpty, bytes.count <= maximumBytes else { throw SerialSessionError.invalid("Send between 1 and 4096 bytes.") }
        return bytes
    }
    static func hex(_ bytes: some Sequence<UInt8>) -> String { bytes.map { String(format: "%02X", $0) }.joined(separator: " ") }
    static func visibleText(_ bytes: [UInt8]) -> String {
        String(String(decoding: bytes, as: UTF8.self).unicodeScalars.map { scalar in
            scalar.value < 32 && scalar.value != 10 && scalar.value != 9 || scalar.value == 127 ? "·" : String(scalar)
        }.joined())
    }
}

struct SerialTimelineEvent: Codable, Identifiable, Sendable {
    enum Direction: String, Codable { case rx = "RX", tx = "TX accepted", status = "Status" }
    let id: Int
    let timestamp: Date
    let elapsedSeconds: Double
    let direction: Direction
    let bytes: [UInt8]
    let note: String
}

struct SerialSessionRecord: Encodable, Sendable {
    static let maximumEvents = 500
    static let maximumRetainedBytes = 1_048_576
    let schemaVersion = 1
    let id: UUID
    let port: String
    let settings: SerialLineSettings
    let startedAt: Date
    private(set) var endedAt: Date?
    private(set) var events: [SerialTimelineEvent] = []
    private(set) var receivedBytes: UInt64 = 0
    private(set) var transmittedBytes: UInt64 = 0
    private(set) var evictedEvents = 0
    private(set) var evictedBytes = 0
    private(set) var retainedBytes = 0
    private var nextID = 0
    let timestampMeaning = "Host observation times, not precision receiver timestamps. TX counts OS-accepted bytes, not device acknowledgement."

    init(id: UUID = UUID(), port: String, settings: SerialLineSettings, startedAt: Date = Date()) {
        self.id = id; self.port = port; self.settings = settings; self.startedAt = startedAt
    }

    mutating func append(_ direction: SerialTimelineEvent.Direction, bytes: [UInt8] = [], note: String = "", at: Date = Date(), elapsed: Double) {
        // Transport publishes bounded chunks; reject accidental oversized callers.
        precondition(bytes.count <= 16_384)
        if direction == .rx { receivedBytes += UInt64(bytes.count) }
        if direction == .tx { transmittedBytes += UInt64(bytes.count) }
        events.append(.init(id: nextID, timestamp: at, elapsedSeconds: max(0, elapsed), direction: direction, bytes: bytes, note: String(note.prefix(512))))
        nextID += 1; retainedBytes += bytes.count
        while events.count > Self.maximumEvents || retainedBytes > Self.maximumRetainedBytes {
            let removed = events.removeFirst()
            retainedBytes -= removed.bytes.count; evictedEvents += 1; evictedBytes += removed.bytes.count
        }
    }
    mutating func finish(at: Date = Date()) { endedAt = at }
    var retainedRX: [UInt8] { events.filter { $0.direction == .rx }.flatMap(\.bytes) }
    func json() throws -> Data {
        let encoder = JSONEncoder(); encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try encoder.encode(self)
    }
    func csv() -> String {
        let formatter = ISO8601DateFormatter(); formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        func quote(_ value: String) -> String { "\"" + value.replacingOccurrences(of: "\"", with: "\"\"") + "\"" }
        let header = "session_id,port,host_timestamp,elapsed_seconds,direction,byte_count,hex\n"
        return header + events.map { event in
            [id.uuidString, port, formatter.string(from: event.timestamp), String(format: "%.6f", locale: Locale(identifier: "en_US_POSIX"), event.elapsedSeconds), event.direction.rawValue, String(event.bytes.count), SerialPayload.hex(event.bytes)].map(quote).joined(separator: ",")
        }.joined(separator: "\n") + "\n"
    }
}
