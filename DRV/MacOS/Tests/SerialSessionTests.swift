/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation
import Darwin

private final class TestPTY {
    var master: Int32 = -1
    var slave: Int32 = -1
    let path: String
    init() {
        var name = [CChar](repeating: 0, count: 256)
        precondition(openpty(&master, &slave, &name, nil, nil) == 0)
        path = String(cString: name)
        precondition(fcntl(master, F_SETFL, O_NONBLOCK) == 0)
    }
    deinit { if master >= 0 { Darwin.close(master) }; if slave >= 0 { Darwin.close(slave) } }
    func inject(_ bytes: [UInt8]) { precondition(bytes.withUnsafeBytes { Darwin.write(master, $0.baseAddress, $0.count) } == bytes.count) }
    func received() -> [UInt8] {
        var bytes = [UInt8](repeating: 0, count: 8192)
        let count = bytes.withUnsafeMutableBytes { Darwin.read(master, $0.baseAddress, $0.count) }
        return count > 0 ? Array(bytes.prefix(count)) : []
    }
}

@main enum SerialSessionTests {
    static func check(_ condition: Bool) { precondition(condition) }
    @MainActor static func main() async throws {
        setbuf(stdout, nil)
        try modelTests()
        try transportTests()
        try await controllerTests()
        print("Serial session tests passed: line settings, strict text/hex input, bounded ledger/exports, PTY RX/TX, restoration, disconnect, stale/single-use review, cancellation, unplug, backpressure timeout and reconnect isolation.")
    }
    static func rejects(_ body: () throws -> Void) {
        do { try body(); preconditionFailure("Invalid operation accepted") } catch { }
    }
    static func modelTests() throws {
        check(try SerialPayload.parse("Aµ", format: .text, ending: .crlf) == [65, 0xc2, 0xb5, 13, 10])
        check(try SerialPayload.parse("00 7f\nFF", format: .hex, ending: .crlf) == [0, 127, 255])
        for bad in ["", "0", "0x12", "1G", "éé", "AA,BB"] { rejects { _ = try SerialPayload.parse(bad, format: .hex, ending: .none) } }
        rejects { _ = try SerialPayload.parse(String(repeating: "A", count: 4097), format: .text, ending: .none) }
        rejects { _ = try SerialPayload.parse(String(repeating: "A", count: 4096), format: .text, ending: .lf) }
        check(try SerialPayload.parse(String(repeating: "FF", count: 4096), format: .hex, ending: .none).count == 4096)
        precondition(SerialPayload.visibleText([0x1b, 0x41, 0, 10]) == "·A·\n")
        for bits in 5...8 { for parity in SerialLineSettings.Parity.allCases { for stops in [1, 2] { for flow in SerialLineSettings.Flow.allCases {
            let settings = SerialLineSettings(baud: 9600, dataBits: bits, parity: parity, stopBits: stops, flow: flow)
            var inherited = termios()
            inherited.c_cflag = SerialLineSettings.controlMask
            inherited.c_iflag = SerialLineSettings.inputMask
            var value = try settings.configured(from: inherited)
            precondition(cfgetispeed(&value) == 9600 && cfgetospeed(&value) == 9600)
            precondition((value.c_cflag & tcflag_t(PARENB) != 0) == (parity != .none))
            precondition((value.c_cflag & tcflag_t(PARODD) != 0) == (parity == .odd))
            precondition((value.c_cflag & tcflag_t(CSTOPB) != 0) == (stops == 2))
            precondition((value.c_iflag & tcflag_t(IXON | IXOFF) != 0) == (flow == .software || flow == .both))
            precondition((value.c_cflag & tcflag_t(CCTS_OFLOW | CRTS_IFLOW) != 0) == (flow == .hardware || flow == .both))
            precondition(value.c_cflag & tcflag_t(CDTR_IFLOW | CDSR_OFLOW | CCAR_OFLOW) == 0)
        } } } }
        rejects { _ = try SerialLineSettings(baud: 12345).configured(from: termios()) }
        rejects { _ = try SerialLineSettings(dataBits: 9).configured(from: termios()) }
        rejects { _ = try SerialLineSettings(stopBits: 3).configured(from: termios()) }
        var record = SerialSessionRecord(port: "/dev/test,\"port", settings: .init())
        for index in 0..<600 { record.append(.rx, bytes: [UInt8(index % 256)], elapsed: Double(index)) }
        precondition(record.events.count == 500 && record.evictedEvents == 100 && record.receivedBytes == 600)
        precondition(record.retainedRX.count == 500 && record.retainedBytes == 500 && record.events.first?.id == 100)
        for _ in 0..<100 { record.append(.tx, bytes: Array(repeating: 255, count: 16_384), elapsed: 600) }
        precondition(record.retainedBytes <= SerialSessionRecord.maximumRetainedBytes && record.evictedBytes > 0)
        precondition(record.transmittedBytes == 1_638_400)
        record.finish()
        let json = try JSONSerialization.jsonObject(with: record.json()) as! [String: Any]
        precondition(json["schemaVersion"] as! Int == 1 && json["endedAt"] != nil && json["evictedEvents"] as! Int > 0)
        precondition(record.csv().contains("\"/dev/test,\"\"port\"") && record.csv().contains("TX accepted"))
    }
    static func transportTests() throws {
        let pty = TestPTY()
        var before = termios(); precondition(tcgetattr(pty.slave, &before) == 0)
        let port = try SerialSessionTransport(path: pty.path, settings: .init())
        check(try port.receive().isEmpty)
        // Darwin's pseudo-terminal implementation does not reject every second
        // open after TIOCEXCL. Real serial-driver exclusivity requires hardware
        // testing; never advertise PTY coverage as that guarantee.
        pty.inject([0, 65, 255, 10])
        var rx: [UInt8] = []
        for _ in 0..<100 where rx.isEmpty { rx = try port.receive(); if rx.isEmpty { usleep(1000) } }
        precondition(rx == [0, 65, 255, 10])
        check(try port.transmit([0x42, 0, 13][...]) == 3)
        var tx: [UInt8] = []
        for _ in 0..<100 where tx.isEmpty { tx = pty.received(); if tx.isEmpty { usleep(1000) } }
        precondition(tx == [0x42, 0, 13])
        precondition(port.close() == nil && port.close() == nil)
        var after = termios(); precondition(tcgetattr(pty.slave, &after) == 0)
        // The kernel may mark pending canonical input for reprocessing on restore.
        precondition(after.c_cflag == before.c_cflag && after.c_iflag == before.c_iflag && after.c_lflag & ~tcflag_t(PENDIN) == before.c_lflag & ~tcflag_t(PENDIN) && after.c_oflag == before.c_oflag)
        precondition(cfgetispeed(&after) == cfgetispeed(&before) && cfgetospeed(&after) == cfgetospeed(&before))
        rejects { _ = try port.receive() }; rejects { _ = try port.transmit([1][...]) }
        let reopened = try SerialSessionTransport(path: pty.path, settings: .init()); reopened.close()
        rejects { _ = try SerialSessionTransport(path: "/dev/null", settings: .init()) }
        rejects { _ = try SerialSessionTransport(path: "/tmp/not-a-serial-device", settings: .init()) }
    }
    @MainActor static func until(_ condition: () -> Bool) async throws {
        let deadline = ProcessInfo.processInfo.systemUptime + 3
        while !condition() && ProcessInfo.processInfo.systemUptime < deadline { try await Task.sleep(for: .milliseconds(10)) }
        precondition(condition(), "Timed out waiting for session state")
    }
    @MainActor static func controllerTests() async throws {
        let pty = TestPTY(); let model = SerialSessionController(); var released = 0
        model.connect(path: pty.path, settings: .init(), knownPorts: []) { released += 1 }
        precondition(!model.active && released == 1)
        model.connect(path: pty.path, settings: .init(), knownPorts: [pty.path]) { released += 1 }
        try await until { model.state == .connected }
        pty.inject([65, 66, 67]); try await until { model.record?.receivedBytes == 3 }
        let review = try model.reviewSend("00 FF 42", format: .hex, ending: .none)
        precondition(review.isFresh(at: review.createdAtUptime))
        precondition(review.isFresh(at: review.createdAtUptime + 30))
        precondition(!review.isFresh(at: review.createdAtUptime + 30.001))
        precondition(!review.isFresh(at: review.createdAtUptime - 1))
        model.send(review); try await until { !model.sending }
        precondition(model.record?.transmittedBytes == 3)
        var tx: [UInt8] = []
        for _ in 0..<100 where tx.isEmpty { tx = pty.received(); try await Task.sleep(for: .milliseconds(1)) }
        precondition(tx == [0, 255, 66])
        model.send(review); precondition(!model.sending && model.record?.transmittedBytes == 3)
        let expired = SerialSendReview(sessionID: review.sessionID, port: review.port, settings: review.settings, bytes: [1], createdAtUptime: ProcessInfo.processInfo.systemUptime - 31)
        model.send(expired); precondition(!model.sending && model.message.contains("stale"))
        model.disconnect(); precondition(!model.active && model.record?.endedAt != nil && released == 2)
        model.send(review); precondition(!model.sending)
        model.connect(path: pty.path, settings: .init(), knownPorts: [pty.path]) { released += 1 }
        try await until { model.state == .connected }
        precondition(model.record?.receivedBytes == 0 && model.record?.id != review.sessionID)
        model.send(review); precondition(!model.sending && model.message.contains("stale"))
        Darwin.close(pty.master); pty.master = -1
        try await until { model.state == .failed }
        precondition(released == 3 && model.record?.endedAt != nil)
        let pending = TestPTY()
        model.connect(path: pending.path, settings: .init(), knownPorts: [pending.path]) { released += 1 }
        model.disconnect()
        model.disconnect()
        try await until { !model.active }
        precondition(released == 4)
        let finalPort = try SerialSessionTransport(path: pending.path, settings: .init()); finalPort.close()

        // Stop consuming the master endpoint to exercise OS backpressure and a
        // bounded write timeout. Only accepted bytes belong in the TX ledger.
        let blockedPTY = TestPTY(); let blocked = SerialSessionController()
        blocked.connect(path: blockedPTY.path, settings: .init(), knownPorts: [blockedPTY.path])
        try await until { blocked.state == .connected }
        var requested: UInt64 = 0
        for _ in 0..<32 where blocked.state == .connected {
            let payload = try blocked.reviewSend(String(repeating: "X", count: 4096), format: .text, ending: .none)
            requested += 4096; blocked.send(payload)
            try await until { !blocked.sending }
        }
        precondition(blocked.state == .failed && blocked.message.contains("timed out"))
        precondition(blocked.record!.transmittedBytes > 0 && blocked.record!.transmittedBytes < requested)
        precondition(blocked.record!.events.filter { $0.direction == .tx }.reduce(0) { $0 + $1.bytes.count } == Int(blocked.record!.transmittedBytes))
    }
}
