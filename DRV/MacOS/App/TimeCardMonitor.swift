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

enum TimeCardSelfTestSeverity: String, Equatable, Sendable {
    case pass = "Pass"
    case warning = "Warning"
    case fail = "Fail"
    case gated = "Gated"
    case waiting = "Waiting"
}

struct TimeCardSelfTestItem: Identifiable, Equatable, Sendable {
    let id: String
    let name: String
    let severity: TimeCardSelfTestSeverity
    let detail: String

    init(
        _ name: String,
        severity: TimeCardSelfTestSeverity,
        detail: String
    ) {
        self.id = name
        self.name = name
        self.severity = severity
        self.detail = detail
    }

    var state: String {
        severity.rawValue
    }
}

struct TimeCardSelfTestReport: Equatable, Sendable {
    let runAt: Date
    let items: [TimeCardSelfTestItem]

    var passCount: Int {
        items.filter { $0.severity == .pass }.count
    }

    var warningCount: Int {
        items.filter { $0.severity == .warning }.count
    }

    var failCount: Int {
        items.filter { $0.severity == .fail }.count
    }

    var gatedCount: Int {
        items.filter { $0.severity == .gated }.count
    }

    var attentionCount: Int {
        warningCount + failCount
    }

    var overallState: String {
        if failCount > 0 { return "Fail" }
        if warningCount > 0 { return "Warning" }
        if passCount > 0 { return "Pass" }
        return "Waiting"
    }

    var summary: String {
        if failCount > 0 {
            return "\(failCount) failed check(s), \(warningCount) warning(s)"
        }
        if warningCount > 0 {
            return "\(warningCount) warning(s), \(passCount) passed check(s)"
        }
        if gatedCount > 0 {
            return "\(passCount) passed, \(gatedCount) gated by FPGA or ABI"
        }
        return "\(passCount) passed check(s)"
    }
}

private struct TimeCardLEDPlan: Sendable {
    let led: TimeCardLEDKind
    let red: UInt32
    let green: UInt32
    let blue: UInt32
    let globalCurrent: UInt32
    let optional: Bool
}

private struct TimeCardLEDPolicyResult: Sendable {
    let states: [TimeCardLEDState]
    let routes: [TimeCardSMARoute]
}

private enum TimeCardSelfTestOutcome: Sendable {
    case success(
        snapshot: TimeCardDeviceSnapshot,
        sensors: TimeCardSensorSnapshot?,
        sensorError: String?,
        routes: [TimeCardSMARoute],
        smaError: String?,
        i2cStatus: TimeCardI2CStatusSnapshot?,
        i2cMux: TimeCardI2CMuxSnapshot?,
        ledStates: [TimeCardLEDState],
        i2cError: String?,
        report: TimeCardSelfTestReport
    )
    case failure(report: TimeCardSelfTestReport, message: String)
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
    @Published private(set) var selfTestReport: TimeCardSelfTestReport?
    @Published private(set) var selfTestInProgress = false
    @Published private(set) var selfTestMessage = ""
    @Published private(set) var serialPorts: [TimeCardSerialPort] = []
    @Published private(set) var serialRefreshInProgress = false
    @Published private(set) var serialMessage = ""
    @Published private(set) var serialCapture: TimeCardSerialCapture?
    @Published private(set) var serialCaptureInProgress = false

    private var refreshTimer: Timer?
    private var nextAutomaticAttempt = Date.distantPast
    private var selectionGeneration: UInt64 = 0
    private var refreshInProgress = false
    private var manualRefreshPending = false
    private let historyCapacity = 120

    init() {
        refresh()
        refreshSerialPorts()
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
        selfTestReport = nil
        selfTestMessage = ""
        refresh()
    }

    func refreshSerialPorts() {
        guard !serialRefreshInProgress else { return }
        serialRefreshInProgress = true
        serialMessage = "Refreshing macOS serial ports..."

        Task { [weak self] in
            let ports = await Task.detached(priority: .utility) {
                TimeCardClient.listSerialPorts()
            }.value

            guard let self else { return }
            self.serialRefreshInProgress = false
            self.serialPorts = ports
            self.serialMessage = ports.isEmpty
                ? "No macOS serial ports were found."
                : "\(ports.count) macOS serial port(s) found."
        }
    }

    func captureSerialPreview(portPath: String, baudRate: UInt32) {
        guard !serialCaptureInProgress else { return }
        serialCaptureInProgress = true
        serialCapture = nil
        serialMessage = "Capturing serial preview from \(portPath)..."

        Task { [weak self] in
            let result = await Task.detached(priority: .userInitiated) {
                do {
                    return Result<TimeCardSerialCapture, Error>.success(
                        try TimeCardClient.captureSerialPreview(
                            portPath: portPath,
                            baudRate: baudRate
                        )
                    )
                } catch {
                    return Result<TimeCardSerialCapture, Error>.failure(error)
                }
            }.value

            guard let self else { return }
            self.serialCaptureInProgress = false
            switch result {
            case .success(let capture):
                self.serialCapture = capture
                self.serialMessage =
                    "Captured \(capture.byteCount) byte(s) from \(capture.portPath)."
            case .failure(let error):
                self.serialMessage =
                    "Serial preview failed: \(error.localizedDescription)"
            }
        }
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

    func setLED(
        led: TimeCardLEDKind,
        red: UInt32,
        green: UInt32,
        blue: UInt32,
        globalCurrent: UInt32
    ) {
        guard !i2cOperationInProgress else { return }
        guard let descriptor = selectedDescriptor else {
            i2cOperationMessage = "No Time Card is selected."
            return
        }
        guard snapshot?.capabilityNames.contains("LEDs") == true else {
            i2cOperationMessage = "LED control is not advertised by this driver."
            return
        }

        i2cOperationInProgress = true
        i2cOperationMessage = "Setting \(led.label) LED..."

        Task { [weak self] in
            let result = await Task.detached(priority: .userInitiated) {
                do {
                    return Result<TimeCardLEDState, Error>.success(
                        try TimeCardClient.setLEDState(
                            for: descriptor,
                            led: led,
                            red: red,
                            green: green,
                            blue: blue,
                            globalCurrent: globalCurrent
                        )
                    )
                } catch {
                    return Result<TimeCardLEDState, Error>.failure(error)
                }
            }.value

            guard let self else { return }
            self.i2cOperationInProgress = false
            switch result {
            case .success(let state):
                self.mergeLEDStates([state])
                self.i2cOperationMessage =
                    "\(state.led.label) LED set and verified at RGB \(state.rgbText)."
                self.refresh()
            case .failure(let error):
                self.i2cOperationMessage =
                    "\(led.label) LED set failed: \(error.localizedDescription)"
            }
        }
    }

    func applyGNSSLEDPolicy() {
        applyLEDPolicy(
            label: "GNSS LED policy",
            includeGNSS: true,
            includeSMA: false
        )
    }

    func applySMALEDPolicy() {
        applyLEDPolicy(
            label: "SMA LED policy",
            includeGNSS: false,
            includeSMA: true
        )
    }

    func applyAllLEDPolicy() {
        applyLEDPolicy(
            label: "GNSS and SMA LED policy",
            includeGNSS: true,
            includeSMA: true
        )
    }

    func runReadOnlySelfTest() {
        guard !selfTestInProgress else { return }
        guard let descriptor = selectedDescriptor else {
            selfTestMessage = "No Time Card is selected."
            selfTestReport = TimeCardSelfTestReport(
                runAt: Date(),
                items: [
                    TimeCardSelfTestItem(
                        "Driver service",
                        severity: serviceDetected ? .warning : .waiting,
                        detail: serviceDetected
                            ? "A service was seen earlier, but it is not selected."
                            : "No Time Card DriverKit service has been discovered."
                    ),
                    TimeCardSelfTestItem(
                        "User-client access",
                        severity: .waiting,
                        detail: "Select or discover a Time Card before running the self-test."
                    ),
                ]
            )
            return
        }

        selfTestInProgress = true
        selfTestMessage = "Running read-only production self-test..."

        Task { [weak self] in
            let outcome = await Task.detached(
                priority: .userInitiated
            ) { () -> TimeCardSelfTestOutcome in
                do {
                    let currentSnapshot = try TimeCardClient.readSnapshot(
                        for: descriptor
                    )
                    var sensors: TimeCardSensorSnapshot?
                    var sensorError: String?
                    var routes: [TimeCardSMARoute] = []
                    var smaError: String?
                    var i2cStatus: TimeCardI2CStatusSnapshot?
                    var i2cMux: TimeCardI2CMuxSnapshot?
                    var ledStates: [TimeCardLEDState] = []
                    var i2cErrors: [String] = []

                    if currentSnapshot.capabilityNames.contains("SMA") {
                        do {
                            routes = try TimeCardClient.querySMARoutes(
                                for: descriptor
                            )
                        } catch {
                            smaError = error.localizedDescription
                        }
                    }

                    if currentSnapshot.capabilityNames.contains("Sensors") {
                        do {
                            sensors = try TimeCardClient.querySensors(
                                for: descriptor
                            )
                        } catch {
                            sensorError = error.localizedDescription
                        }
                    }

                    if currentSnapshot.capabilityNames.contains("I2C") {
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

                    if currentSnapshot.capabilityNames.contains("LEDs") {
                        do {
                            ledStates = try TimeCardClient.queryLEDStates(
                                for: descriptor
                            )
                        } catch {
                            i2cErrors.append(error.localizedDescription)
                        }
                    }

                    let i2cError = i2cErrors.isEmpty
                        ? nil : i2cErrors.joined(separator: "; ")
                    let report = TimeCardMonitor.buildSelfTestReport(
                        snapshot: currentSnapshot,
                        sensors: sensors,
                        sensorError: sensorError,
                        routes: routes,
                        smaError: smaError,
                        i2cStatus: i2cStatus,
                        i2cMux: i2cMux,
                        ledStates: ledStates,
                        i2cError: i2cError
                    )
                    return .success(
                        snapshot: currentSnapshot,
                        sensors: sensors,
                        sensorError: sensorError,
                        routes: routes,
                        smaError: smaError,
                        i2cStatus: i2cStatus,
                        i2cMux: i2cMux,
                        ledStates: ledStates,
                        i2cError: i2cError,
                        report: report
                    )
                } catch {
                    let report = TimeCardMonitor.buildFailedSelfTestReport(
                        error: error
                    )
                    return .failure(
                        report: report,
                        message: "Self-test failed: \(error.localizedDescription)"
                    )
                }
            }.value

            guard let self else { return }
            self.selfTestInProgress = false
            self.applySelfTestOutcome(outcome)
        }
    }

    private func applyLEDPolicy(
        label: String,
        includeGNSS: Bool,
        includeSMA: Bool
    ) {
        guard !i2cOperationInProgress else { return }
        guard let descriptor = selectedDescriptor else {
            i2cOperationMessage = "No Time Card is selected."
            return
        }
        guard let currentSnapshot = snapshot else {
            i2cOperationMessage = "No Time Card status snapshot is available."
            return
        }
        guard currentSnapshot.capabilityNames.contains("LEDs") else {
            i2cOperationMessage = "LED control is not advertised by this driver."
            return
        }
        if includeSMA && !currentSnapshot.capabilityNames.contains("SMA") {
            i2cOperationMessage = "SMA routing is not advertised by this driver."
            return
        }

        i2cOperationInProgress = true
        i2cOperationMessage = "Applying \(label)..."

        Task { [weak self] in
            let result = await Task.detached(priority: .userInitiated) {
                () -> Result<TimeCardLEDPolicyResult, Error> in
                do {
                    var states: [TimeCardLEDState] = []
                    var routes: [TimeCardSMARoute] = []

                    if includeGNSS {
                        for plan in TimeCardMonitor.gnssLEDPlans(
                            for: currentSnapshot
                        ) {
                            if let state = try TimeCardMonitor.applyLEDPlan(
                                plan,
                                descriptor: descriptor
                            ) {
                                states.append(state)
                            }
                        }
                    }

                    if includeSMA {
                        routes = try TimeCardClient.querySMARoutes(
                            for: descriptor
                        )
                        for route in routes where route.isPresent {
                            guard let plan = TimeCardMonitor.smaLEDPlan(
                                for: route
                            ) else {
                                continue
                            }
                            if let state = try TimeCardMonitor.applyLEDPlan(
                                plan,
                                descriptor: descriptor
                            ) {
                                states.append(state)
                            }
                        }
                    }

                    return .success(
                        TimeCardLEDPolicyResult(
                            states: states,
                            routes: routes
                        )
                    )
                } catch {
                    return .failure(error)
                }
            }.value

            guard let self else { return }
            self.i2cOperationInProgress = false
            switch result {
            case .success(let policyResult):
                self.mergeLEDStates(policyResult.states)
                if !policyResult.routes.isEmpty {
                    self.smaRoutes = policyResult.routes
                }
                self.i2cOperationMessage =
                    "\(label) applied to \(policyResult.states.count) LED(s)."
                self.refresh()
            case .failure(let error):
                self.i2cOperationMessage =
                    "\(label) failed: \(error.localizedDescription)"
            }
        }
    }

    private func applySelfTestOutcome(_ outcome: TimeCardSelfTestOutcome) {
        switch outcome {
        case .success(
            let currentSnapshot, let sensors, let sensorError, let routes,
            let smaError, let currentI2CStatus, let currentI2CMux,
            let currentLEDStates, let i2cError, let report
        ):
            snapshot = currentSnapshot
            state = .connected
            errorMessage = ""
            recoverySuggestion = ""
            lastUpdated = Date()
            appendSamplingWindow(currentSnapshot.sampleWindowNanoseconds)
            if !routes.isEmpty {
                smaRoutes = routes
            }
            if let smaError {
                smaMessage = "SMA self-test failed: \(smaError)"
            } else if !routes.isEmpty {
                smaMessage = "SMA connector states refreshed by self-test."
            } else if currentSnapshot.capabilityNames.contains("SMA") {
                smaMessage = "SMA self-test returned no connector routes."
            } else {
                smaMessage = "SMA routing is not advertised by this profile."
            }
            sensorTelemetry = sensors
            if let sensors {
                sensorMessage = sensors.validReadings.isEmpty
                    ? "Sensor self-test returned no valid samples."
                    : "\(sensors.validReadings.count) live sensor block(s)."
                appendSensorHistory(sensors)
            } else if let sensorError {
                sensorMessage = "Sensor self-test failed: \(sensorError)"
            } else if currentSnapshot.capabilityNames.contains("Sensors") {
                sensorMessage = "Sensor branch is advertised but no sample was returned."
            } else {
                sensorMessage = "Sensor branch is not advertised by this profile."
                sensorHistory.removeAll(keepingCapacity: true)
            }
            i2cStatus = currentI2CStatus
            i2cMux = currentI2CMux
            ledStates = currentLEDStates
            if let i2cError {
                i2cMessage = "I2C/LED self-test issue: \(i2cError)"
            } else if currentI2CStatus != nil || currentI2CMux != nil ||
                !currentLEDStates.isEmpty {
                let fitted = currentLEDStates.filter(\.isPresent).count
                i2cMessage =
                    "I2C/LED self-test refreshed, \(fitted)/\(currentLEDStates.count) LED state(s) live."
            } else if currentSnapshot.capabilityNames.contains("I2C") ||
                currentSnapshot.capabilityNames.contains("LEDs") {
                i2cMessage = "I2C or LED capability is advertised but returned no sample."
            } else {
                i2cMessage = "I2C and LED control are not advertised by this profile."
            }
            selfTestReport = report
            selfTestMessage = "Self-test complete: \(report.summary)."

        case .failure(let report, let message):
            selfTestReport = report
            selfTestMessage = message
        }
    }

    private func mergeLEDStates(_ updates: [TimeCardLEDState]) {
        var states = ledStates
        for update in updates {
            if let index = states.firstIndex(where: { $0.led == update.led }) {
                states[index] = update
            } else {
                states.append(update)
            }
        }
        states.sort { $0.led.rawValue < $1.led.rawValue }
        ledStates = states
    }

    nonisolated private static func gnssLEDPlans(
        for snapshot: TimeCardDeviceSnapshot
    ) -> [TimeCardLEDPlan] {
        var gnss1 = TimeCardLEDPlan(
            led: .gnss1,
            red: 145,
            green: 64,
            blue: 0,
            globalCurrent: 96,
            optional: false
        )

        if snapshot.gnssTelemetryAvailable,
           let fixValid = snapshot.gnssFixValidityBitSet,
           let fixOK = snapshot.gnssFixOK,
           let satValid = snapshot.satelliteDataValid,
           let seenSatellites = snapshot.seenSatellites {
            if fixValid && fixOK {
                gnss1 = TimeCardLEDPlan(
                    led: .gnss1,
                    red: 0,
                    green: 180,
                    blue: 30,
                    globalCurrent: 96,
                    optional: false
                )
            } else if satValid && seenSatellites != 0 {
                gnss1 = TimeCardLEDPlan(
                    led: .gnss1,
                    red: 170,
                    green: 78,
                    blue: 0,
                    globalCurrent: 96,
                    optional: false
                )
            } else {
                gnss1 = TimeCardLEDPlan(
                    led: .gnss1,
                    red: 180,
                    green: 0,
                    blue: 0,
                    globalCurrent: 96,
                    optional: false
                )
            }
        } else if snapshot.clockInSync == true {
            gnss1 = TimeCardLEDPlan(
                led: .gnss1,
                red: 0,
                green: 180,
                blue: 30,
                globalCurrent: 96,
                optional: false
            )
        }

        return [
            gnss1,
            TimeCardLEDPlan(
                led: .gnss2,
                red: 145,
                green: 64,
                blue: 0,
                globalCurrent: 96,
                optional: true
            ),
        ]
    }

    nonisolated private static func smaLEDPlan(
        for route: TimeCardSMARoute
    ) -> TimeCardLEDPlan? {
        guard let led = TimeCardLEDKind(rawValue: route.connector + 1) else {
            return nil
        }
        var red: UInt32 = 130
        var green: UInt32 = 55
        var blue: UInt32 = 0
        if route.isDisabled || route.direction == .disabled {
            red = 50
            green = 35
            blue = 0
        } else if route.direction == .input {
            red = 0
            green = 65
            blue = 180
        } else if route.direction == .output {
            red = 0
            green = 165
            blue = 30
        }
        return TimeCardLEDPlan(
            led: led,
            red: red,
            green: green,
            blue: blue,
            globalCurrent: 96,
            optional: false
        )
    }

    nonisolated private static func applyLEDPlan(
        _ plan: TimeCardLEDPlan,
        descriptor: TimeCardServiceDescriptor
    ) throws -> TimeCardLEDState? {
        do {
            return try TimeCardClient.setLEDState(
                for: descriptor,
                led: plan.led,
                red: plan.red,
                green: plan.green,
                blue: plan.blue,
                globalCurrent: plan.globalCurrent
            )
        } catch TimeCardClientError.methodFailed(let selector, let result)
            where plan.optional && selector == 7 &&
                result == kIOReturnUnsupported {
            return nil
        }
    }

    nonisolated private static func buildSelfTestReport(
        snapshot: TimeCardDeviceSnapshot,
        sensors: TimeCardSensorSnapshot?,
        sensorError: String?,
        routes: [TimeCardSMARoute],
        smaError: String?,
        i2cStatus: TimeCardI2CStatusSnapshot?,
        i2cMux: TimeCardI2CMuxSnapshot?,
        ledStates: [TimeCardLEDState],
        i2cError: String?
    ) -> TimeCardSelfTestReport {
        var items: [TimeCardSelfTestItem] = [
            TimeCardSelfTestItem(
                "Driver service",
                severity: .pass,
                detail: "A Time Card DriverKit service is selected."
            ),
            TimeCardSelfTestItem(
                "User-client access",
                severity: .pass,
                detail: "The app opened the DriverKit user client successfully."
            ),
            TimeCardSelfTestItem(
                "Board profile",
                severity: snapshot.boardProfile >= 1 &&
                    snapshot.boardProfile <= 5 ? .pass : .warning,
                detail: "\(snapshot.boardName), PCI \(snapshot.pciIdentity), \(snapshot.layoutName) register map."
            ),
            TimeCardSelfTestItem(
                "Clock read and cross-timestamp",
                severity: snapshot.sampleWindowNanoseconds > 0 ? .pass : .warning,
                detail: "Sampling window \(snapshot.sampleWindowNanoseconds) ns, source \(snapshot.configuredClockSource ?? 0)."
            ),
            TimeCardSelfTestItem(
                "Driver capability contract",
                severity: snapshot.abiVersion >= 7 ? .pass : .warning,
                detail: "ABI v\(snapshot.abiVersion), capabilities \(snapshot.capabilityNames.joined(separator: ", "))."
            ),
        ]

        if snapshot.capabilityNames.contains("ToD") {
            items.append(
                TimeCardSelfTestItem(
                    "ToD and GNSS summary",
                    severity: snapshot.todTelemetryAvailable ||
                        snapshot.gnssTelemetryAvailable ? .pass : .gated,
                    detail: snapshot.todTelemetryAvailable ||
                        snapshot.gnssTelemetryAvailable
                        ? "Optional UTC, leap, GNSS, or satellite summary registers are exposed."
                        : "This FPGA image exposes ToD status but not optional UTC/GNSS summary registers."
                )
            )
        } else {
            items.append(
                TimeCardSelfTestItem(
                    "ToD and GNSS summary",
                    severity: .gated,
                    detail: "The selected board profile does not advertise ToD telemetry."
                )
            )
        }

        if snapshot.capabilityNames.contains("SMA") {
            if let smaError {
                items.append(
                    TimeCardSelfTestItem(
                        "SMA routing",
                        severity: .fail,
                        detail: smaError
                    )
                )
            } else {
                let present = routes.filter(\.isPresent).count
                items.append(
                    TimeCardSelfTestItem(
                        "SMA routing",
                        severity: present == 4 ? .pass : .warning,
                        detail: "\(present)/4 connector route(s) returned live readback."
                    )
                )
            }
        } else {
            items.append(
                TimeCardSelfTestItem(
                    "SMA routing",
                    severity: .gated,
                    detail: "SMA routing is not advertised by this profile."
                )
            )
        }

        if snapshot.capabilityNames.contains("Sensors") {
            if let sensorError {
                items.append(
                    TimeCardSelfTestItem(
                        "Sensor fabric",
                        severity: .fail,
                        detail: sensorError
                    )
                )
            } else if let sensors {
                let valid = sensors.validReadings.count
                let severity: TimeCardSelfTestSeverity =
                    valid > 0 && sensors.muxWasRestored ? .pass : .warning
                items.append(
                    TimeCardSelfTestItem(
                        "Sensor fabric",
                        severity: severity,
                        detail: "\(valid)/\(sensors.readings.count) valid block(s), mux restored \(sensors.muxWasRestored ? "yes" : "no")."
                    )
                )
            } else {
                items.append(
                    TimeCardSelfTestItem(
                        "Sensor fabric",
                        severity: .warning,
                        detail: "Sensors are advertised but no sample was returned."
                    )
                )
            }
        } else {
            items.append(
                TimeCardSelfTestItem(
                    "Sensor fabric",
                    severity: .gated,
                    detail: "Sensors are not advertised by this profile."
                )
            )
        }

        if snapshot.capabilityNames.contains("I2C") {
            if let i2cError {
                items.append(
                    TimeCardSelfTestItem(
                        "I2C controller",
                        severity: .fail,
                        detail: i2cError
                    )
                )
            } else if let i2cStatus {
                let knownDevices = i2cStatus.knownDeviceNames.isEmpty
                    ? "none" : i2cStatus.knownDeviceNames.joined(separator: ", ")
                let severity: TimeCardSelfTestSeverity =
                    i2cStatus.isPresent &&
                    !i2cStatus.isBusBusy ? .pass : .warning
                items.append(
                    TimeCardSelfTestItem(
                        "I2C controller",
                        severity: severity,
                        detail: "Status 0x\(String(format: "%02x", i2cStatus.status & 0xff)), idle between transactions, known devices \(knownDevices)."
                    )
                )
            } else {
                items.append(
                    TimeCardSelfTestItem(
                        "I2C controller",
                        severity: .warning,
                        detail: "I2C is advertised but controller status was not returned."
                    )
                )
            }

            if let i2cMux {
                items.append(
                    TimeCardSelfTestItem(
                        "I2C mux",
                        severity: i2cMux.isPresent ? .pass : .warning,
                        detail: String(
                            format: "Mux %@, channel mask 0x%02x.",
                            i2cMux.isPresent ? "present" : "not present",
                            i2cMux.channelMask & 0xff
                        )
                    )
                )
            } else {
                items.append(
                    TimeCardSelfTestItem(
                        "I2C mux",
                        severity: .warning,
                        detail: "I2C mux readback was not returned."
                    )
                )
            }
        } else {
            items.append(
                TimeCardSelfTestItem(
                    "I2C controller",
                    severity: .gated,
                    detail: "I2C is not advertised by this profile."
                )
            )
            items.append(
                TimeCardSelfTestItem(
                    "I2C mux",
                    severity: .gated,
                    detail: "Mux control depends on the I2C controller ABI."
                )
            )
        }

        if snapshot.capabilityNames.contains("LEDs") {
            if let i2cError {
                items.append(
                    TimeCardSelfTestItem(
                        "LED controller",
                        severity: .fail,
                        detail: i2cError
                    )
                )
            } else {
                let present = ledStates.filter(\.isPresent).count
                items.append(
                    TimeCardSelfTestItem(
                        "LED controller",
                        severity: present > 0 ? .pass : .warning,
                        detail: "\(present)/\(ledStates.count) subsystem LED state(s) returned readback."
                    )
                )
            }
        } else {
            items.append(
                TimeCardSelfTestItem(
                    "LED controller",
                    severity: .gated,
                    detail: "LED control is not advertised by this profile."
                )
            )
        }

        return TimeCardSelfTestReport(runAt: Date(), items: items)
    }

    nonisolated private static func buildFailedSelfTestReport(
        error: Error
    ) -> TimeCardSelfTestReport {
        TimeCardSelfTestReport(
            runAt: Date(),
            items: [
                TimeCardSelfTestItem(
                    "Driver service",
                    severity: .warning,
                    detail: "A selected service was available before the self-test started."
                ),
                TimeCardSelfTestItem(
                    "User-client access",
                    severity: .fail,
                    detail: error.localizedDescription
                ),
                TimeCardSelfTestItem(
                    "Clock read and cross-timestamp",
                    severity: .fail,
                    detail: "The status snapshot could not be read."
                ),
            ]
        )
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
