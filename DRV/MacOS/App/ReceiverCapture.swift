/* SPDX-License-Identifier: BSD-3-Clause */

import Foundation

enum ReceiverRawFormat: String, CaseIterable, Identifiable {
    case ascii = "ASCII", hex = "Hex", decimal = "Decimal", binary = "Binary"
    var id: Self { self }

    func render(_ bytes: ArraySlice<UInt8>) -> String {
        if self == .ascii {
            return String(bytes.map { byte in
                (byte == 9 || byte == 10 || byte == 13 || (32...126).contains(byte))
                    ? Character(UnicodeScalar(byte)) : "·"
            })
        }
        return stride(from: bytes.startIndex, to: bytes.endIndex, by: 16).map { offset in
            let row = bytes[offset..<min(offset + 16, bytes.endIndex)].map { byte in
                switch self {
                case .hex: return String(format: "%02X", byte)
                case .decimal: return String(format: "%03d", byte)
                case .binary: return String(repeating: "0", count: 8 - String(byte, radix: 2).count) + String(byte, radix: 2)
                case .ascii: return ""
                }
            }.joined(separator: " ")
            return String(format: "%08X  ", offset) + row
        }.joined(separator: "\n")
    }
}

struct ReceiverCaptureDocument: Sendable {
    static let maximumBytes = 16 * 1024 * 1024
    static let maximumMessages = 20_000
    let source: String
    let bytes: [UInt8]
    let messages: [ReceiverStreamMessage]
    let nmeaSentences: [NMEASentence]
    let ubxFrames: [TimeCardUBXFrame]

    init(source: String, bytes: [UInt8]) {
        self.source = source
        self.bytes = Array(bytes.prefix(Self.maximumBytes))
        self.messages = ReceiverStreamMessage.parse(from: self.bytes, limit: Self.maximumMessages)
        self.nmeaSentences = self.messages.filter { $0.protocolName == "NMEA" }.compactMap { NMEASentence.parse($0.detail) }
        let retainedBytes = self.bytes
        self.ubxFrames = self.messages.filter { $0.protocolName == "UBX" }.compactMap {
            TimeCardUBXFrame.parseFrames(from: Array(retainedBytes[$0.offset..<($0.offset + $0.byteCount)])).first
        }
    }

    var decodeLimitReached: Bool { messages.count == Self.maximumMessages }
    var failedChecksums: Int { messages.filter { $0.checksumState == .failed }.count }
    var decodedBytes: Int { messages.reduce(0) { $0 + $1.byteCount } }

    func filtered(protocolName: String, errorsOnly: Bool, search: String) -> [ReceiverStreamMessage] {
        let query = search.trimmingCharacters(in: .whitespacesAndNewlines)
        return messages.filter {
            (protocolName == "All" || $0.protocolName == protocolName) &&
            (!errorsOnly || $0.checksumState == .failed) &&
            (query.isEmpty || [$0.name, $0.summary, $0.detail, $0.offsetText]
                .joined(separator: " ").localizedCaseInsensitiveContains(query))
        }
    }

    func payload(for message: ReceiverStreamMessage) -> ArraySlice<UInt8> {
        guard message.offset >= 0, message.byteCount >= 0,
              message.offset <= bytes.count,
              message.byteCount <= bytes.count - message.offset else { return [] }
        return bytes[message.offset..<(message.offset + message.byteCount)]
    }

    func exportJSON(_ selected: [ReceiverStreamMessage]) throws -> Data {
        let rows: [[String: Any]] = selected.map { message in
            ["offset": message.offset, "byteCount": message.byteCount,
             "protocol": message.protocolName, "name": message.name,
             "checksum": message.checksumState.label, "summary": message.summary,
             "detail": message.detail,
             "hex": payload(for: message).map { String(format: "%02X", $0) }.joined(separator: " ")]
        }
        return try JSONSerialization.data(withJSONObject: [
            "schemaVersion": 1, "source": source, "captureByteCount": bytes.count,
            "decodedMessageCount": messages.count, "exportedMessageCount": selected.count,
            "decodeLimitReached": decodeLimitReached, "messages": rows,
        ], options: [.prettyPrinted, .sortedKeys])
    }

    func exportCSV(_ selected: [ReceiverStreamMessage]) -> String {
        var rows = [Self.csv(["offset", "byte_count", "protocol", "name", "checksum", "summary", "detail", "hex"])]
        rows += selected.map {
            Self.csv([String($0.offset), String($0.byteCount), $0.protocolName, $0.name,
                      $0.checksumState.label, $0.summary, $0.detail,
                      payload(for: $0).map { String(format: "%02X", $0) }.joined(separator: " ")])
        }
        return rows.joined(separator: "\r\n") + "\r\n"
    }

    func exportText(_ selected: [ReceiverStreamMessage]) -> String {
        (["Source: \(source)", "Capture: \(bytes.count) bytes, \(messages.count) decoded messages",
          "Export: \(selected.count) matching messages; decode limit reached: \(decodeLimitReached)"] + selected.map {
            "\($0.offsetText) [\($0.protocolName)] \($0.name) | \($0.checksumState.label)\n\($0.summary)\n\($0.detail)"
        }).joined(separator: "\n") + "\n"
    }

    static func csv(_ fields: [String]) -> String {
        fields.map { "\"" + $0.replacingOccurrences(of: "\"", with: "\"\"") + "\"" }.joined(separator: ",")
    }

    static func readFile(_ url: URL) throws -> [UInt8] {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        // Bound the read itself, including files that grow after the dialog opens.
        let data = try handle.read(upToCount: maximumBytes + 1) ?? Data()
        guard data.count <= maximumBytes else { throw ReplayError.tooLarge }
        return Array(data)
    }

    enum ReplayError: LocalizedError {
        case tooLarge
        var errorDescription: String? { "Replay files must be 16 MiB or smaller." }
    }
}
