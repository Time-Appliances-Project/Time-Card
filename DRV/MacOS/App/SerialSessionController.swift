/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation
import Combine

struct SerialSendReview: Identifiable {
    let id = UUID()
    let sessionID: UUID
    let port: String
    let settings: SerialLineSettings
    let bytes: [UInt8]
    let createdAtUptime: Double
    func isFresh(at uptime: Double) -> Bool {
        let age = uptime - createdAtUptime
        return age >= 0 && age <= 30
    }
}

@MainActor final class SerialSessionController: ObservableObject {
    enum State: String { case disconnected = "Disconnected", connecting = "Connecting", connected = "Connected", stopping = "Stopping", failed = "Failed" }
    @Published private(set) var state: State = .disconnected
    @Published private(set) var record: SerialSessionRecord?
    @Published private(set) var message = "Select a macOS serial port. No connection or commands are started automatically."
    @Published private(set) var sending = false
    var active: Bool { [.connecting, .connected, .stopping].contains(state) }
    private var transport: SerialSessionTransport?
    private var reader: Task<Void, Never>?
    private var writer: Task<Void, Never>?
    private var opener: Task<Void, Never>?
    private var startedUptime = 0.0
    private var releaseLease: (() -> Void)?
    private var pendingSendID: UUID?
    deinit { reader?.cancel(); writer?.cancel(); opener?.cancel(); transport?.close() }

    func connect(path: String, settings: SerialLineSettings, knownPorts: [String], release: @escaping () -> Void = {}) {
        guard !active else { release(); return }
        guard knownPorts.contains(path) else { message = "Port inventory changed. Refresh and select a port again."; release(); return }
        state = .connecting; sending = false; pendingSendID = nil; startedUptime = ProcessInfo.processInfo.systemUptime
        releaseLease = release
        record = SerialSessionRecord(port: path, settings: settings)
        let id = record!.id
        message = "Opening \(path) with \(settings.summary)…"
        append(.status, note: message)
        opener = Task { [weak self] in
            let result = await Task.detached(priority: .utility) {
                Result { try SerialSessionTransport(path: path, settings: settings) }
            }.value
            guard let self else { if case .success(let port) = result { port.close() }; return }
            guard self.record?.id == id else { if case .success(let port) = result { port.close() }; return }
            guard !Task.isCancelled, self.state == .connecting else {
                if case .success(let port) = result { port.close() }
                self.finish("Connection cancelled.", failed: false)
                return
            }
            switch result {
            case .failure(let error): self.finish(error.localizedDescription, failed: true)
            case .success(let port):
                self.transport = port; self.state = .connected
                self.message = "Connected to \(path). Receiving continuously; sends require review."
                self.append(.status, note: self.message)
                self.startReader(port, sessionID: id)
            }
        }
    }

    func disconnect(reason: String = "Disconnected by user.") {
        guard active, state != .stopping else { return }
        let wasOpening = state == .connecting
        state = .stopping; pendingSendID = nil
        opener?.cancel(); reader?.cancel(); writer?.cancel()
        let warning = transport?.close(); transport = nil
        // Keep the lease until an in-flight open has completed and closed.
        if wasOpening { message = "Cancelling connection…"; return }
        finish(reason + (warning.map { " Cleanup warning: \($0)." } ?? ""), failed: warning != nil)
    }

    func reviewSend(_ input: String, format: SerialSendFormat, ending: SerialLineEnding) throws -> SerialSendReview {
        guard state == .connected, !sending, let record else { throw SerialSessionError.invalid("Connect a port and finish the current send first.") }
        let review = SerialSendReview(sessionID: record.id, port: record.port, settings: record.settings,
            bytes: try SerialPayload.parse(input, format: format, ending: ending), createdAtUptime: ProcessInfo.processInfo.systemUptime)
        pendingSendID = review.id
        return review
    }

    func send(_ review: SerialSendReview) {
        guard state == .connected, !sending, let transport, let record,
              pendingSendID == review.id, review.sessionID == record.id, review.port == record.port, review.settings == record.settings,
              review.isFresh(at: ProcessInfo.processInfo.systemUptime),
              !review.bytes.isEmpty, review.bytes.count <= SerialPayload.maximumBytes else {
            message = "Send review is stale or the session changed. Review the bytes again."; return
        }
        pendingSendID = nil; sending = true
        let id = record.id
        append(.status, note: "Approved send: \(review.bytes.count) bytes. No device acknowledgement is implied.")
        writer = Task { [weak self] in
            var offset = 0
            let deadline = ProcessInfo.processInfo.systemUptime + 2
            do {
                while offset < review.bytes.count {
                    try Task.checkCancellation()
                    guard ProcessInfo.processInfo.systemUptime < deadline else { throw SerialSessionError.invalid("Transmit timed out; \(offset)/\(review.bytes.count) bytes were accepted by the OS. No automatic retry.") }
                    let chunk = Array(review.bytes.dropFirst(offset))
                    // Nonblocking, at most 4 KiB. Account for accepted bytes in
                    // the same main-actor turn, before Disconnect can intervene.
                    let count = try transport.transmit(chunk[...])
                    guard let self, self.record?.id == id, self.state == .connected else { return }
                    if count > 0 { self.append(.tx, bytes: Array(chunk.prefix(count))); offset += count }
                    if offset < review.bytes.count { try await Task.sleep(for: .milliseconds(20)) }
                }
                guard let self, self.record?.id == id, self.state == .connected else { return }
                self.sending = false; self.message = "OS accepted \(offset) transmitted bytes. Verify the device response separately."
            } catch is CancellationError { }
            catch {
                guard let self, self.record?.id == id, self.state == .connected else { return }
                self.fail(error.localizedDescription)
            }
        }
    }

    private func startReader(_ port: SerialSessionTransport, sessionID: UUID) {
        reader = Task { [weak self] in
            do {
                while !Task.isCancelled {
                    // poll(timeout: 0) and one nonblocking read, at most 16 KiB.
                    let bytes = try port.receive()
                    guard let self, self.record?.id == sessionID, self.state == .connected else { return }
                    if !bytes.isEmpty { self.append(.rx, bytes: bytes) }
                    if self.elapsed >= 86_400 { self.disconnect(reason: "24-hour session limit reached. Reconnect explicitly."); return }
                    try await Task.sleep(for: .milliseconds(25))
                }
            } catch is CancellationError { }
            catch {
                guard let self, self.record?.id == sessionID, self.state == .connected else { return }
                self.fail(error.localizedDescription)
            }
        }
    }
    private var elapsed: Double { max(0, ProcessInfo.processInfo.systemUptime - startedUptime) }
    private func append(_ direction: SerialTimelineEvent.Direction, bytes: [UInt8] = [], note: String = "") {
        record?.append(direction, bytes: bytes, note: note, elapsed: elapsed)
    }
    private func fail(_ text: String) {
        reader?.cancel(); writer?.cancel()
        let warning = transport?.close(); transport = nil
        finish(text + (warning.map { " Cleanup warning: \($0)." } ?? ""), failed: true)
    }
    private func finish(_ text: String, failed: Bool) {
        message = text
        if sending { append(.status, note: "Send ended without automatic retry. Any accepted bytes may already have affected the device.") }
        append(.status, note: text); record?.finish()
        sending = false; state = failed ? .failed : .disconnected
        releaseLease?(); releaseLease = nil
    }
}
