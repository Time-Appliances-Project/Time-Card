/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

@MainActor final class DeviceLabMonitor: ObservableObject {
    @Published private(set) var atomic: SA53Snapshot?
    @Published private(set) var atomicHistory: [SA53Snapshot] = []
    @Published private(set) var atomicBusy = false
    @Published private(set) var atomicMessage = "Refresh to identify the oscillator on the MAC UART."
    @Published private(set) var motionSamples: [MotionSample] = []
    @Published private(set) var motionRunning = false
    @Published private(set) var motionBusy = false
    @Published private(set) var motionMessage = "Start motion to enable volatile sensor reports."
    @Published private(set) var supportsMotion = false
    @Published private(set) var supportsAtomic = false
    private var descriptor: TimeCardServiceDescriptor?
    private weak var monitor: TimeCardMonitor?
    private var motionTask: Task<Void, Never>?
    private var generation: UInt64 = 0

    func bind(_ monitor: TimeCardMonitor) {
        self.monitor = monitor
        let selected = monitor.state == .connected ? monitor.services.first { $0.id == monitor.selectedServiceID } : nil
        if selected?.id != descriptor?.id {
            stopMotion()
            generation &+= 1
            motionBusy = false
            atomic = nil; atomicHistory.removeAll(); motionSamples.removeAll()
            atomicMessage = "Refresh to identify the selected card's oscillator."
        }
        descriptor = selected
        supportsAtomic = selected != nil && [1, 2].contains(monitor.snapshot?.boardProfile ?? 0) && (monitor.snapshot?.abiVersion ?? 0) >= 9 && (monitor.snapshot?.capabilities ?? 0) & (1 << 8) != 0
        supportsMotion = selected != nil && (monitor.snapshot?.abiVersion ?? 0) >= 11 && (monitor.snapshot?.capabilities ?? 0) & (1 << 11) != 0
    }
    func refreshAtomic() { performAtomic("SA53 telemetry refreshed") { try $0.refresh() } }
    func setAtomic(_ parameter: String, value: Int64) {
        guard let baseline = atomic else { return }
        performAtomic(parameter + " operation completed; inspect readback") { try $0.set(parameter, value: value, baseline: baseline) }
    }
    func atomicAction(_ action: SA53Client.Action) {
        guard let baseline = atomic else { return }
        performAtomic(action.rawValue + " acknowledged; inspect refreshed telemetry") { try $0.action(action, baseline: baseline) }
    }
    private func performAtomic(_ description: String, operation: @escaping @Sendable (SA53Client) throws -> SA53Snapshot) {
        guard !atomicBusy, supportsAtomic, let descriptor, let monitor,
              monitor.beginPeripheralOperation() else {
            atomicMessage = "Connect a supported card and wait for other operations or captures to finish."
            return
        }
        atomicBusy = true; atomicMessage = "Communicating with SA53..."
        let generation = generation
        Task {
            let result = await Task.detached(priority: .userInitiated) { Result { try operation(SA53Client(descriptor: descriptor)) } }.value
            atomicBusy = false
            var failed = false
            if generation == self.generation {
                switch result {
                case .success(let snapshot):
                    atomic = snapshot; atomicHistory.append(snapshot)
                    if atomicHistory.count > 120 { atomicHistory.removeFirst(atomicHistory.count - 120) }
                    atomicMessage = description + (snapshot.warnings.isEmpty ? "." : ". Some telemetry is unavailable; see warnings.")
                case .failure(let error): atomic = nil; atomicMessage = error.localizedDescription; failed = true
                }
            } else { atomicMessage = "Card changed during SA53 operation. Refresh before another action."; failed = true }
            monitor.endPeripheralOperation(atomicMessage, failed: failed)
        }
    }

    func startMotion() {
        guard !motionRunning, !motionBusy, supportsMotion, let descriptor, let monitor,
              monitor.beginPeripheralOperation() else {
            motionMessage = "Live motion needs active driver 26 / ABI v11. Wait for other operations before starting."
            return
        }
        motionBusy = true; motionSamples.removeAll(); motionMessage = "Starting sensor fusion reports..."
        let generation = generation
        motionTask = Task {
            let started = await Task.detached { Result { try TimeCardClient.queryIMU(for: descriptor, mode: 1) } }.value
            guard generation == self.generation, !Task.isCancelled else {
                monitor.endPeripheralOperation("Motion start interrupted by card change.", failed: true)
                return
            }
            motionBusy = false
            switch started {
            case .failure(let error):
                motionMessage = error.localizedDescription
                monitor.endPeripheralOperation(motionMessage, failed: true)
                return
            case .success(let sample):
                acceptMotion(sample)
                motionRunning = true
                monitor.endPeripheralOperation("Motion reports enabled. No Time Card clock settings changed.", failed: false)
            }
            var lastData = ProcessInfo.processInfo.systemUptime
            var lastRetry = lastData
            while !Task.isCancelled && generation == self.generation {
                do { try await Task.sleep(for: .milliseconds(250)) } catch { break }
                let now = ProcessInfo.processInfo.systemUptime
                let retry = now - lastData > 2 && now - lastRetry > 5
                if retry { lastRetry = now }
                let result = await Task.detached { Result { try TimeCardClient.queryIMU(for: descriptor, mode: retry ? 1 : 0) } }.value
                guard !Task.isCancelled, generation == self.generation else { break }
                switch result {
                case .success(let sample):
                    acceptMotion(sample)
                    if sample.reportCount > 0 { lastData = now }
                case .failure(let error):
                    motionMessage = error.localizedDescription
                    if now - lastData > 10 { stopMotion(); return }
                }
            }
        }
    }
    private func acceptMotion(_ sample: MotionSample) {
        if sample.flags & (1 << 11) != 0 { motionSamples.removeAll() }
        if sample.reportCount > 0 {
            motionSamples.append(sample)
            if motionSamples.count > 300 { motionSamples.removeFirst(motionSamples.count - 300) }
        }
        let partial = sample.flags & (1 << 9) != 0
        motionMessage = sample.reportCount > 0
            ? "\(sample.sensorName): \(sample.reportCount) fresh reports. " + (partial ? "An incomplete packet was discarded." : "Mux restored.")
            : "Waiting for fresh sensor reports. Missing axes are not shown as zero."
    }
    func stopMotion() {
        let wasActive = motionRunning || motionBusy || motionTask != nil
        motionTask?.cancel(); motionTask = nil; motionRunning = false
        guard wasActive, let descriptor else { return }
        motionBusy = true; motionMessage = "Stopping motion reports..."
        let generation = generation
        Task {
            let result = await Task.detached { Result { try TimeCardClient.queryIMU(for: descriptor, mode: 2) } }.value
            guard generation == self.generation else { return }
            motionBusy = false
            if case .failure(let error) = result { motionMessage = "Host sampling stopped; sensor stop was not confirmed: " + error.localizedDescription }
            else { motionMessage = "Stopped. BNO08x reports disabled; BNO055 fusion mode is retained. History remains available for export." }
        }
    }
}
