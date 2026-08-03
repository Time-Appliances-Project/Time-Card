/* SPDX-License-Identifier: BSD-3-Clause */

import Foundation
import IOKit

enum TimeCardMonitorState: Equatable, Sendable {
    case discovering
    case noService
    case connected
    case accessUnavailable
    case failed
}

struct SamplingWindowPoint: Identifiable, Equatable, Sendable {
    let id = UUID()
    let timestamp: Date
    let nanoseconds: Double
}

private enum TimeCardRefreshOutcome: Sendable {
    case success(
        services: [TimeCardServiceDescriptor],
        selected: TimeCardServiceDescriptor,
        snapshot: TimeCardDeviceSnapshot
    )
    case failure(
        services: [TimeCardServiceDescriptor],
        selected: TimeCardServiceDescriptor?,
        error: TimeCardClientError
    )
    case unexpected(String)
}

@MainActor
final class TimeCardMonitor: ObservableObject {
    @Published private(set) var state: TimeCardMonitorState = .discovering
    @Published private(set) var services: [TimeCardServiceDescriptor] = []
    @Published private(set) var selectedServiceID: UInt64?
    @Published private(set) var snapshot: TimeCardDeviceSnapshot?
    @Published private(set) var errorMessage = ""
    @Published private(set) var recoverySuggestion = ""
    @Published private(set) var lastUpdated: Date?
    @Published private(set) var samplingWindowHistory: [SamplingWindowPoint] = []

    private var refreshTimer: Timer?
    private var nextAutomaticAttempt = Date.distantPast
    private var selectionGeneration: UInt64 = 0
    private var refreshInProgress = false
    private var manualRefreshPending = false
    private let historyCapacity = 120

    init() {
        refresh()
        refreshTimer = Timer.scheduledTimer(
            withTimeInterval: 1.0, repeats: true
        ) { [weak self] _ in
            Task { @MainActor in
                self?.automaticRefresh()
            }
        }
    }

    func selectService(_ serviceID: UInt64) {
        guard services.contains(where: { $0.id == serviceID }),
              selectedServiceID != serviceID else {
            return
        }
        selectedServiceID = serviceID
        selectionGeneration &+= 1
        snapshot = nil
        samplingWindowHistory.removeAll(keepingCapacity: true)
        refresh()
    }

    func refresh() {
        nextAutomaticAttempt = .distantPast
        performRefresh()
    }

    private func automaticRefresh() {
        guard Date() >= nextAutomaticAttempt else { return }
        startRefresh(queueIfBusy: false)
    }

    private func performRefresh() {
        startRefresh(queueIfBusy: true)
    }

    private func startRefresh(queueIfBusy: Bool) {
        if refreshInProgress {
            if queueIfBusy {
                manualRefreshPending = true
            }
            return
        }

        refreshInProgress = true
        let requestedServiceID = selectedServiceID
        let generation = selectionGeneration

        Task { [weak self] in
            let outcome = await Task.detached(
                priority: .utility
            ) { () -> TimeCardRefreshOutcome in
                do {
                    let discovered = try TimeCardClient.discoverServices()
                    guard !discovered.isEmpty else {
                        return .failure(
                            services: [],
                            selected: nil,
                            error: .serviceNotFound
                        )
                    }

                    let descriptor = discovered.first {
                        $0.id == requestedServiceID
                    } ?? discovered[0]
                    do {
                        let snapshot = try TimeCardClient.readSnapshot(
                            for: descriptor
                        )
                        return .success(
                            services: discovered,
                            selected: descriptor,
                            snapshot: snapshot
                        )
                    } catch let error as TimeCardClientError {
                        return .failure(
                            services: discovered,
                            selected: descriptor,
                            error: error
                        )
                    }
                } catch let error as TimeCardClientError {
                    return .failure(
                        services: [], selected: nil, error: error
                    )
                } catch {
                    return .unexpected(error.localizedDescription)
                }
            }.value

            guard let self else { return }
            self.finishRefresh(outcome, generation: generation)
        }
    }

    private func finishRefresh(
        _ outcome: TimeCardRefreshOutcome,
        generation: UInt64
    ) {
        refreshInProgress = false
        if generation == selectionGeneration {
            apply(outcome)
        }

        if manualRefreshPending {
            manualRefreshPending = false
            refresh()
        }
    }

    private func apply(_ outcome: TimeCardRefreshOutcome) {
        switch outcome {
        case .success(let discovered, let selected, let current):
            services = discovered
            updateSelectedService(selected)
            snapshot = current
            state = .connected
            errorMessage = ""
            recoverySuggestion = ""
            lastUpdated = Date()
            appendSamplingWindow(current.sampleWindowNanoseconds)
            nextAutomaticAttempt = .distantPast

        case .failure(let discovered, let selected, let error):
            services = discovered
            updateSelectedService(selected)
            snapshot = nil
            errorMessage = error.localizedDescription
            recoverySuggestion = error.recoverySuggestion ?? ""
            switch error {
            case .serviceNotFound, .serviceDisappeared:
                state = .noService
                nextAutomaticAttempt = Date().addingTimeInterval(2)
            case .openFailed(let result)
                where result == kIOReturnNotPrivileged
                    || result == kIOReturnNotPermitted:
                state = .accessUnavailable
                nextAutomaticAttempt = Date().addingTimeInterval(30)
            default:
                state = .failed
                nextAutomaticAttempt = Date().addingTimeInterval(5)
            }

        case .unexpected(let message):
            snapshot = nil
            state = .failed
            errorMessage = message
            recoverySuggestion = ""
            nextAutomaticAttempt = Date().addingTimeInterval(5)
        }
    }

    private func updateSelectedService(
        _ descriptor: TimeCardServiceDescriptor?
    ) {
        let newServiceID = descriptor?.id
        if selectedServiceID != newServiceID {
            selectedServiceID = newServiceID
            samplingWindowHistory.removeAll(keepingCapacity: true)
        }
    }

    var serviceDetected: Bool {
        !services.isEmpty
    }

    private func appendSamplingWindow(_ nanoseconds: UInt64) {
        samplingWindowHistory.append(
            SamplingWindowPoint(
                timestamp: Date(), nanoseconds: Double(nanoseconds)
            )
        )
        if samplingWindowHistory.count > historyCapacity {
            samplingWindowHistory.removeFirst(
                samplingWindowHistory.count - historyCapacity
            )
        }
    }
}
