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

struct SensorHistoryPoint: Identifiable, Equatable, Sendable {
    let id = UUID()
    let timestamp: Date
    let series: String
    let value: Double
}

private enum TimeCardRefreshOutcome: Sendable {
    case success(
        services: [TimeCardServiceDescriptor],
        selected: TimeCardServiceDescriptor,
        snapshot: TimeCardDeviceSnapshot,
        sensors: TimeCardSensorSnapshot?,
        sensorError: String?,
        i2cStatus: TimeCardI2CStatusSnapshot?,
        i2cMux: TimeCardI2CMuxSnapshot?,
        ledStates: [TimeCardLEDState],
        i2cError: String?
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
    @Published private(set) var commandInProgress = false
    @Published private(set) var commandMessage = ""
    @Published private(set) var smaRoutes: [TimeCardSMARoute] = []
    @Published private(set) var smaMessage = ""
    @Published private(set) var sensorTelemetry: TimeCardSensorSnapshot?
    @Published private(set) var sensorMessage = ""
    @Published private(set) var sensorHistory: [SensorHistoryPoint] = []
    @Published private(set) var i2cStatus: TimeCardI2CStatusSnapshot?
    @Published private(set) var i2cMux: TimeCardI2CMuxSnapshot?
    @Published private(set) var ledStates: [TimeCardLEDState] = []
    @Published private(set) var i2cMessage = ""
    @Published private(set) var i2cScanResults: [TimeCardI2CProbeResult] = []
    @Published private(set) var i2cTransfer: TimeCardI2CTransferSnapshot?
    @Published private(set) var i2cOperationInProgress = false
    @Published private(set) var i2cOperationMessage = ""

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
        smaRoutes.removeAll(keepingCapacity: true)
        sensorTelemetry = nil
        sensorHistory.removeAll(keepingCapacity: true)
        i2cStatus = nil
        i2cMux = nil
        ledStates.removeAll(keepingCapacity: true)
        i2cMessage = ""
        i2cScanResults.removeAll(keepingCapacity: true)
        i2cTransfer = nil
        i2cOperationMessage = ""
        refresh()
    }

    func refresh() {
        nextAutomaticAttempt = .distantPast
        performRefresh()
    }

    func setCardFromSystem() {
        guard !commandInProgress else { return }
        guard let selectedServiceID else {
            commandMessage = "No Time Card is selected."
            return
        }
        guard let descriptor = services.first(where: { $0.id == selectedServiceID }) else {
            commandMessage = "The selected Time Card is no longer available."
            return
        }

        commandInProgress = true
        commandMessage = "Setting Time Card from macOS system time..."

        Task { [weak self] in
            let result = await Task.detached(priority: .userInitiated) {
                do {
                    try TimeCardClient.setCardFromSystem(for: descriptor)
                    return Result<Void, Error>.success(())
                } catch {
                    return Result<Void, Error>.failure(error)
                }
            }.value

            guard let self else { return }
            self.commandInProgress = false
            switch result {
            case .success:
                self.commandMessage = "Time Card clock was set from macOS system time."
                self.refresh()
            case .failure(let error):
                self.commandMessage = "Set-time failed: \(error.localizedDescription)"
            }
        }
    }

    func refreshSMA() {
        guard let descriptor = selectedDescriptor else {
            smaMessage = "No Time Card is selected."
            return
        }
        guard snapshot?.capabilityNames.contains("SMA") == true else {
            smaRoutes = []
            smaMessage = "SMA routing is not advertised by this driver."
            return
        }

        Task { [weak self] in
            let result = await Task.detached(priority: .utility) {
                do {
                    return Result<[TimeCardSMARoute], Error>.success(
                        try TimeCardClient.querySMARoutes(for: descriptor)
                    )
                } catch {
                    return Result<[TimeCardSMARoute], Error>.failure(error)
                }
            }.value

            guard let self else { return }
            switch result {
            case .success(let routes):
                self.smaRoutes = routes
                self.smaMessage = "SMA connector states refreshed."
            case .failure(let error):
                self.smaMessage = "SMA refresh failed: \(error.localizedDescription)"
            }
        }
    }

    func setSMARoute(
        connector: UInt32,
        direction: TimeCardSMADirection,
        function: UInt32
    ) {
        guard !commandInProgress else { return }
        guard let descriptor = selectedDescriptor else {
            smaMessage = "No Time Card is selected."
            return
        }

        commandInProgress = true
        smaMessage = "Applying SMA \(connector) route..."

        Task { [weak self] in
            let result = await Task.detached(priority: .userInitiated) {
                do {
                    return Result<TimeCardSMARoute, Error>.success(
                        try TimeCardClient.setSMARoute(
                            for: descriptor,
                            connector: connector,
                            direction: direction,
                            function: function
                        )
                    )
                } catch {
                    return Result<TimeCardSMARoute, Error>.failure(error)
                }
            }.value

            guard let self else { return }
            self.commandInProgress = false
            switch result {
            case .success(let route):
                var routes = self.smaRoutes
                if let index = routes.firstIndex(where: { $0.connector == route.connector }) {
                    routes[index] = route
                } else {
                    routes.append(route)
                    routes.sort { $0.connector < $1.connector }
                }
                self.smaRoutes = routes
                self.smaMessage = "SMA \(route.connector) route applied and verified."
                self.refresh()
            case .failure(let error):
                self.smaMessage = "SMA apply failed: \(error.localizedDescription)"
            }
        }
    }

    func scanI2CBus() {
        guard !i2cOperationInProgress else { return }
        guard let descriptor = selectedDescriptor else {
            i2cOperationMessage = "No Time Card is selected."
            return
        }
        guard snapshot?.capabilityNames.contains("I2C") == true else {
            i2cScanResults = []
            i2cOperationMessage = "I2C is not advertised by this driver."
            return
        }

        i2cOperationInProgress = true
        i2cTransfer = nil
        i2cOperationMessage = "Scanning I2C addresses 0x08 through 0x77..."

        Task { [weak self] in
            let result = await Task.detached(priority: .userInitiated) {
                do {
                    return Result<[TimeCardI2CProbeResult], Error>.success(
                        try TimeCardClient.scanI2CBus(for: descriptor)
                    )
                } catch {
                    return Result<[TimeCardI2CProbeResult], Error>.failure(error)
                }
            }.value

            guard let self else { return }
            self.i2cOperationInProgress = false
            switch result {
            case .success(let results):
                self.i2cScanResults = results
                let present = results.filter(\.isPresent)
                if present.isEmpty {
                    self.i2cOperationMessage = "I2C scan completed: no devices responded."
                } else {
                    let addresses = present
                        .map(\.addressText)
                        .joined(separator: ", ")
                    self.i2cOperationMessage =
                        "I2C scan completed: \(present.count) device(s) responded at \(addresses)."
                }
                self.refresh()
            case .failure(let error):
                self.i2cOperationMessage =
                    "I2C scan failed: \(error.localizedDescription)"
            }
        }
    }

    func setI2CMux(channelMask: UInt32) {
        guard !i2cOperationInProgress else { return }
        guard let descriptor = selectedDescriptor else {
            i2cOperationMessage = "No Time Card is selected."
            return
        }
        guard snapshot?.capabilityNames.contains("I2C") == true else {
            i2cOperationMessage = "I2C is not advertised by this driver."
            return
        }

        i2cOperationInProgress = true
        i2cOperationMessage = String(
            format: "Setting I2C mux channel mask to 0x%02x...",
            channelMask & 0xff
        )

        Task { [weak self] in
            let result = await Task.detached(priority: .userInitiated) {
                do {
                    return Result<TimeCardI2CMuxSnapshot, Error>.success(
                        try TimeCardClient.setI2CMux(
                            for: descriptor,
                            channelMask: channelMask
                        )
                    )
                } catch {
                    return Result<TimeCardI2CMuxSnapshot, Error>.failure(error)
                }
            }.value

            guard let self else { return }
            self.i2cOperationInProgress = false
            switch result {
            case .success(let mux):
                self.i2cMux = mux
                self.i2cScanResults.removeAll(keepingCapacity: true)
                self.i2cTransfer = nil
                self.i2cOperationMessage = String(
                    format: "I2C mux set and verified at 0x%02x.",
                    mux.channelMask & 0xff
                )
                self.refresh()
            case .failure(let error):
                self.i2cOperationMessage =
                    "I2C mux set failed: \(error.localizedDescription)"
            }
        }
    }

    func readI2C(
        address: UInt32,
        subaddress: UInt32,
        subaddressLength: UInt32,
        length: UInt32
    ) {
        guard !i2cOperationInProgress else { return }
        guard let descriptor = selectedDescriptor else {
            i2cOperationMessage = "No Time Card is selected."
            return
        }
        guard snapshot?.capabilityNames.contains("I2C") == true else {
            i2cOperationMessage = "I2C is not advertised by this driver."
            return
        }

        i2cOperationInProgress = true
        i2cOperationMessage = String(
            format: "Reading %u byte(s) from I2C 0x%02x...",
            length,
            address & 0xff
        )

        Task { [weak self] in
            let result = await Task.detached(priority: .userInitiated) {
                do {
                    return Result<TimeCardI2CTransferSnapshot, Error>.success(
                        try TimeCardClient.readI2C(
                            for: descriptor,
                            address: address,
                            subaddress: subaddress,
                            subaddressLength: subaddressLength,
                            length: length
                        )
                    )
                } catch {
                    return Result<TimeCardI2CTransferSnapshot, Error>.failure(error)
                }
            }.value

            guard let self else { return }
            self.i2cOperationInProgress = false
            switch result {
            case .success(let transfer):
                self.i2cTransfer = transfer
                self.i2cOperationMessage = String(
                    format: "Read %u byte(s) from %@, controller 0x%02x.",
                    transfer.length,
                    transfer.addressText,
                    transfer.controllerStatus & 0xff
                )
                self.refresh()
            case .failure(let error):
                self.i2cOperationMessage =
                    "I2C read failed: \(error.localizedDescription)"
            }
        }
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
                        var sensors: TimeCardSensorSnapshot?
                        var sensorError: String?
                        var i2cStatus: TimeCardI2CStatusSnapshot?
                        var i2cMux: TimeCardI2CMuxSnapshot?
                        var ledStates: [TimeCardLEDState] = []
                        var i2cErrors: [String] = []
                        if snapshot.capabilityNames.contains("Sensors") {
                            do {
                                sensors = try TimeCardClient.querySensors(
                                    for: descriptor
                                )
                            } catch {
                                sensorError = error.localizedDescription
                            }
                        }
                        if snapshot.capabilityNames.contains("I2C") {
                            do {
                                i2cStatus = try TimeCardClient.queryI2CStatus(
                                    for: descriptor
                                )
                                i2cMux = try TimeCardClient.queryI2CMux(
                                    for: descriptor
                                )
                            } catch {
                                i2cErrors.append(error.localizedDescription)
                            }
                        }
                        if snapshot.capabilityNames.contains("LEDs") {
                            do {
                                ledStates = try TimeCardClient.queryLEDStates(
                                    for: descriptor
                                )
                            } catch {
                                i2cErrors.append(error.localizedDescription)
                            }
                        }
                        return .success(
                            services: discovered,
                            selected: descriptor,
                            snapshot: snapshot,
                            sensors: sensors,
                            sensorError: sensorError,
                            i2cStatus: i2cStatus,
                            i2cMux: i2cMux,
                            ledStates: ledStates,
                            i2cError: i2cErrors.isEmpty
                                ? nil : i2cErrors.joined(separator: "; ")
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
        case .success(
            let discovered, let selected, let current, let sensors,
            let sensorError, let currentI2CStatus, let currentI2CMux,
            let currentLEDStates, let i2cError
        ):
            services = discovered
            updateSelectedService(selected)
            snapshot = current
            state = .connected
            errorMessage = ""
            recoverySuggestion = ""
            lastUpdated = Date()
            appendSamplingWindow(current.sampleWindowNanoseconds)
            sensorTelemetry = sensors
            if let sensors {
                sensorMessage = sensors.validReadings.isEmpty
                    ? "Sensor branch responded, but no valid samples were returned."
                    : "\(sensors.validReadings.count) live sensor block(s)."
                appendSensorHistory(sensors)
            } else if let sensorError {
                sensorMessage = "Sensor refresh failed: \(sensorError)"
            } else if current.capabilityNames.contains("Sensors") {
                sensorMessage = "Sensor branch is advertised but has not sampled yet."
            } else {
                sensorMessage = "Sensor branch is not advertised by this profile."
                sensorHistory.removeAll(keepingCapacity: true)
            }
            i2cStatus = currentI2CStatus
            i2cMux = currentI2CMux
            ledStates = currentLEDStates
            var i2cMessages: [String] = []
            if let currentI2CStatus {
                let devices = currentI2CStatus.knownDeviceNames.isEmpty
                    ? "no known devices"
                    : currentI2CStatus.knownDeviceNames.joined(separator: ", ")
                i2cMessages.append("I2C controller refreshed, \(devices)")
            }
            if let currentI2CMux {
                let muxState = currentI2CMux.isPresent ? "live" : "not present"
                i2cMessages.append(
                    String(
                        format: "mux %@ at 0x%02x",
                        muxState,
                        currentI2CMux.channelMask & 0xff
                    )
                )
            }
            if !currentLEDStates.isEmpty {
                let fitted = currentLEDStates.filter(\.isPresent).count
                i2cMessages.append(
                    "\(fitted)/\(currentLEDStates.count) LED state(s) live"
                )
            }
            if let i2cError {
                i2cMessages.append("partial refresh issue: \(i2cError)")
            }
            if !i2cMessages.isEmpty {
                i2cMessage = i2cMessages.joined(separator: "; ") + "."
            } else if let i2cError {
                i2cMessage = "I2C/LED refresh failed: \(i2cError)"
            } else if current.capabilityNames.contains("I2C") ||
                current.capabilityNames.contains("LEDs") {
                i2cMessage = "I2C or LED capability is advertised but has not sampled yet."
            } else {
                i2cMessage = "I2C and LED control are not advertised by this profile."
                ledStates.removeAll(keepingCapacity: true)
            }
            if current.capabilityNames.contains("SMA") && smaRoutes.isEmpty {
                refreshSMA()
            }
            nextAutomaticAttempt = .distantPast

        case .failure(let discovered, let selected, let error):
            services = discovered
            updateSelectedService(selected)
            snapshot = nil
            sensorTelemetry = nil
            i2cStatus = nil
            i2cMux = nil
            ledStates.removeAll(keepingCapacity: true)
            i2cScanResults.removeAll(keepingCapacity: true)
            i2cTransfer = nil
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
            sensorTelemetry = nil
            i2cStatus = nil
            i2cMux = nil
            ledStates.removeAll(keepingCapacity: true)
            i2cScanResults.removeAll(keepingCapacity: true)
            i2cTransfer = nil
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
            sensorHistory.removeAll(keepingCapacity: true)
            i2cScanResults.removeAll(keepingCapacity: true)
            i2cTransfer = nil
        }
    }

    private var selectedDescriptor: TimeCardServiceDescriptor? {
        guard let selectedServiceID else { return nil }
        return services.first { $0.id == selectedServiceID }
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

    private func appendSensorHistory(_ telemetry: TimeCardSensorSnapshot) {
        let timestamp = Date()
        for reading in telemetry.readings where reading.isValid {
            guard let temperature = reading.temperatureCelsius else {
                continue
            }
            let series = "\(reading.kind.label) 0x" +
                String(format: "%02x", reading.address)
            sensorHistory.append(
                SensorHistoryPoint(
                    timestamp: timestamp,
                    series: series,
                    value: temperature
                )
            )
        }
        if sensorHistory.count > historyCapacity * 5 {
            sensorHistory.removeFirst(
                sensorHistory.count - historyCapacity * 5
            )
        }
    }
}
