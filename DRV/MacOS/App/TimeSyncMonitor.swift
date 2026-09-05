/* SPDX-License-Identifier: BSD-3-Clause */
import Darwin
import Foundation

enum TimeSyncMonotonic {
    static func now() -> Double {
        var scale = mach_timebase_info_data_t(); mach_timebase_info(&scale)
        return Double(mach_continuous_time()) * Double(scale.numer) / Double(scale.denom) / 1_000_000_000
    }
}

struct TimeSyncEvent: Identifiable, Codable, Sendable {
    let id: UUID
    let receivedAt: Date
    let message: String
    let checksumValid: Bool
    init(_ frame: TimeCardUBXFrame) {
        id = UUID(); receivedAt = Date(); message = frame.messageText + ": " + frame.summary; checksumValid = frame.checksumValid
    }
}
private struct ReceivedTimeFrame: Sendable { let frame: TimeCardUBXFrame; let monotonic: Double }

@MainActor final class TimeSyncMonitor: ObservableObject {
    @Published private(set) var running = false
    @Published private(set) var stopping = false
    @Published private(set) var message = "Start a live GNSS session to qualify receiver time. No clocks will be changed."
    @Published private(set) var assessment = ReceiverTimeEvidence().assessment(at: 0)
    @Published private(set) var events: [TimeSyncEvent] = []
    @Published private(set) var receivedBytes = 0
    @Published private(set) var transmittedBytes = 0
    @Published private(set) var badFrames = 0
    @Published private(set) var discardedBytes = 0
    @Published private(set) var operatingSystemTimeService = "Unknown; clock ownership has not been granted."
    private var evidence = ReceiverTimeEvidence()
    private var descriptor: TimeCardServiceDescriptor?
    private weak var monitor: TimeCardMonitor?
    private var worker: Task<Void, Never>?
    private var generation: UInt64 = 0
    private var serviceCheckInProgress = false
    deinit { worker?.cancel() }

    func bind(_ monitor: TimeCardMonitor) {
        self.monitor = monitor
        let selected = monitor.state == .connected ? monitor.services.first { $0.id == monitor.selectedServiceID } : nil
        if selected?.id != descriptor?.id {
            stop(); generation &+= 1; evidence = ReceiverTimeEvidence()
            assessment = evidence.assessment(at: TimeSyncMonotonic.now()); events = []
            receivedBytes = 0; transmittedBytes = 0; badFrames = 0; discardedBytes = 0
        }
        descriptor = selected
    }

    func start(port: TimeCardUARTPort, baud: UInt32, poll: Bool) {
        guard !running, !stopping, let descriptor, let monitor,
              [.gnss, .gnss2].contains(port), monitor.snapshot?.supportsUARTWrite == true,
              monitor.beginTimeReferenceSession() else {
            message = "Connect a supported card and finish other hardware operations first."; return
        }
        generation &+= 1; let token = generation
        evidence = ReceiverTimeEvidence(); assessment = evidence.assessment(at: TimeSyncMonotonic.now())
        events = []; receivedBytes = 0; transmittedBytes = 0; badFrames = 0; discardedBytes = 0
        running = true; message = "Opening \(port.label) at \(baud) baud. Close other card-control programs."
        worker = Task { [weak self] in
            let job = Task.detached(priority: .utility) { [weak self] () -> String? in
                do {
                    try TimeCardClient.configureUART(for: descriptor, port: port, baudRate: baud)
                    var framer = LiveUBXFramer()
                    let started = TimeSyncMonotonic.now()
                    var nextPoll = started, nextPublish = started
                    var buffered: [ReceivedTimeFrame] = [], rx = 0, tx = 0
                    let queries = TimeCardUBXPoll.allCases.filter { ["MON-VER", "NAV-TIMEUTC", "NAV-TIMEGPS", "NAV-TIMELS"].contains($0.label) }
                    while !Task.isCancelled && TimeSyncMonotonic.now() - started < 86_400 {
                        let now = TimeSyncMonotonic.now()
                        if poll && now >= nextPoll {
                            for query in queries where query.label != "MON-VER" || tx == 0 {
                                if Task.isCancelled { break }
                                let sent = try TimeCardClient.writeUART(for: descriptor, port: port, bytes: query.packet, timeoutMilliseconds: 100)
                                guard sent.byteCount == query.packet.count else { throw TimeTrustError.invalid("Incomplete GNSS poll write") }
                                tx += sent.byteCount
                            }
                            nextPoll = now + 1
                        }
                        let read = try TimeCardClient.readUART(for: descriptor, port: port, maximumBytes: 256, timeoutMilliseconds: 100)
                        guard read.lineStatus & 0x9e == 0 else { throw TimeTrustError.invalid("UART line/FIFO errors invalidate time evidence.") }
                        rx += read.data.count
                        let frames = framer.feed(read.data)
                        // Delivery is bounded and keeps reception times close to complete frames.
                        let completed = TimeSyncMonotonic.now()
                        buffered.append(contentsOf: frames.map { ReceivedTimeFrame(frame: $0, monotonic: completed) })
                        if completed >= nextPublish || buffered.count >= 32 {
                            await self?.accept(buffered, at: completed, rx: rx, tx: tx, discarded: framer.discardedBytes, token: token)
                            buffered.removeAll(keepingCapacity: true); nextPublish = completed + 0.25
                        }
                    }
                    return nil
                } catch { return error.localizedDescription }
            }
            let failure = await withTaskCancellationHandler(operation: { await job.value }, onCancel: { job.cancel() })
            monitor.endTimeReferenceSession()
            guard let self else { return }
            self.running = false; self.stopping = false; self.worker = nil
            self.evidence = ReceiverTimeEvidence(); self.assessment = self.evidence.assessment(at: TimeSyncMonotonic.now())
            self.message = failure.map { "Session stopped: " + $0 } ?? "Session stopped; time evidence invalidated. No PHC, oscillator, receiver-persistent, or macOS clock settings changed."
        }
    }

    func stop() {
        guard running else { return }
        stopping = true
        // Revoke evidence immediately, before waiting for a bounded UART call.
        evidence = ReceiverTimeEvidence()
        assessment = evidence.assessment(at: TimeSyncMonotonic.now())
        worker?.cancel()
    }

    private func accept(_ frames: [ReceivedTimeFrame], at now: Double, rx: Int, tx: Int, discarded: Int, token: UInt64) {
        guard token == generation, running, !stopping else { return }
        for entry in frames {
            let frame = entry.frame
            evidence.ingest(frame, at: entry.monotonic)
            if !frame.checksumValid { badFrames += 1 }
            events.append(TimeSyncEvent(frame))
        }
        if events.count > 200 { events.removeFirst(events.count - 200) }
        receivedBytes = rx; transmittedBytes = tx; discardedBytes = discarded
        assessment = evidence.assessment(at: now)
        message = rx == 0 ? "No receiver bytes yet. Check the selected UART, baud rate, receiver power, and card routing."
            : assessment.qualified ? "Receiver UTC/GPS/leap data agree. UART arrival is not a precision time transfer or PHC epoch association."
            : "Receiving data; reference qualification is blocked. See the reasons below."
    }

    func refreshTimeServiceStatus() {
        guard !serviceCheckInProgress else { return }
        serviceCheckInProgress = true
        Task {
            let status = await Task.detached(priority: .utility) {
                let task = Process(); let pipe = Pipe()
                task.executableURL = URL(fileURLWithPath: "/bin/launchctl")
                task.arguments = ["print", "system/com.apple.timed"]
                task.standardOutput = pipe; task.standardError = pipe
                do {
                    try task.run()
                    let bytes = pipe.fileHandleForReading.readDataToEndOfFile()
                    task.waitUntilExit()
                    let text = String(decoding: bytes.prefix(65_536), as: UTF8.self)
                    return task.terminationStatus == 0 && text.contains("state = running")
                        ? "Apple timed is running. This is a potential clock-owner conflict; no ownership has been transferred."
                        : "Apple timed ownership is unknown. Verify Network Time and other time daemons before granting control."
                } catch { return "Time-service check failed: " + error.localizedDescription }
            }.value
            operatingSystemTimeService = status; serviceCheckInProgress = false
        }
    }

    func export(snapshot: TimeCardDeviceSnapshot?) throws -> Data {
        struct Record: Encodable {
            let schemaVersion = 1
            let exportedAt = Date()
            let serviceID: String?
            let driverABI: UInt32?
            let sessionRunning: Bool
            let receiver: ReceiverTimeAssessment
            let receivedBytes: Int
            let transmittedPollBytes: Int
            let badFrames: Int
            let discardedBytes: Int
            let systemTimeService: String
            let phcEpochAssociated = false
            let systemClockWriterInstalled = false
            let clockWrites = 0
            let events: [TimeSyncEvent]
        }
        let record = Record(serviceID: descriptor.map { String($0.id) }, driverABI: snapshot?.abiVersion, sessionRunning: running && !stopping,
            receiver: evidence.assessment(at: TimeSyncMonotonic.now()), receivedBytes: receivedBytes, transmittedPollBytes: transmittedBytes,
            badFrames: badFrames, discardedBytes: discardedBytes, systemTimeService: operatingSystemTimeService, events: events)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]; encoder.dateEncodingStrategy = .iso8601
        return try encoder.encode(record)
    }
}
