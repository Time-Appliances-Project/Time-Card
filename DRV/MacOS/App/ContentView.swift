/* SPDX-License-Identifier: BSD-3-Clause */

import Charts
import AppKit
import SwiftUI
import UniformTypeIdentifiers

private enum ControlCenterPage: String, CaseIterable, Identifiable {
    case overview = "Overview"
    case clock = "Precision Clock"
    case atomic = "Atomic Clock"
    case gnss = "GNSS"
    case uart = "UART and NMEA"
    case sma = "SMA Routing"
    case timing = "Generators"
    case fpga = "FPGA Engines"
    case sensors = "Sensors and IMU"
    case i2c = "I2C and LEDs"
    case telemetry = "Telemetry Studio"
    case operations = "Profiles and Self-Test"
    case subsystems = "Subsystem Map"
    case flash = "FPGA Flash"
    case hardware = "Hardware"
    case driver = "Driver"

    var id: Self { self }

    static var initialPage: Self {
        let arguments = ProcessInfo.processInfo.arguments
        let inline = arguments.first(where: { $0.hasPrefix("--page=") }).map { String($0.dropFirst(7)) }
        let separate = arguments.firstIndex(of: "--page").flatMap { index in
            index + 1 < arguments.count ? arguments[index + 1] : nil
        }
        let requested = inline ?? separate ?? UserDefaults.standard.string(forKey: "controlCenterPage") ?? ""
        return allCases.first {
            $0.rawValue.caseInsensitiveCompare(requested) == .orderedSame ||
                String(describing: $0).caseInsensitiveCompare(requested) == .orderedSame
        } ?? .overview
    }

    var systemImage: String {
        switch self {
        case .overview: "gauge.with.dots.needle.67percent"
        case .clock: "clock.badge.checkmark"
        case .atomic: "atom"
        case .gnss: "location.north.line"
        case .uart: "terminal"
        case .sma: "cable.connector"
        case .timing: "waveform.path.ecg"
        case .fpga: "cpu"
        case .sensors: "gyroscope"
        case .i2c: "lightbulb.led"
        case .telemetry: "chart.xyaxis.line"
        case .operations: "checklist"
        case .subsystems: "point.3.connected.trianglepath.dotted"
        case .flash: "externaldrive.badge.timemachine"
        case .hardware: "memorychip"
        case .driver: "puzzlepiece.extension"
        }
    }
}

struct ContentView: View {
    @EnvironmentObject private var driverManager: DriverManager
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var selectedPage: ControlCenterPage = .initialPage
    @StateObject private var deviceLab = DeviceLabMonitor()

    var body: some View {
        NavigationSplitView {
            List(ControlCenterPage.allCases, selection: $selectedPage) { page in
                Label(page.rawValue, systemImage: page.systemImage)
                    .tag(page)
            }
            .listStyle(.sidebar)
            .navigationTitle("Time Card")
            .navigationSplitViewColumnWidth(min: 210, ideal: 235)
        } detail: {
            Group {
                switch selectedPage {
                case .overview:
                    OverviewView()
                case .clock:
                    PrecisionClockView()
                case .atomic:
                    AtomicClockView()
                case .gnss:
                    GNSSWorkspaceView()
                case .uart:
                    UARTWorkspaceView()
                case .sma:
                    SMARoutingView()
                case .timing:
                    TimingControlsView()
                case .fpga:
                    CapabilityWorkspaceView(workspace: .fpga)
                case .sensors:
                    SensorDashboardView()
                case .i2c:
                    I2CAndLEDView()
                case .telemetry:
                    TelemetryStudioView()
                case .operations:
                    OperationsView()
                case .subsystems:
                    SubsystemMapView()
                case .flash:
                    CapabilityWorkspaceView(workspace: .flash)
                case .hardware:
                    HardwareView()
                case .driver:
                    DriverView()
                }
            }
            .navigationTitle(selectedPage.rawValue)
            .disabled(monitor.profileOperationInProgress || monitor.peripheralOperationInProgress)
            .background(ControlCenterBackground())
            .toolbar {
                ToolbarItemGroup {
                    if monitor.services.count > 1 {
                        Picker(
                            "Time Card",
                            selection: Binding(
                                get: { monitor.selectedServiceID ?? 0 },
                                set: { monitor.selectService($0) }
                            )
                        ) {
                            ForEach(monitor.services) { service in
                                Text(service.displayName).tag(service.id)
                            }
                        }
                        .frame(maxWidth: 260)
                    }

                    Button {
                        monitor.refresh()
                    } label: {
                        Label("Refresh", systemImage: "arrow.clockwise")
                    }
                    .help("Refresh Time Card telemetry")
                    .keyboardShortcut("r", modifiers: .command)
                }
            }
        }
        .environmentObject(deviceLab)
        .onReceive(monitor.$snapshot) { _ in deviceLab.bind(monitor) }
        .onChange(of: monitor.state) { _, _ in deviceLab.bind(monitor) }
        .onChange(of: monitor.selectedServiceID) { _, _ in deviceLab.bind(monitor) }
        .onChange(of: selectedPage) { _, page in
            UserDefaults.standard.set(page.rawValue, forKey: "controlCenterPage")
        }
    }
}

private struct OverviewView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

                if let snapshot = monitor.snapshot {
                    HStack(spacing: 14) {
                        MetricCard(
                            title: "Time Card",
                            value: TimeCardFormatting.rawTimestamp(snapshot),
                            detail: "Raw hardware epoch",
                            systemImage: "clock",
                            accent: .cyan
                        )
                        MetricCard(
                            title: "macOS",
                            value: TimeCardFormatting.systemTimestamp(snapshot),
                            detail: "Realtime sampling midpoint",
                            systemImage: "desktopcomputer",
                            accent: .indigo
                        )
                        MetricCard(
                            title: "Sampling window",
                            value: TimeCardFormatting.duration(
                                snapshot.sampleWindowNanoseconds
                            ),
                            detail: "Bracketed cross timestamp",
                            systemImage: "arrow.left.and.right",
                            accent: .mint
                        )
                    }

                    HStack(alignment: .top, spacing: 18) {
                        ControlCenterPanel(
                            title: "Clock status",
                            subtitle: "Capability-aware DriverKit telemetry"
                        ) {
                            InfoRow(
                                label: "Synchronization",
                                value: TimeCardFormatting.syncStatus(snapshot),
                                valueColor: snapshot.clockInSync == true
                                    ? .green : .orange
                            )
                            InfoRow(
                                label: "Clock source",
                                value: TimeCardFormatting.clockSource(snapshot)
                            )
                            InfoRow(
                                label: "Register map",
                                value: snapshot.layoutName
                            )
                            InfoRow(
                                label: "Last update",
                                value: TimeCardFormatting.date(
                                    monitor.lastUpdated
                                )
                            )
                        }

                        ControlCenterPanel(
                            title: "Device",
                            subtitle: "Exact PCI profile selected by the driver"
                        ) {
                            InfoRow(label: "Board", value: snapshot.boardName)
                            InfoRow(
                                label: "PCI device",
                                value: snapshot.pciIdentity
                            )
                            InfoRow(
                                label: "PCI revision",
                                value: String(
                                    format: "0x%02x",
                                    snapshot.pciRevision & 0xff
                                )
                            )
                            InfoRow(
                                label: "Driver ABI",
                                value: "\(snapshot.abiVersion)"
                            )
                        }
                    }

                    SamplingWindowChart()

                    ControlCenterPanel(
                        title: "Control workspaces",
                        subtitle: "Windows Control Center feature map on macOS"
                    ) {
                        LazyVGrid(
                            columns: [
                                GridItem(.adaptive(minimum: 185), spacing: 12)
                            ],
                            spacing: 12
                        ) {
                            WorkspaceTile(
                                "Precision Clock",
                                "Live",
                                "PHC read, set, cross timestamp",
                                "clock.badge.checkmark",
                                .green
                            )
                            WorkspaceTile(
                                "GNSS",
                                snapshot.capabilityNames.contains("ToD")
                                    ? "Partial" : "Gated",
                                "ToD live, u-blox backend pending",
                                "location.north.line",
                                .cyan
                            )
                            WorkspaceTile(
                                "SMA",
                                snapshot.capabilityNames.contains("SMA")
                                    ? "Live" : "Gated",
                                "Route fabric readback and guarded writes",
                                "cable.connector",
                                .orange
                            )
                            WorkspaceTile(
                                "FPGA",
                                "Gated",
                                "Core contract and controls next",
                                "cpu",
                                .purple
                            )
                            WorkspaceTile(
                                "Sensors",
                                snapshot.capabilityNames.contains("Sensors")
                                    ? "Live" : "Gated",
                                "Environmental telemetry and IMU gap tracking",
                                "gyroscope",
                                .pink
                            )
                            WorkspaceTile(
                                "Flash",
                                "Gated",
                                "SPI flash safety ABI next",
                                "externaldrive.badge.timemachine",
                                .red
                            )
                        }
                    }

                    ControlCenterPanel(
                        title: "macOS port status",
                        subtitle: "Safe Control Center deployment"
                    ) {
                        Label(
                            "The dashboard reads card identity, clock status, "
                                + "Time of Day status, and bracketed timestamps. "
                                + "It can also set the card from macOS time "
                                + "through the guarded DriverKit ABI.",
                            systemImage: "checkmark.shield"
                        )
                        .foregroundStyle(.secondary)

                        Label(
                            "ABI v2 does not expose a trusted UTC-to-TAI "
                                + "offset, so raw card time is not labeled UTC "
                                + "and is not presented as a clock offset.",
                            systemImage: "info.circle"
                        )
                        .foregroundStyle(.secondary)
                    }
                } else {
                    MonitorUnavailableView()
                }
            }
            .padding(24)
        }
    }
}

private struct PrecisionClockView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ClockSourceControlPanel()
                if let snapshot = monitor.snapshot {
                    HStack(spacing: 14) {
                        MetricCard(
                            title: "Raw card time",
                            value: TimeCardFormatting.rawTimestamp(snapshot),
                            detail: "Seconds.nanoseconds",
                            systemImage: "timer"
                        )
                        MetricCard(
                            title: "Clock state",
                            value: TimeCardFormatting.syncStatus(snapshot),
                            detail: "Version-gated status register",
                            systemImage: snapshot.clockInSync == true
                                ? "checkmark.circle" : "exclamationmark.circle"
                        )
                        MetricCard(
                            title: "Clock source",
                            value: TimeCardFormatting.clockSource(snapshot),
                            detail: "Configured source register",
                            systemImage: "point.3.connected.trianglepath.dotted"
                        )
                    }

                    SamplingWindowChart()

                    HStack(alignment: .top, spacing: 18) {
                        ControlCenterPanel(
                            title: "Precision clock core",
                            subtitle: "Common PHC block"
                        ) {
                            InfoRow(
                                label: "Core version",
                                value: TimeCardFormatting.validHex(
                                    snapshot.clockVersion,
                                    valid: snapshot.hasValidField(1 << 0)
                                )
                            )
                            InfoRow(
                                label: "Status register",
                                value: TimeCardFormatting.validHex(
                                    snapshot.clockStatus,
                                    valid: snapshot.hasValidField(1 << 1)
                                )
                            )
                            InfoRow(
                                label: "Select register",
                                value: TimeCardFormatting.validHex(
                                    snapshot.clockSelect,
                                    valid: snapshot.hasValidField(1 << 2)
                                )
                            )
                            InfoRow(
                                label: "BAR offset",
                                value: TimeCardFormatting.hex(
                                    snapshot.clockOffset
                                )
                            )
                        }

                        ControlCenterPanel(
                            title: "Time of Day core",
                            subtitle: snapshot.capabilityNames.contains("ToD")
                                ? "Profile-supported block"
                                : "Not present on this board profile"
                        ) {
                            InfoRow(
                                label: "Capability",
                                value: snapshot.capabilityNames.contains("ToD")
                                    ? "Available" : "Not present"
                            )
                            InfoRow(
                                label: "Core version",
                                value: TimeCardFormatting.validHex(
                                    snapshot.todVersion,
                                    valid: snapshot.hasValidField(1 << 3)
                                )
                            )
                            InfoRow(
                                label: "Status register",
                                value: TimeCardFormatting.validHex(
                                    snapshot.todStatus,
                                    valid: snapshot.hasValidField(1 << 4)
                                )
                            )
                            InfoRow(
                                label: "BAR offset",
                                value: snapshot.todOffset == 0
                                    ? "Not present"
                                    : TimeCardFormatting.hex(snapshot.todOffset)
                            )
                        }
                    }

                    ControlCenterPanel(
                        title: "Time-scale boundary",
                        subtitle: "No assumed leap-second value"
                    ) {
                        Text(
                            "The Time Card core publishes an epoch-like raw "
                                + "counter. Until the driver reports a trusted "
                                + "image-specific UTC-to-TAI contract, the app "
                                + "keeps it separate from macOS UTC and does not "
                                + "claim a precision offset between them."
                        )
                        .foregroundStyle(.secondary)
                    }

                    ControlCenterPanel(
                        title: "Guarded clock control",
                        subtitle: "Implemented through DriverKit ABI v2"
                    ) {
                        Label(
                            "Set the Time Card PHC from macOS system time. "
                                + "This mirrors the safe one-shot Windows "
                                + "Control Center action and leaves macOS time "
                                + "unchanged.",
                            systemImage: "arrow.right.circle"
                        )
                        .foregroundStyle(.secondary)

                        HStack {
                            Button("Set Card from macOS Time") {
                                monitor.setCardFromSystem()
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(
                                monitor.commandInProgress ||
                                    !snapshot.capabilityNames.contains("Clock set")
                            )

                            if monitor.commandInProgress {
                                ProgressView()
                                    .controlSize(.small)
                            }
                        }

                        if !monitor.commandMessage.isEmpty {
                            Text(monitor.commandMessage)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                        }
                    }
                } else {
                    ControlCenterHeader()
                    MonitorUnavailableView()
                }
            }
            .padding(24)
        }
    }
}

private struct GNSSWorkspaceView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

                if let snapshot = monitor.snapshot {
                    HStack(spacing: 14) {
                        MetricCard(
                            title: "GNSS fix",
                            value: snapshot.gnssFixName,
                            detail: gnssFixDetail(snapshot),
                            systemImage: snapshot.gnssFixOK == true
                                ? "location.circle.fill"
                                : "location.slash",
                            accent: snapshot.gnssFixOK == true
                                ? .green : .orange
                        )
                        MetricCard(
                            title: "Satellites",
                            value: satelliteValue(snapshot),
                            detail: satelliteDetail(snapshot),
                            systemImage: "antenna.radiowaves.left.and.right",
                            accent: snapshot.satelliteDataValid == true
                                ? .cyan : .secondary
                        )
                        MetricCard(
                            title: "UTC summary",
                            value: utcValue(snapshot),
                            detail: leapDetail(snapshot),
                            systemImage: snapshot.utcOffsetValid == true
                                ? "clock.badge.checkmark"
                                : "clock.badge.exclamationmark",
                            accent: snapshot.utcOffsetValid == true
                                ? .green : .orange
                        )
                    }

                    HStack(alignment: .top, spacing: 18) {
                        ControlCenterPanel(
                            title: "Time-of-Day core",
                            subtitle: "DriverKit register snapshot"
                        ) {
                            InfoRow(
                                label: "Capability",
                                value: snapshot.capabilityNames.contains("ToD")
                                    ? "Available" : "Not present"
                            )
                            InfoRow(
                                label: "ToD version",
                                value: TimeCardFormatting.validHex(
                                    snapshot.todVersion,
                                    valid: snapshot.hasValidField(1 << 3)
                                )
                            )
                            InfoRow(
                                label: "ToD status",
                                value: TimeCardFormatting.validHex(
                                    snapshot.todStatus,
                                    valid: snapshot.hasValidField(1 << 4)
                                )
                            )
                            InfoRow(
                                label: "ToD offset",
                                value: snapshot.todOffset == 0
                                    ? "Not present"
                                    : TimeCardFormatting.hex(snapshot.todOffset)
                            )
                        }

                        ControlCenterPanel(
                            title: "GNSS and UTC summary",
                            subtitle: "Windows-compatible raw fields"
                        ) {
                            InfoRow(
                                label: "GNSS status",
                                value: TimeCardFormatting.validHex(
                                    snapshot.gnssStatus,
                                    valid: snapshot.gnssTelemetryAvailable
                                )
                            )
                            InfoRow(
                                label: "Satellites",
                                value: TimeCardFormatting.validHex(
                                    snapshot.satellites,
                                    valid: snapshot.gnssTelemetryAvailable
                                )
                            )
                            InfoRow(
                                label: "UTC status",
                                value: TimeCardFormatting.validHex(
                                    snapshot.utcStatus,
                                    valid: snapshot.todTelemetryAvailable
                                )
                            )
                            InfoRow(
                                label: "Leap status",
                                value: TimeCardFormatting.validHex(
                                    snapshot.leap,
                                    valid: snapshot.todTelemetryAvailable
                                )
                            )
                        }
                    }

                    ControlCenterPanel(
                        title: "Receiver feature coverage",
                        subtitle: "Windows Control Center parity map"
                    ) {
                        VStack(spacing: 10) {
                            FeatureRow(
                                name: "ToD core status",
                                state: snapshot.hasValidField(1 << 4)
                                    ? "Live" : "Unavailable",
                                note: "Version and status are read through the current macOS ABI."
                            )
                            FeatureRow(
                                name: "UTC and leap summary",
                                state: snapshot.todTelemetryAvailable
                                    ? "Live" : "Gated",
                                note: "Uses ToD UTC and leap summary registers when the FPGA image exposes them."
                            )
                            FeatureRow(
                                name: "GNSS fix and satellite counts",
                                state: snapshot.gnssTelemetryAvailable
                                    ? "Live" : "Gated",
                                note: "Decodes the same fix and satellite summary fields used by the Windows dashboard."
                            )
                            FeatureRow(
                                name: "u-blox identity and firmware",
                                state: snapshot.supportsUARTWrite
                                    ? "Available" : "Backend pending",
                                note: "ABI v9 can send safe UBX poll requests; use the UART lab to capture MON-VER responses."
                            )
                            FeatureRow(
                                name: "Sky map and constellation view",
                                state: "Backend pending",
                                note: "Needs UBX-NAV-SAT samples from the receiver stream."
                            )
                            FeatureRow(
                                name: "Survey-in and fixed-position controls",
                                state: "Backend pending",
                                note: "Needs a write-gated receiver configuration ABI with persistence warnings."
                            )
                        }
                    }
                } else {
                    MonitorUnavailableView()
                }
            }
            .padding(24)
        }
    }

    private func gnssFixDetail(_ snapshot: TimeCardDeviceSnapshot) -> String {
        guard snapshot.gnssTelemetryAvailable else {
            return "GNSS summary registers are not exposed yet."
        }
        if snapshot.gnssFixOK == true {
            return "Receiver reports a valid timing fix."
        }
        if snapshot.gnssFixValidityBitSet == true {
            return "Fix field is valid, but fix OK is not asserted."
        }
        return "Fix validity bit is not asserted."
    }

    private func satelliteValue(_ snapshot: TimeCardDeviceSnapshot) -> String {
        guard let seen = snapshot.seenSatellites,
              let locked = snapshot.lockedSatellites else {
            return "Unavailable"
        }
        return "\(seen) seen / \(locked) locked"
    }

    private func satelliteDetail(_ snapshot: TimeCardDeviceSnapshot) -> String {
        guard snapshot.gnssTelemetryAvailable else {
            return "Satellite summary is not exposed yet."
        }
        return snapshot.satelliteDataValid == true
            ? "Satellite count is marked valid."
            : "Satellite count is not marked valid."
    }

    private func utcValue(_ snapshot: TimeCardDeviceSnapshot) -> String {
        guard let offset = snapshot.utcOffsetSeconds else {
            return "Unavailable"
        }
        return snapshot.utcOffsetValid == true
            ? "UTC +\(offset) s"
            : "UTC not valid"
    }

    private func leapDetail(_ snapshot: TimeCardDeviceSnapshot) -> String {
        guard snapshot.todTelemetryAvailable else {
            return "UTC/leap summary is not exposed yet."
        }
        if snapshot.leapInformationValid == true {
            return "Leap info valid, next \(Int32(bitPattern: snapshot.leap)) s."
        }
        return "Leap information is not marked valid."
    }
}

private struct UARTWorkspaceView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var selectedSerialPortID = ""
    @State private var serialBaudRate: UInt32 = 115_200
    @State private var selectedUARTPort: TimeCardUARTPort = .gnss
    @State private var hardwareUARTBaudRate: UInt32 = 115_200
    @State private var hardwareUARTCaptureDurationSeconds: Double = 10
    @State private var nmeaText = ""
    @State private var nmeaMessage = ""
    @State private var ubxInputText = ""
    @State private var ubxMessage = ""
    @State private var uartCaptureSaveMessage = ""
    @State private var pendingUBXAutoload = false
    @State private var receiverSource = "Automatic"
    private let serialBaudRates: [UInt32] = [
        9_600, 19_200, 38_400, 57_600, 115_200, 230_400,
    ]
    private let uartCaptureDurations: [Double] = [5, 10, 30, 60]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

                ControlCenterPanel(
                    title: "UART and NMEA Laboratory",
                    subtitle: "Capture, inspect, and replay receiver traffic"
                ) {
                    HStack(alignment: .top, spacing: 14) {
                        Image(systemName: "terminal")
                            .font(.system(size: 34, weight: .semibold))
                            .foregroundStyle(.teal)
                            .frame(width: 58, height: 58)
                            .background(.teal.opacity(0.14), in: RoundedRectangle(cornerRadius: 16))

                        VStack(alignment: .leading, spacing: 10) {
                            Text(uartHeaderText)
                            .foregroundStyle(.secondary)

                            HStack(spacing: 8) {
                                StatusPill(serialState, serialColor)
                                StatusPill(uartState, uartColor)
                            }
                        }

                        Spacer()

                        Button("Refresh Ports") {
                            monitor.refreshSerialPorts()
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(monitor.serialRefreshInProgress)
                    }

                    if !monitor.serialMessage.isEmpty {
                        Divider()
                        Text(monitor.serialMessage)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .textSelection(.enabled)
                    }
                }

                Picker("Receiver console source", selection: $receiverSource) {
                    ForEach(["Automatic", "Hardware capture", "Hardware read", "Serial preview", "UBX input", "NMEA input"], id: \.self) {
                        Text($0).tag($0)
                    }
                }.frame(maxWidth: 380)

                ReceiverConsoleView(liveInput: ReceiverConsoleInput(
                    source: receiverMixedSourceDescription,
                    bytes: receiverMixedStreamBytes
                ))


                ControlCenterPanel(
                    title: "Feature coverage",
                    subtitle: "Windows UART workspace mapped to macOS readiness"
                ) {
                    VStack(spacing: 10) {
                        FeatureRow(
                            name: "Generic macOS serial ports",
                            state: serialState,
                            note: "Enumerates IOSerialBSDClient devices and exposes callout and dial-in paths."
                        )
                        FeatureRow(
                            name: "Time Card UART streams",
                            state: uartState,
                            note: "Uses DriverKit ABI v8/v9 to observe, configure, read bounded FPGA UART samples, and send guarded receiver polls."
                        )
                        FeatureRow(
                            name: "NMEA capture and export",
                            state: nmeaCaptureState,
                            note: "Paste or load receiver sentences to validate checksum and decode common GNSS messages."
                        )
                        FeatureRow(
                            name: "u-blox UBX receiver traffic",
                            state: ubxCaptureState,
                            note: "Decodes captured, pasted, or direct hardware UART UBX frames."
                        )
                    }
                }

                ControlCenterPanel(
                    title: "Time Card hardware UART",
                    subtitle: "Direct bounded reads from the FPGA 16550 UART blocks"
                ) {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack(alignment: .bottom, spacing: 12) {
                            VStack(alignment: .leading, spacing: 6) {
                                Text("Port")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                Picker("", selection: $selectedUARTPort) {
                                    ForEach(TimeCardUARTPort.allCases) { port in
                                        Text(port.label).tag(port)
                                    }
                                }
                                .labelsHidden()
                                .frame(width: 170)
                            }

                            VStack(alignment: .leading, spacing: 6) {
                                Text("Baud")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                Picker("", selection: $hardwareUARTBaudRate) {
                                    ForEach(serialBaudRates, id: \.self) { baud in
                                        Text("\(baud)").tag(baud)
                                    }
                                }
                                .labelsHidden()
                                .frame(width: 120)
                            }

                            Button("Configure") {
                                monitor.configureUART(
                                    port: selectedUARTPort,
                                    baudRate: hardwareUARTBaudRate
                                )
                            }
                            .buttonStyle(.bordered)
                            .disabled(
                                !hardwareUARTAvailable ||
                                    monitor.uartOperationInProgress ||
                                    monitor.uartCaptureInProgress
                            )

                            Button("Observe") {
                                monitor.observeUART(port: selectedUARTPort)
                            }
                            .buttonStyle(.bordered)
                            .disabled(
                                !hardwareUARTAvailable ||
                                    monitor.uartOperationInProgress ||
                                    monitor.uartCaptureInProgress
                            )

                            Button("Read Hardware") {
                                monitor.readUART(port: selectedUARTPort)
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(
                                !hardwareUARTAvailable ||
                                    monitor.uartOperationInProgress ||
                                    monitor.uartCaptureInProgress
                            )

                            Menu("Send UBX Poll") {
                                ForEach(TimeCardUBXPoll.allCases) { poll in
                                    Button(poll.label) {
                                        sendUBXPoll(poll)
                                    }
                                }
                            }
                            .disabled(
                                !hardwareUARTWriteAvailable ||
                                    !selectedUARTPort.supportsReceiverPolls ||
                                    monitor.uartOperationInProgress ||
                                    monitor.uartCaptureInProgress
                            )

                            if monitor.uartOperationInProgress ||
                                monitor.uartCaptureInProgress {
                                ProgressView()
                                    .controlSize(.small)
                            }

                            Spacer()
                        }

                        Text(selectedUARTPort.detail)
                            .font(.caption)
                            .foregroundStyle(.secondary)

                        HStack(alignment: .bottom, spacing: 12) {
                            VStack(alignment: .leading, spacing: 6) {
                                Text("Capture")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                Picker("", selection: $hardwareUARTCaptureDurationSeconds) {
                                    ForEach(uartCaptureDurations, id: \.self) { seconds in
                                        Text("\(Int(seconds)) s").tag(seconds)
                                    }
                                }
                                .labelsHidden()
                                .frame(width: 100)
                            }

                            Button("Start Capture") {
                                monitor.startUARTCapture(
                                    port: selectedUARTPort,
                                    baudRate: hardwareUARTBaudRate,
                                    durationSeconds: hardwareUARTCaptureDurationSeconds
                                )
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(
                                !hardwareUARTAvailable ||
                                    monitor.uartOperationInProgress ||
                                    monitor.uartCaptureInProgress
                            )

                            Button("Stop") {
                                monitor.stopUARTCapture()
                            }
                            .buttonStyle(.bordered)
                            .disabled(!monitor.uartCaptureInProgress)

                            Button("Clear Capture") {
                                monitor.clearUARTCapture()
                            }
                            .buttonStyle(.bordered)
                            .disabled(
                                monitor.uartCaptureInProgress ||
                                    monitor.uartCapture == nil
                            )

                            Spacer()

                            Text("Capture loops safe 256-byte DriverKit reads.")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }

                        if !hardwareUARTAvailable {
                            Text(
                                "Install and activate the ABI v8 driver to enable "
                                    + "direct FPGA UART access on supported variants."
                            )
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        }

                        if hardwareUARTAvailable && !hardwareUARTWriteAvailable {
                            Text(
                                "Install and activate the ABI v9 driver to enable "
                                    + "bounded UART writes and UBX receiver polls."
                            )
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        } else if hardwareUARTWriteAvailable &&
                            !selectedUARTPort.supportsReceiverPolls {
                            Text("UBX polls are enabled for GNSS receiver ports only.")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }

                        if !monitor.uartMessage.isEmpty {
                            Text(monitor.uartMessage)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                        }

                        if let observation = monitor.uartObservation {
                            HStack(spacing: 8) {
                                StatusPill(
                                    observation.isPresent ? "Present" : "Not present",
                                    observation.isPresent ? .green : .secondary
                                )
                                StatusPill(
                                    observation.hasActivity ? "Activity" : "Idle",
                                    observation.hasActivity ? .green : .orange
                                )
                                StatusPill("LSR \(observation.lineStatusText)", .teal)
                            }
                        }

                        if let write = monitor.uartWriteResult {
                            HStack(spacing: 8) {
                                StatusPill(
                                    write.complete ? "Write complete" : "Partial write",
                                    write.complete ? .green : .orange
                                )
                                StatusPill(
                                    "\(write.byteCount)/\(write.requestedByteCount) bytes",
                                    write.complete ? .green : .orange
                                )
                                StatusPill("LSR \(write.lineStatusText)", .teal)
                                Text("Run Read Hardware to capture the receiver response.")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }

                        if let transfer = monitor.uartReadResult {
                            HStack(spacing: 8) {
                                StatusPill(
                                    transfer.byteCount == 0
                                        ? "No bytes" : "\(transfer.byteCount) bytes",
                                    transfer.byteCount == 0 ? .orange : .green
                                )
                                StatusPill("LSR \(transfer.lineStatusText)", .teal)

                                Spacer()

                                Button("Load as NMEA") {
                                    nmeaText = transfer.text
                                    nmeaMessage = "Loaded hardware UART bytes."
                                }
                                .buttonStyle(.bordered)
                                .disabled(transfer.data.isEmpty)

                                Button("Load as UBX") {
                                    ubxInputText = transfer.dataHex
                                    ubxMessage = "Loaded hardware UART bytes."
                                }
                                .buttonStyle(.bordered)
                                .disabled(transfer.data.isEmpty)
                            }

                            Text(uartReadPreviewText(transfer))
                                .font(.system(.caption, design: .monospaced))
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                                .padding(10)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .background(
                                    Color.secondary.opacity(0.07),
                                    in: RoundedRectangle(cornerRadius: 10)
                                )
                        }

                        if let capture = monitor.uartCapture {
                            Divider()
                            VStack(alignment: .leading, spacing: 10) {
                                HStack(spacing: 8) {
                                    StatusPill(capture.stopReason, .teal)
                                    StatusPill(
                                        capture.byteCount == 0
                                            ? "No bytes" : "\(capture.byteCount) bytes",
                                        capture.byteCount == 0 ? .orange : .green
                                    )
                                    StatusPill("\(capture.baudRate) baud", .teal)
                                    StatusPill(
                                        String(
                                            format: "%.1f/%.0f s",
                                            capture.durationSeconds,
                                            capture.requestedDurationSeconds
                                        ),
                                        .secondary
                                    )
                                    StatusPill(
                                        "\(capture.readCount) read window(s)",
                                        .secondary
                                    )
                                    StatusPill("LSR \(capture.lineStatusText)", .teal)

                                    Spacer()

                                    Button("Load as NMEA") {
                                        nmeaText = capture.text
                                        nmeaMessage = "Loaded hardware UART capture."
                                    }
                                    .buttonStyle(.bordered)
                                    .disabled(capture.data.isEmpty)

                                    Button("Load as UBX") {
                                        ubxInputText = capture.dataHex
                                        ubxMessage = "Loaded hardware UART capture."
                                    }
                                    .buttonStyle(.bordered)
                                    .disabled(capture.data.isEmpty)

                                    Button("Copy Text") {
                                        NSPasteboard.general.clearContents()
                                        NSPasteboard.general.setString(
                                            capture.text,
                                            forType: .string
                                        )
                                    }
                                    .buttonStyle(.bordered)
                                    .disabled(capture.data.isEmpty)

                                    Button("Copy Hex") {
                                        NSPasteboard.general.clearContents()
                                        NSPasteboard.general.setString(
                                            capture.dataHex,
                                            forType: .string
                                        )
                                    }
                                    .buttonStyle(.bordered)
                                    .disabled(capture.data.isEmpty)

                                    Button("Save Capture") {
                                        saveUARTCapture(capture)
                                    }
                                    .buttonStyle(.bordered)
                                    .disabled(capture.data.isEmpty)
                                }

                                if !uartCaptureSaveMessage.isEmpty {
                                    Text(uartCaptureSaveMessage)
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                        .textSelection(.enabled)
                                }

                                Text(uartCapturePreviewText(capture))
                                    .font(.system(.caption, design: .monospaced))
                                    .foregroundStyle(.secondary)
                                    .textSelection(.enabled)
                                    .padding(10)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .background(
                                        Color.secondary.opacity(0.07),
                                        in: RoundedRectangle(cornerRadius: 10)
                                    )
                            }
                        }
                    }
                }

                ControlCenterPanel(
                    title: "macOS serial ports",
                    subtitle: "Read-only IOKit inventory"
                ) {
                    if monitor.serialRefreshInProgress {
                        HStack(spacing: 10) {
                            ProgressView()
                                .controlSize(.small)
                            Text("Refreshing serial ports...")
                                .foregroundStyle(.secondary)
                        }
                    } else if monitor.serialPorts.isEmpty {
                        ContentUnavailableView(
                            "No serial ports found",
                            systemImage: "cable.connector.slash",
                            description: Text(
                                "Connect a USB serial adapter or expose a receiver "
                                    + "serial endpoint, then refresh this page."
                            )
                        )
                        .frame(height: 170)
                    } else {
                        LazyVGrid(
                            columns: [
                                GridItem(.adaptive(minimum: 260), spacing: 12)
                            ],
                            spacing: 12
                        ) {
                            ForEach(monitor.serialPorts) { port in
                                SerialPortCard(port: port)
                            }
                        }
                    }
                }

                ControlCenterPanel(
                    title: "Serial preview capture",
                    subtitle: "Bounded read from a selected macOS serial device"
                ) {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack(alignment: .bottom, spacing: 12) {
                            VStack(alignment: .leading, spacing: 6) {
                                Text("Port")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                Picker("", selection: $selectedSerialPortID) {
                                    ForEach(monitor.serialPorts) { port in
                                        Text(port.displayName)
                                            .tag(port.calloutDevice)
                                    }
                                }
                                .labelsHidden()
                                .frame(width: 220)
                                .disabled(monitor.serialPorts.isEmpty)
                            }

                            VStack(alignment: .leading, spacing: 6) {
                                Text("Baud")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                Picker("", selection: $serialBaudRate) {
                                    ForEach(serialBaudRates, id: \.self) { baud in
                                        Text("\(baud)").tag(baud)
                                    }
                                }
                                .labelsHidden()
                                .frame(width: 120)
                            }

                            Button("Capture Preview") {
                                guard let path = selectedSerialPortPath else {
                                    return
                                }
                                monitor.captureSerialPreview(
                                    portPath: path,
                                    baudRate: serialBaudRate
                                )
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(
                                selectedSerialPortPath == nil ||
                                    monitor.serialCaptureInProgress
                            )

                            if monitor.serialCaptureInProgress {
                                ProgressView()
                                    .controlSize(.small)
                            }

                            Spacer()

                            Button("Decode Capture") {
                                nmeaText = monitor.serialCapture?.text ?? ""
                                nmeaMessage = "Loaded serial capture into the NMEA decoder."
                            }
                            .buttonStyle(.bordered)
                            .disabled(monitor.serialCapture?.data.isEmpty != false)
                        }

                        Text(
                            "Preview capture reads for a short bounded window "
                                + "and restores the serial settings when it exits. "
                                + "Use the callout device for receiver traffic."
                        )
                        .font(.caption)
                        .foregroundStyle(.secondary)

                        if let capture = monitor.serialCapture {
                            HStack(spacing: 8) {
                                StatusPill(
                                    capture.byteCount == 0
                                        ? "No bytes" : "\(capture.byteCount) bytes",
                                    capture.byteCount == 0 ? .orange : .green
                                )
                                StatusPill("\(capture.baudRate) baud", .teal)
                                Text(TimeCardFormatting.date(capture.capturedAt))
                                    .font(.caption.monospacedDigit())
                                    .foregroundStyle(.secondary)
                            }

                            Text(serialCapturePreviewText(capture))
                                .font(.system(.caption, design: .monospaced))
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                                .padding(10)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .background(
                                    Color.secondary.opacity(0.07),
                                    in: RoundedRectangle(cornerRadius: 10)
                                )
                        } else {
                            Text(
                                "No preview capture yet. Select a serial port "
                                    + "and baud rate, then capture a short sample."
                            )
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        }
                    }
                }

                ControlCenterPanel(
                    title: "Receiver stream summary",
                    subtitle: "Roll-up of decoded UBX, NMEA, and RTCM3 capture data"
                ) {
                    ReceiverStreamSummaryView(
                        nmeaSentences: decodedNMEASentences,
                        ubxFrames: decodedUBXFrames,
                        mixedMessages: receiverMixedMessages
                    )
                }

                ControlCenterPanel(
                    title: "Receiver validation checklist",
                    subtitle: "Windows GNSS workflow mapped to current capture evidence"
                ) {
                    ReceiverValidationChecklistView(
                        nmeaSentences: decodedNMEASentences,
                        ubxFrames: decodedUBXFrames,
                        mixedMessages: receiverMixedMessages,
                        hardwareUARTAvailable: hardwareUARTAvailable,
                        hardwareUARTWriteAvailable: hardwareUARTWriteAvailable,
                        sourceDescription: receiverMixedSourceDescription
                    )
                }

                ControlCenterPanel(
                    title: "Receiver mixed stream decoder",
                    subtitle: "Protocol-aware UBX, NMEA, and RTCM3 timeline"
                ) {
                    ReceiverMixedStreamView(
                        messages: receiverMixedMessages,
                        sourceDescription: receiverMixedSourceDescription
                    )
                }

                ControlCenterPanel(
                    title: "u-blox UBX decoder lab",
                    subtitle: "Decode binary receiver frames from capture bytes or pasted hex"
                ) {
                    VStack(alignment: .leading, spacing: 12) {
                        TextEditor(text: $ubxInputText)
                            .font(.system(.caption, design: .monospaced))
                            .frame(minHeight: 110)
                            .padding(6)
                            .background(
                                Color.secondary.opacity(0.07),
                                in: RoundedRectangle(cornerRadius: 10)
                            )
                            .overlay(
                                RoundedRectangle(cornerRadius: 10)
                                    .stroke(Color.secondary.opacity(0.16), lineWidth: 1)
                            )

                        HStack {
                            Button("Load Capture Bytes") {
                                let bytes = monitor.uartCapture?.data ??
                                    monitor.serialCapture?.data ?? []
                                ubxInputText = bytes
                                    .map { String(format: "%02x", $0) }
                                    .joined(separator: " ")
                                ubxMessage = "Loaded capture bytes."
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(
                                monitor.uartCapture?.data.isEmpty != false &&
                                    monitor.serialCapture?.data.isEmpty != false
                            )

                            Button("Load Pasteboard") {
                                ubxInputText = NSPasteboard.general.string(
                                    forType: .string
                                ) ?? ""
                                ubxMessage = "Loaded pasteboard text."
                            }
                            .buttonStyle(.bordered)

                            Button("Clear") {
                                ubxInputText = ""
                                ubxMessage = "UBX input cleared."
                            }
                            .buttonStyle(.bordered)

                            Spacer()

                            Text(ubxDecodeSummary)
                                .font(.caption.monospacedDigit())
                                .foregroundStyle(.secondary)
                        }

                        if !ubxMessage.isEmpty {
                            Text(ubxMessage)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                        }

                        if decodedUBXFrames.isEmpty {
                            Text(
                                "Paste UBX hex bytes beginning with b5 62, "
                                    + "or capture a receiver serial preview. "
                                    + "The decoder verifies Fletcher checksums "
                                    + "and summarizes common NAV, MON, TIM, "
                                    + "and CFG messages."
                            )
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        } else {
                            VStack(spacing: 10) {
                                ForEach(decodedUBXFrames) { frame in
                                    UBXFrameRow(frame: frame)
                                }
                            }
                        }
                    }
                }

                ControlCenterPanel(
                    title: "NMEA decoder lab",
                    subtitle: "Paste GNSS receiver sentences and validate checksums"
                ) {
                    VStack(alignment: .leading, spacing: 12) {
                        TextEditor(text: $nmeaText)
                            .font(.system(.caption, design: .monospaced))
                            .frame(minHeight: 130)
                            .padding(6)
                            .background(
                                Color.secondary.opacity(0.07),
                                in: RoundedRectangle(cornerRadius: 10)
                            )
                            .overlay(
                                RoundedRectangle(cornerRadius: 10)
                                    .stroke(Color.secondary.opacity(0.16), lineWidth: 1)
                            )

                        HStack {
                            Button("Load Hardware Capture") {
                                nmeaText = monitor.uartCapture?.text ?? ""
                                nmeaMessage = "Loaded hardware UART capture."
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(monitor.uartCapture?.data.isEmpty != false)

                            Button("Load Pasteboard") {
                                nmeaText = NSPasteboard.general.string(
                                    forType: .string
                                ) ?? ""
                                nmeaMessage = "Loaded pasteboard text."
                            }
                            .buttonStyle(.bordered)

                            Button("Clear") {
                                nmeaText = ""
                                nmeaMessage = "NMEA input cleared."
                            }
                            .buttonStyle(.bordered)

                            Spacer()

                            Text(nmeaDecodeSummary)
                                .font(.caption.monospacedDigit())
                                .foregroundStyle(.secondary)
                        }

                        if !nmeaMessage.isEmpty {
                            Text(nmeaMessage)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                        }

                        if decodedNMEASentences.isEmpty {
                            Text(
                                "Paste lines such as $GPGGA, $GPRMC, $GPGSV, "
                                    + "or $GPZDA. The app verifies checksums "
                                    + "when a *hh suffix is present."
                            )
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        } else {
                            VStack(spacing: 10) {
                                ForEach(decodedNMEASentences) { sentence in
                                    NMEASentenceRow(sentence: sentence)
                                }
                            }
                        }
                    }
                }
            }
            .padding(24)
        }
        .onAppear {
            monitor.refreshSerialPorts()
            selectFirstSerialPortIfNeeded(monitor.serialPorts)
        }
        .onChange(of: monitor.serialPorts) { _, ports in
            selectFirstSerialPortIfNeeded(ports)
        }
        .onChange(of: monitor.uartReadResult) { _, transfer in
            guard pendingUBXAutoload, let transfer else {
                return
            }
            pendingUBXAutoload = false
            guard !transfer.data.isEmpty else {
                ubxMessage = "No hardware response captured after UBX poll."
                return
            }
            ubxInputText = transfer.dataHex
            ubxMessage =
                "Captured \(transfer.byteCount) hardware UART byte(s) after UBX poll."
        }
        .onChange(of: monitor.uartOperationInProgress) { _, inProgress in
            if !inProgress && pendingUBXAutoload {
                pendingUBXAutoload = false
                ubxMessage = "No hardware response captured after UBX poll."
            }
        }
    }

    private var serialState: String {
        if monitor.serialRefreshInProgress {
            return "Waiting"
        }
        return monitor.serialPorts.isEmpty ? "Unavailable" : "Live"
    }

    private var serialColor: Color {
        switch serialState {
        case "Live": .green
        case "Waiting": .orange
        default: .secondary
        }
    }

    private var hardwareUARTAvailable: Bool {
        monitor.snapshot?.supportsUART == true
    }

    private var hardwareUARTWriteAvailable: Bool {
        monitor.snapshot?.supportsUARTWrite == true
    }

    private var uartState: String {
        if monitor.uartOperationInProgress || monitor.uartCaptureInProgress {
            return "Waiting"
        }
        if hardwareUARTAvailable {
            return monitor.uartCapture?.byteCount ?? 0 > 0 ||
                monitor.uartReadResult?.byteCount ?? 0 > 0
                ? "Live" : "Available"
        }
        return "Gated"
    }

    private var uartColor: Color {
        switch uartState {
        case "Live", "Available": .green
        case "Waiting": .orange
        default: .secondary
        }
    }

    private var nmeaCaptureState: String {
        if monitor.serialCapture?.byteCount ?? 0 > 0 ||
            monitor.uartCapture?.byteCount ?? 0 > 0 ||
            monitor.uartReadResult?.byteCount ?? 0 > 0 {
            return "Live"
        }
        return decodedNMEASentences.isEmpty ? "Partial" : "Live"
    }

    private var ubxCaptureState: String {
        if monitor.uartCapture?.byteCount ?? 0 > 0 ||
            monitor.uartReadResult?.byteCount ?? 0 > 0 {
            return "Live"
        }
        return decodedUBXFrames.isEmpty ? "Partial" : "Live"
    }

    private var selectedSerialPortPath: String? {
        if monitor.serialPorts.contains(where: { $0.calloutDevice == selectedSerialPortID }) {
            return selectedSerialPortID
        }
        return monitor.serialPorts.first?.calloutDevice
    }

    private func selectFirstSerialPortIfNeeded(_ ports: [TimeCardSerialPort]) {
        guard !ports.isEmpty,
              !ports.contains(where: { $0.calloutDevice == selectedSerialPortID }) else {
            return
        }
        selectedSerialPortID = ports.first?.calloutDevice ?? ""
    }

    private func serialCapturePreviewText(
        _ capture: TimeCardSerialCapture
    ) -> String {
        guard !capture.text.isEmpty else {
            return "No bytes arrived during the preview window."
        }
        let limit = 1800
        if capture.text.count <= limit {
            return capture.text
        }
        return String(capture.text.prefix(limit)) + "\n[preview truncated]"
    }

    private func uartCapturePreviewText(
        _ capture: TimeCardUARTCapture
    ) -> String {
        guard !capture.data.isEmpty else {
            return "No bytes arrived during the hardware UART capture window."
        }
        let scalars = Array(capture.text.unicodeScalars)
        let printable = scalars.filter { scalar in
            (scalar.value >= 0x20 && scalar.value <= 0x7e) ||
                scalar.value == 0x0a ||
                scalar.value == 0x0d ||
                scalar.value == 0x09
        }.count
        if !scalars.isEmpty && printable * 4 >= scalars.count * 3 {
            let limit = 2400
            if capture.text.count <= limit {
                return capture.text
            }
            return String(capture.text.prefix(limit)) + "\n[preview truncated]"
        }
        return capture.hexDumpLines.prefix(64).joined(separator: "\n")
    }

    private func saveUARTCapture(_ capture: TimeCardUARTCapture) {
        let panel = NSSavePanel()
        panel.title = "Save Hardware UART Capture"
        panel.nameFieldStringValue =
            "TimeCardMacOS-UART-\(capture.port.rawValue)-"
                + "\(uartCaptureTimestampForFilename).bin"
        panel.allowedContentTypes = [.data]
        panel.canCreateDirectories = true

        guard panel.runModal() == .OK, let destinationURL = panel.url else {
            uartCaptureSaveMessage = "UART capture save canceled."
            return
        }

        do {
            try Data(capture.data).write(to: destinationURL, options: .atomic)
            uartCaptureSaveMessage =
                "Saved UART capture to \(destinationURL.path)."
            NSWorkspace.shared.activateFileViewerSelecting([destinationURL])
        } catch {
            uartCaptureSaveMessage =
                "UART capture save failed: \(error.localizedDescription)"
        }
    }

    private var uartCaptureTimestampForFilename: String {
        let formatter = DateFormatter()
        formatter.calendar = Calendar(identifier: .gregorian)
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        return formatter.string(from: Date())
    }

    private var uartHeaderText: String {
        "The macOS Control Center now enumerates native serial devices through "
            + "IOKit and can read the Time Card FPGA UART streams through "
            + "DriverKit ABI v8/v9 when the active driver advertises UART capability."
    }

    private func sendUBXPoll(_ poll: TimeCardUBXPoll) {
        pendingUBXAutoload = true
        monitor.writeUARTAndRead(
            port: selectedUARTPort,
            bytes: poll.packet,
            label: "UBX \(poll.label) poll"
        )
        ubxInputText = poll.hexString
        ubxMessage = "Sent \(poll.label) poll bytes and waiting for response."
    }

    private func uartReadPreviewText(
        _ transfer: TimeCardUARTReadResult
    ) -> String {
        guard !transfer.data.isEmpty else {
            return "No bytes arrived during the hardware UART read window."
        }
        let scalars = Array(transfer.text.unicodeScalars)
        let printable = scalars.filter { scalar in
            (scalar.value >= 0x20 && scalar.value <= 0x7e) ||
                scalar.value == 0x0a ||
                scalar.value == 0x0d ||
                scalar.value == 0x09
        }.count
        if !scalars.isEmpty && printable * 4 >= scalars.count * 3 {
            let limit = 1800
            if transfer.text.count <= limit {
                return transfer.text
            }
            return String(transfer.text.prefix(limit)) + "\n[preview truncated]"
        }
        return transfer.hexDumpLines.prefix(32).joined(separator: "\n")
    }

    private var decodedNMEASentences: [NMEASentence] {
        nmeaText.split(whereSeparator: \.isNewline)
            .compactMap { NMEASentence.parse(String($0)) }
    }

    private var decodedUBXFrames: [TimeCardUBXFrame] {
        TimeCardUBXFrame.parseFrames(from: ubxInputBytes)
    }

    private var ubxInputBytes: [UInt8] {
        if let bytes = parseHexBytes(ubxInputText), !bytes.isEmpty {
            return bytes
        }
        return Array(ubxInputText.utf8)
    }

    private var nmeaDecodeSummary: String {
        let sentences = decodedNMEASentences
        guard !sentences.isEmpty else {
            return "No decoded sentences"
        }
        let valid = sentences.filter(\.checksumValid).count
        let unchecked = sentences.filter { !$0.raw.contains("*") }.count
        return "\(sentences.count) decoded, \(valid) checksum OK, \(unchecked) unchecked"
    }

    private var ubxDecodeSummary: String {
        let frames = decodedUBXFrames
        guard !frames.isEmpty else {
            return "No decoded UBX frames"
        }
        let valid = frames.filter(\.checksumValid).count
        return "\(frames.count) frame(s), \(valid) checksum OK"
    }

    private var receiverMixedMessages: [ReceiverStreamMessage] {
        let bytes = receiverMixedStreamBytes
        let parsed = ReceiverStreamMessage.parse(from: bytes)
        if !parsed.isEmpty {
            return parsed
        }

        let nmeaFallback = decodedNMEASentences.enumerated().map {
            ReceiverStreamMessage(sentence: $0.element, index: $0.offset)
        }
        let ubxFallback = decodedUBXFrames.enumerated().map {
            ReceiverStreamMessage(frame: $0.element, index: $0.offset)
        }
        return nmeaFallback + ubxFallback
    }

    private var receiverMixedStreamBytes: [UInt8] {
        switch receiverSource {
        case "Hardware capture": return monitor.uartCapture?.data ?? []
        case "Hardware read": return monitor.uartReadResult?.data ?? []
        case "Serial preview": return monitor.serialCapture?.data ?? []
        case "UBX input": return ubxInputBytes
        case "NMEA input": return Array(nmeaText.utf8)
        default: break
        }
        if let capture = monitor.uartCapture, !capture.data.isEmpty {
            return capture.data
        }
        if let capture = monitor.serialCapture, !capture.data.isEmpty {
            return capture.data
        }
        if let read = monitor.uartReadResult, !read.data.isEmpty {
            return read.data
        }
        if !ubxInputText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return ubxInputBytes
        }
        return Array(nmeaText.utf8)
    }

    private var receiverMixedSourceDescription: String {
        if receiverSource != "Automatic" { return receiverSource }
        if let capture = monitor.uartCapture, !capture.data.isEmpty {
            return "Latest Time Card hardware UART capture, \(capture.byteCount) byte(s)."
        }
        if let capture = monitor.serialCapture, !capture.data.isEmpty {
            return "Latest macOS serial preview capture, \(capture.byteCount) byte(s)."
        }
        if let read = monitor.uartReadResult, !read.data.isEmpty {
            return "Latest Time Card hardware UART read, \(read.byteCount) byte(s)."
        }
        if !ubxInputText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return "Current UBX decoder input."
        }
        if !nmeaText.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return "Current NMEA decoder input."
        }
        return "No receiver stream bytes loaded yet."
    }

    private func parseHexBytes(_ text: String) -> [UInt8]? {
        let compact = text
            .replacingOccurrences(of: "0x", with: " ")
            .replacingOccurrences(of: "0X", with: " ")
        let tokens = compact.split { character in
            character.isWhitespace || character == "," || character == ":"
        }
        if tokens.count > 1 {
            var bytes: [UInt8] = []
            for token in tokens {
                guard token.count <= 2,
                      let byte = UInt8(token, radix: 16) else {
                    return nil
                }
                bytes.append(byte)
            }
            return bytes
        }

        let hexOnly = compact.filter { $0.isHexDigit }
        guard !hexOnly.isEmpty, hexOnly.count % 2 == 0 else {
            return nil
        }
        var bytes: [UInt8] = []
        var index = hexOnly.startIndex
        while index < hexOnly.endIndex {
            let next = hexOnly.index(index, offsetBy: 2)
            guard let byte = UInt8(hexOnly[index..<next], radix: 16) else {
                return nil
            }
            bytes.append(byte)
            index = next
        }
        return bytes
    }
}


private struct UBXFrameRow: View {
    let frame: TimeCardUBXFrame

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                Text(frame.messageText)
                    .font(.headline)
                Spacer()
                StatusPill(
                    frame.checksumValid ? "Checksum OK" : "Checksum fail",
                    frame.checksumValid ? .green : .red
                )
            }
            Text("Offset \(frame.offset), length \(frame.length), \(frame.checksumText)")
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)
                .textSelection(.enabled)
            Text(frame.summary)
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            Color.secondary.opacity(0.07),
            in: RoundedRectangle(cornerRadius: 12)
        )
    }
}

private struct ReceiverStreamFact: Identifiable {
    let id: String
    let label: String
    let value: String
}

extension ReceiverStreamChecksumState {
    var color: Color {
        switch self {
        case .ok: .green
        case .failed: .red
        case .missing: .secondary
        case .notChecked: .orange
        }
    }
}

private struct ReceiverMixedStreamView: View {
    let messages: [ReceiverStreamMessage]
    let sourceDescription: String

    private var displayedMessages: ArraySlice<ReceiverStreamMessage> {
        messages.prefix(80)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Text(sourceDescription)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                StatusPill(streamSummaryText, messages.isEmpty ? .secondary : .blue)
            }

            if messages.isEmpty {
                Text(
                    "No mixed receiver messages decoded yet. Capture UART bytes "
                        + "or load paste/capture data into the UBX or NMEA labs."
                )
                .font(.caption)
                .foregroundStyle(.secondary)
            } else {
                LazyVStack(spacing: 8) {
                    ForEach(displayedMessages) { message in
                        ReceiverMixedStreamRow(message: message)
                    }
                }
                if messages.count > displayedMessages.count {
                    Text("Showing first \(displayedMessages.count) of \(messages.count) decoded message(s).")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
    }

    private var streamSummaryText: String {
        guard !messages.isEmpty else { return "No decoded messages" }
        let ubx = messages.filter { $0.protocolName == "UBX" }.count
        let nmea = messages.filter { $0.protocolName == "NMEA" }.count
        let rtcm = messages.filter { $0.protocolName == "RTCM3" }.count
        let failed = messages.filter { $0.checksumState == .failed }.count
        let checksumText = failed == 0 ? "" : ", \(failed) checksum fail"
        return "\(messages.count) decoded, \(ubx) UBX, \(nmea) NMEA, \(rtcm) RTCM3\(checksumText)"
    }
}

private struct ReceiverMixedStreamRow: View {
    let message: ReceiverStreamMessage

    var body: some View {
        VStack(alignment: .leading, spacing: 7) {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text(message.offsetText)
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
                StatusPill(message.protocolName, protocolColor)
                Text(message.name)
                    .font(.headline)
                Spacer()
                Text("\(message.byteCount) B")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
                StatusPill(message.checksumState.label, message.checksumState.color)
            }
            Text(message.summary)
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)
            Text(message.detail)
                .font(.system(.caption2, design: .monospaced))
                .foregroundStyle(.tertiary)
                .lineLimit(2)
                .textSelection(.enabled)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            Color.secondary.opacity(0.07),
            in: RoundedRectangle(cornerRadius: 12)
        )
    }

    private var protocolColor: Color {
        switch message.protocolName {
        case "UBX": .teal
        case "NMEA": .green
        case "RTCM3": .orange
        default: .secondary
        }
    }
}

private struct ReceiverValidationChecklistView: View {
    let nmeaSentences: [NMEASentence]
    let ubxFrames: [TimeCardUBXFrame]
    let mixedMessages: [ReceiverStreamMessage]
    let hardwareUARTAvailable: Bool
    let hardwareUARTWriteAvailable: Bool
    let sourceDescription: String

    private var items: [ReceiverValidationChecklistItem] {
        ReceiverValidationChecklist.items(
            nmeaSentences: nmeaSentences,
            ubxFrames: ubxFrames,
            mixedMessages: mixedMessages,
            hardwareUARTAvailable: hardwareUARTAvailable,
            hardwareUARTWriteAvailable: hardwareUARTWriteAvailable,
            sourceDescription: sourceDescription
        )
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 10) {
                validationSummaryPill(
                    "Verified",
                    items.filter { $0.state == "Verified" }.count,
                    .green
                )
                validationSummaryPill(
                    "Ready",
                    items.filter { $0.state == "Ready" }.count,
                    .teal
                )
                validationSummaryPill(
                    "Missing",
                    items.filter { $0.state == "Missing" }.count,
                    .orange
                )
                validationSummaryPill(
                    "Attention",
                    items.filter { $0.state == "Attention" }.count,
                    .red
                )
                Spacer()
                StatusPill(
                    mixedMessages.isEmpty
                        ? "No stream loaded"
                        : "\(mixedMessages.count) message(s)",
                    mixedMessages.isEmpty ? .secondary : .blue
                )
            }

            VStack(spacing: 9) {
                ForEach(items) { item in
                    ReceiverValidationChecklistRow(item: item)
                }
            }

            Text(
                "Run the UBX polls from the hardware UART panel, capture a "
                    + "bounded sample, then use this checklist to decide which "
                    + "Windows GNSS validation steps are already proven."
            )
            .font(.caption)
            .foregroundStyle(.secondary)
            .textSelection(.enabled)
        }
    }

    private func validationSummaryPill(
        _ label: String,
        _ count: Int,
        _ color: Color
    ) -> some View {
        HStack(spacing: 6) {
            Text("\(count)")
                .font(.caption.weight(.bold))
                .monospacedDigit()
            Text(label)
                .font(.caption)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .foregroundStyle(color)
        .background(color.opacity(0.12), in: Capsule())
        .overlay(Capsule().stroke(color.opacity(0.24), lineWidth: 1))
    }
}

private struct ReceiverValidationChecklistRow: View {
    let item: ReceiverValidationChecklistItem

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: item.symbol)
                .font(.system(size: 19, weight: .semibold))
                .foregroundStyle(item.accent)
                .frame(width: 32, height: 32)
                .background(
                    item.accent.opacity(0.12),
                    in: RoundedRectangle(cornerRadius: 9)
                )

            VStack(alignment: .leading, spacing: 5) {
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text(item.name)
                        .font(.headline)
                    StatusPill(item.state, item.accent)
                    Spacer()
                    Text(item.windowsStep)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Text(item.detail)
                    .font(.caption)
                    .foregroundStyle(.primary)
                    .textSelection(.enabled)

                Text(item.nextAction)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }
        }
        .padding(12)
        .background(
            item.accent.opacity(0.07),
            in: RoundedRectangle(cornerRadius: 14)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(item.accent.opacity(0.18), lineWidth: 1)
        )
    }
}

private struct ReceiverValidationChecklistItem: Identifiable {
    let id: String
    let name: String
    let windowsStep: String
    let state: String
    let detail: String
    let nextAction: String
    let symbol: String

    var accent: Color {
        switch state {
        case "Verified": .green
        case "Ready": .teal
        case "Partial", "Missing": .orange
        case "Attention": .red
        default: .secondary
        }
    }
}

private enum ReceiverValidationChecklist {
    static func items(
        nmeaSentences: [NMEASentence],
        ubxFrames: [TimeCardUBXFrame],
        mixedMessages: [ReceiverStreamMessage],
        hardwareUARTAvailable: Bool,
        hardwareUARTWriteAvailable: Bool,
        sourceDescription: String
    ) -> [ReceiverValidationChecklistItem] {
        let validUBXNames = Set(
            ubxFrames.filter(\.checksumValid).map(\.messageName)
        )
        let validNMEAFormatters = Set(
            nmeaSentences.filter(\.checksumValid).map(\.formatter)
        )
        let nmeaFailures = nmeaSentences.filter {
            $0.raw.contains("*") && !$0.checksumValid
        }
        let rtcmMessages = mixedMessages.filter { $0.protocolName == "RTCM3" }
        let rtcmFailures = rtcmMessages.filter { $0.checksumState == .failed }
        let hasStream = !mixedMessages.isEmpty

        func missingOrWaiting() -> String {
            hasStream ? "Missing" : "Waiting"
        }

        func ubxState(_ names: Set<String>) -> String {
            if !validUBXNames.isDisjoint(with: names) { return "Verified" }
            if hardwareUARTWriteAvailable { return "Ready" }
            return missingOrWaiting()
        }

        func nmeaState(_ formatters: Set<String>) -> String {
            if !validNMEAFormatters.isDisjoint(with: formatters) {
                return "Verified"
            }
            return missingOrWaiting()
        }

        return [
            ReceiverValidationChecklistItem(
                id: "capture-source",
                name: "Capture source",
                windowsStep: "Load receiver stream",
                state: hasStream ? "Verified" :
                    (hardwareUARTAvailable ? "Ready" : "Waiting"),
                detail: sourceDescription,
                nextAction: hasStream
                    ? "Use the decoded messages below to validate receiver state."
                    : "Capture hardware UART or load serial preview, NMEA, or UBX bytes.",
                symbol: "tray.and.arrow.down"
            ),
            ReceiverValidationChecklistItem(
                id: "poll-path",
                name: "UBX poll path",
                windowsStep: "Receiver command channel",
                state: hardwareUARTWriteAvailable ? "Ready" :
                    (hardwareUARTAvailable ? "Partial" : "Waiting"),
                detail: hardwareUARTWriteAvailable
                    ? "Safe UBX poll writes are enabled for the GNSS UART."
                    : "Hardware UART reads are available, but UBX poll writes are not fully enabled.",
                nextAction: "Send MON-VER first, then NAV-PVT, NAV-SAT, NAV-TIMELS, and TIM-TP.",
                symbol: "paperplane"
            ),
            ReceiverValidationChecklistItem(
                id: "mon-ver",
                name: "MON-VER identity",
                windowsStep: "Receiver firmware",
                state: ubxState(["MON-VER"]),
                detail: latestUBXSummary(
                    "MON-VER",
                    frames: ubxFrames,
                    fallback: "No valid UBX MON-VER frame decoded."
                ),
                nextAction: "Poll MON-VER to capture u-blox protocol and firmware identity.",
                symbol: "cpu"
            ),
            ReceiverValidationChecklistItem(
                id: "fix-position",
                name: "Fix and position",
                windowsStep: "NAV-PVT or NMEA fix",
                state: fixState(
                    validUBXNames: validUBXNames,
                    validNMEAFormatters: validNMEAFormatters,
                    hasStream: hasStream
                ),
                detail: latestReceiverSummary(
                    ubxNames: ["NAV-PVT", "NAV-STATUS"],
                    nmeaFormatters: ["GGA", "RMC", "GSA", "GLL", "GNS"],
                    ubxFrames: ubxFrames,
                    nmeaSentences: nmeaSentences,
                    fallback: "No NAV-PVT, NAV-STATUS, GGA, RMC, GSA, GLL, or GNS data decoded."
                ),
                nextAction: "Poll NAV-PVT or load NMEA GGA/RMC/GSA data to prove fix state.",
                symbol: "location.viewfinder"
            ),
            ReceiverValidationChecklistItem(
                id: "satellite-table",
                name: "Satellite table",
                windowsStep: "NAV-SAT or GSV",
                state: satelliteState(
                    validUBXNames: validUBXNames,
                    validNMEAFormatters: validNMEAFormatters,
                    hasStream: hasStream
                ),
                detail: latestReceiverSummary(
                    ubxNames: ["NAV-SAT"],
                    nmeaFormatters: ["GSV", "GGA", "GNS"],
                    ubxFrames: ubxFrames,
                    nmeaSentences: nmeaSentences,
                    fallback: "No NAV-SAT, GSV, GGA, or GNS satellite data decoded."
                ),
                nextAction: "Poll NAV-SAT or load GSV sentences to fill the sky map and satellite table.",
                symbol: "antenna.radiowaves.left.and.right"
            ),
            ReceiverValidationChecklistItem(
                id: "utc-leap",
                name: "NAV-TIMELS UTC/leap",
                windowsStep: "Leap second state",
                state: validUBXNames.contains("NAV-TIMELS")
                    ? "Verified" : nmeaState(["ZDA", "RMC"]),
                detail: latestReceiverSummary(
                    ubxNames: ["NAV-TIMELS", "NAV-TIMEUTC"],
                    nmeaFormatters: ["ZDA", "RMC"],
                    ubxFrames: ubxFrames,
                    nmeaSentences: nmeaSentences,
                    fallback: "No NAV-TIMELS, NAV-TIMEUTC, ZDA, or RMC time data decoded."
                ),
                nextAction: "Poll NAV-TIMELS to prove UTC offset and leap state before discipline.",
                symbol: "calendar.badge.exclamationmark"
            ),
            ReceiverValidationChecklistItem(
                id: "tim-tp",
                name: "TIM-TP time pulse",
                windowsStep: "Timing pulse",
                state: ubxState(["TIM-TP"]),
                detail: latestUBXSummary(
                    "TIM-TP",
                    frames: ubxFrames,
                    fallback: "No valid UBX TIM-TP frame decoded."
                ),
                nextAction: "Poll TIM-TP to validate time-pulse quantization and reference flags.",
                symbol: "waveform.path.ecg"
            ),
            ReceiverValidationChecklistItem(
                id: "nmea-health",
                name: "NMEA sentence health",
                windowsStep: "NMEA checksum scan",
                state: nmeaFailures.isEmpty
                    ? (nmeaSentences.isEmpty ? missingOrWaiting() : "Verified")
                    : "Attention",
                detail: nmeaHealthDetail(
                    sentences: nmeaSentences,
                    failures: nmeaFailures
                ),
                nextAction: "Use valid checksums before trusting NMEA position, date, or satellite summaries.",
                symbol: "checkmark.shield"
            ),
            ReceiverValidationChecklistItem(
                id: "rtcm3-health",
                name: "RTCM3 correction health",
                windowsStep: "Correction stream",
                state: rtcmFailures.isEmpty
                    ? (rtcmMessages.isEmpty ? missingOrWaiting() : "Verified")
                    : "Attention",
                detail: ReceiverStreamDecoder.rtcmSummary(messages: mixedMessages),
                nextAction: "Validate RTCM3 CRC24Q before using correction traffic for survey or rover tests.",
                symbol: "dot.radiowaves.left.and.right"
            ),
        ]
    }

    static func reportText(
        nmeaSentences: [NMEASentence],
        ubxFrames: [TimeCardUBXFrame],
        mixedMessages: [ReceiverStreamMessage],
        hardwareUARTAvailable: Bool,
        hardwareUARTWriteAvailable: Bool,
        sourceDescription: String
    ) -> String {
        var lines = ["Time Card macOS receiver validation checklist"]
        lines.append("Source: \(sourceDescription)")
        for item in items(
            nmeaSentences: nmeaSentences,
            ubxFrames: ubxFrames,
            mixedMessages: mixedMessages,
            hardwareUARTAvailable: hardwareUARTAvailable,
            hardwareUARTWriteAvailable: hardwareUARTWriteAvailable,
            sourceDescription: sourceDescription
        ) {
            lines.append("")
            lines.append("\(item.name): \(item.state)")
            lines.append("Windows step: \(item.windowsStep)")
            lines.append("Detail: \(item.detail)")
            lines.append("Next: \(item.nextAction)")
        }
        return lines.joined(separator: "\n")
    }

    private static func fixState(
        validUBXNames: Set<String>,
        validNMEAFormatters: Set<String>,
        hasStream: Bool
    ) -> String {
        if !validUBXNames.isDisjoint(with: ["NAV-PVT", "NAV-STATUS"]) ||
            !validNMEAFormatters.isDisjoint(
                with: ["GGA", "RMC", "GSA", "GLL", "GNS"]
            ) {
            return "Verified"
        }
        return hasStream ? "Missing" : "Waiting"
    }

    private static func satelliteState(
        validUBXNames: Set<String>,
        validNMEAFormatters: Set<String>,
        hasStream: Bool
    ) -> String {
        if validUBXNames.contains("NAV-SAT") ||
            !validNMEAFormatters.isDisjoint(with: ["GSV", "GGA", "GNS"]) {
            return "Verified"
        }
        return hasStream ? "Missing" : "Waiting"
    }

    private static func latestUBXSummary(
        _ name: String,
        frames: [TimeCardUBXFrame],
        fallback: String
    ) -> String {
        frames.last { $0.checksumValid && $0.messageName == name }?.summary ??
            fallback
    }

    private static func latestReceiverSummary(
        ubxNames: Set<String>,
        nmeaFormatters: Set<String>,
        ubxFrames: [TimeCardUBXFrame],
        nmeaSentences: [NMEASentence],
        fallback: String
    ) -> String {
        if let frame = ubxFrames.last(
            where: { $0.checksumValid && ubxNames.contains($0.messageName) }
        ) {
            return frame.summary
        }
        if let sentence = nmeaSentences.last(
            where: { $0.checksumValid && nmeaFormatters.contains($0.formatter) }
        ) {
            return sentence.summary
        }
        return fallback
    }

    private static func nmeaHealthDetail(
        sentences: [NMEASentence],
        failures: [NMEASentence]
    ) -> String {
        guard !sentences.isEmpty else {
            return "No NMEA sentences decoded."
        }
        let valid = sentences.filter(\.checksumValid).count
        let missing = sentences.filter { !$0.raw.contains("*") }.count
        if !failures.isEmpty {
            return "\(failures.count) checksum failure(s), \(valid) valid, \(missing) without checksum."
        }
        return "\(sentences.count) sentence(s), \(valid) checksum OK, \(missing) without checksum."
    }
}

struct ReceiverStreamSummaryView: View {
    let nmeaSentences: [NMEASentence]
    let ubxFrames: [TimeCardUBXFrame]
    let mixedMessages: [ReceiverStreamMessage]

    private let columns = [
        GridItem(.adaptive(minimum: 220), spacing: 12)
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            LazyVGrid(columns: columns, spacing: 12) {
                MetricCard(
                    title: "Decoded stream",
                    value: decodedStreamValue,
                    detail: decodedStreamDetail,
                    systemImage: "waveform.path.ecg",
                    accent: decodedCount == 0 ? .secondary : .green
                )
                MetricCard(
                    title: "Checksum health",
                    value: checksumHealthValue,
                    detail: checksumHealthDetail,
                    systemImage: "checkmark.shield",
                    accent: checksumHealthAccent
                )
                MetricCard(
                    title: "RTCM3 corrections",
                    value: rtcmValue,
                    detail: rtcmDetail,
                    systemImage: "dot.radiowaves.left.and.right",
                    accent: rtcmAccent
                )
                MetricCard(
                    title: "Receiver version",
                    value: receiverVersionValue,
                    detail: receiverVersionDetail,
                    systemImage: "cpu",
                    accent: receiverVersionFrame == nil ? .secondary : .teal
                )
                MetricCard(
                    title: "Fix and position",
                    value: fixSourceValue,
                    detail: fixDetail,
                    systemImage: "location.viewfinder",
                    accent: fixDetailAvailable ? .green : .orange
                )
                MetricCard(
                    title: "Satellites",
                    value: satelliteValue,
                    detail: satelliteDetail,
                    systemImage: "antenna.radiowaves.left.and.right",
                    accent: satelliteDetailAvailable ? .mint : .secondary
                )
            }

            if facts.isEmpty {
                Text(
                    "No receiver facts decoded yet. Load hardware UART capture, "
                        + "serial preview bytes, paste NMEA, or paste UBX hex."
                )
                .font(.caption)
                .foregroundStyle(.secondary)
            } else {
                VStack(spacing: 8) {
                    ForEach(facts) { fact in
                        InfoRow(label: fact.label, value: fact.value)
                    }
                }
            }

            if !satelliteSignals.isEmpty {
                Divider()
                VStack(alignment: .leading, spacing: 8) {
                    HStack(alignment: .firstTextBaseline) {
                        Label("Satellite signal table", systemImage: "sparkles")
                            .font(.headline)
                        Spacer()
                        StatusPill(
                            "\(satelliteSignals.count) record(s)",
                            .mint
                        )
                    }
                    Text(satelliteSignalSource)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    ReceiverSatelliteSkyMapView(signals: satelliteSignals)
                    ScrollView(.horizontal) {
                        Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 8) {
                            GridRow {
                                satelliteHeader("Source")
                                satelliteHeader("GNSS")
                                satelliteHeader("SV")
                                satelliteHeader("C/N0")
                                satelliteHeader("Elevation")
                                satelliteHeader("Azimuth")
                                satelliteHeader("Use")
                                satelliteHeader("Quality")
                                satelliteHeader("Flags")
                            }
                            Divider()
                                .gridCellColumns(9)
                            ForEach(satelliteSignals) { signal in
                                GridRow {
                                    satelliteCell(signal.source)
                                    satelliteCell(signal.constellation)
                                    satelliteCell(signal.satelliteID)
                                    satelliteCell(signal.cn0Text)
                                    satelliteCell(signal.elevationText)
                                    satelliteCell(signal.azimuthText)
                                    StatusPill(
                                        signal.usedText,
                                        satelliteUseColor(signal.usedInFix)
                                    )
                                    satelliteCell(signal.quality)
                                    satelliteCell(signal.flagsText)
                                }
                            }
                        }
                        .font(.caption.monospacedDigit())
                        .textSelection(.enabled)
                        .padding(12)
                        .background(
                            Color.secondary.opacity(0.07),
                            in: RoundedRectangle(cornerRadius: 12)
                        )
                    }
                }
            }
        }
    }

    private var decodedCount: Int {
        mixedMessages.count
    }

    private var decodedStreamValue: String {
        decodedCount == 0 ? "Waiting" : "\(decodedCount)"
    }

    private var decodedStreamDetail: String {
        ReceiverStreamDecoder.protocolSummary(messages: mixedMessages)
    }

    private var checksumHealthValue: String {
        guard !mixedMessages.isEmpty else { return "Waiting" }
        let failed = mixedMessages.filter { $0.checksumState == .failed }.count
        let checked = mixedMessages.filter { $0.checksumState == .ok }.count
        if failed > 0 {
            return "\(failed) failing"
        }
        if checked > 0 {
            return "Healthy"
        }
        return "Unchecked"
    }

    private var checksumHealthDetail: String {
        ReceiverStreamDecoder.checksumSummary(messages: mixedMessages)
    }

    private var checksumHealthAccent: Color {
        guard !mixedMessages.isEmpty else { return .secondary }
        if mixedMessages.contains(where: { $0.checksumState == .failed }) {
            return .red
        }
        if mixedMessages.contains(where: { $0.checksumState == .ok }) {
            return .green
        }
        return .orange
    }

    private var rtcmMessages: [ReceiverStreamMessage] {
        mixedMessages.filter { $0.protocolName == "RTCM3" }
    }

    private var rtcmValue: String {
        rtcmMessages.isEmpty ? "None" : "\(rtcmMessages.count)"
    }

    private var rtcmDetail: String {
        ReceiverStreamDecoder.rtcmSummary(messages: mixedMessages)
    }

    private var rtcmAccent: Color {
        guard !rtcmMessages.isEmpty else { return .secondary }
        if rtcmMessages.contains(where: { $0.checksumState == .failed }) {
            return .red
        }
        if rtcmMessages.contains(where: { $0.checksumState == .ok }) {
            return .orange
        }
        return .secondary
    }

    private var receiverVersionFrame: TimeCardUBXFrame? {
        ubxFrames.last {
            $0.checksumValid && $0.messageName == "MON-VER"
        }
    }

    private var receiverVersionValue: String {
        receiverVersionFrame == nil ? "Unknown" : "MON-VER"
    }

    private var receiverVersionDetail: String {
        receiverVersionFrame?.summary ?? "No UBX MON-VER frame decoded."
    }

    private var fixFrame: TimeCardUBXFrame? {
        ubxFrames.last {
            $0.checksumValid &&
                ($0.messageName == "NAV-PVT" || $0.messageName == "NAV-STATUS")
        }
    }

    private var fixSentence: NMEASentence? {
        nmeaSentences.last {
            $0.formatter == "GGA" || $0.formatter == "RMC" ||
                $0.formatter == "GSA" || $0.formatter == "GLL" ||
                $0.formatter == "GNS"
        }
    }

    private var fixSourceValue: String {
        if fixFrame != nil { return "UBX" }
        if fixSentence != nil { return "NMEA" }
        return "No fix"
    }

    private var fixDetail: String {
        fixFrame?.summary ??
            fixSentence?.summary ??
            "No NAV-PVT, NAV-STATUS, GGA, RMC, GSA, GLL, or GNS data decoded."
    }

    private var fixDetailAvailable: Bool {
        fixFrame != nil || fixSentence != nil
    }

    private var satelliteFrame: TimeCardUBXFrame? {
        ubxFrames.last {
            $0.checksumValid && $0.messageName == "NAV-SAT"
        }
    }

    private var satelliteSentence: NMEASentence? {
        nmeaSentences.last {
            $0.formatter == "GSV" || $0.formatter == "GGA" ||
                $0.formatter == "GNS"
        }
    }

    private var satelliteValue: String {
        if let frame = satelliteFrame {
            return frame.messageName
        }
        if let sentence = satelliteSentence {
            return sentence.label
        }
        return "Unknown"
    }

    private var satelliteDetail: String {
        satelliteFrame?.summary ??
            satelliteSentence?.summary ??
            "No NAV-SAT, GSV, GGA, or GNS satellite data decoded."
    }

    private var satelliteDetailAvailable: Bool {
        satelliteFrame != nil || satelliteSentence != nil
    }

    private var satelliteSignals: [ReceiverSatelliteSignal] {
        ReceiverStreamDecoder.satelliteSignals(
            nmeaSentences: nmeaSentences,
            ubxFrames: ubxFrames
        )
    }

    private var satelliteSignalSource: String {
        ReceiverStreamDecoder.satelliteSource(
            nmeaSentences: nmeaSentences,
            ubxFrames: ubxFrames
        )
    }

    private var timingFrame: TimeCardUBXFrame? {
        ubxFrames.last {
            $0.checksumValid && $0.messageName == "TIM-TP"
        }
    }

    private var timingSentence: NMEASentence? {
        nmeaSentences.last {
            $0.formatter == "ZDA" || $0.formatter == "RMC" ||
                $0.formatter == "GLL" || $0.formatter == "GNS"
        }
    }

    private var facts: [ReceiverStreamFact] {
        var rows: [ReceiverStreamFact] = []
        if let receiverVersionFrame {
            rows.append(
                ReceiverStreamFact(
                    id: "receiver-version",
                    label: "Receiver version",
                    value: receiverVersionFrame.summary
                )
            )
        }
        if let fixFrame {
            rows.append(
                ReceiverStreamFact(
                    id: "ubx-fix",
                    label: "UBX fix",
                    value: fixFrame.summary
                )
            )
        } else if let fixSentence {
            rows.append(
                ReceiverStreamFact(
                    id: "nmea-fix",
                    label: "NMEA fix",
                    value: fixSentence.summary
                )
            )
        }
        if let satelliteFrame {
            rows.append(
                ReceiverStreamFact(
                    id: "ubx-satellites",
                    label: "UBX satellites",
                    value: satelliteFrame.summary
                )
            )
        } else if let satelliteSentence {
            rows.append(
                ReceiverStreamFact(
                    id: "nmea-satellites",
                    label: "NMEA satellites",
                    value: satelliteSentence.summary
                )
            )
        }
        if let timingFrame {
            rows.append(
                ReceiverStreamFact(
                    id: "ubx-timing",
                    label: "UBX timing",
                    value: timingFrame.summary
                )
            )
        } else if let timingSentence {
            rows.append(
                ReceiverStreamFact(
                    id: "nmea-timing",
                    label: "NMEA timing",
                    value: timingSentence.summary
                )
            )
        }
        if !mixedMessages.isEmpty {
            rows.append(
                ReceiverStreamFact(
                    id: "checksum-health",
                    label: "Checksum health",
                    value: checksumHealthDetail
                )
            )
        }
        if !rtcmMessages.isEmpty {
            rows.append(
                ReceiverStreamFact(
                    id: "rtcm3-corrections",
                    label: "RTCM3 corrections",
                    value: rtcmDetail
                )
            )
        }
        return rows
    }

    private func satelliteHeader(_ text: String) -> some View {
        Text(text)
            .fontWeight(.semibold)
            .foregroundStyle(.secondary)
    }

    private func satelliteCell(_ text: String) -> some View {
        Text(text)
            .lineLimit(1)
    }

    private func satelliteUseColor(_ usedInFix: Bool?) -> Color {
        switch usedInFix {
        case true:
            return .green
        case false:
            return .secondary
        case nil:
            return .orange
        }
    }
}

private struct ReceiverSatelliteSkyMapView: View {
    let signals: [ReceiverSatelliteSignal]

    private var plottableSignals: [ReceiverSatelliteSignal] {
        signals.filter {
            $0.elevation != nil && $0.azimuth != nil
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                Label("Satellite sky map", systemImage: "scope")
                    .font(.headline)
                Spacer()
                StatusPill("\(plottableSignals.count) plotted", .blue)
            }
            Text("North-up polar view: zenith is center, horizon is the outer ring, marker size follows C/N0, and a bright outline means the receiver used the satellite in the fix.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

            if plottableSignals.isEmpty {
                Text("Satellite records do not include elevation and azimuth yet.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .padding(12)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(
                        Color.secondary.opacity(0.07),
                        in: RoundedRectangle(cornerRadius: 12)
                    )
            } else {
                GeometryReader { geometry in
                    let size = min(geometry.size.width, geometry.size.height)
                    ZStack {
                        ForEach([0.25, 0.50, 0.75, 1.00], id: \.self) { factor in
                            Circle()
                                .stroke(
                                    Color.secondary.opacity(factor == 1.00 ? 0.45 : 0.22),
                                    style: StrokeStyle(
                                        lineWidth: factor == 1.00 ? 1.4 : 0.8,
                                        dash: factor == 1.00 ? [] : [4, 4]
                                    )
                                )
                                .frame(
                                    width: size * factor,
                                    height: size * factor
                                )
                        }
                        skyMapAxis(size: size, horizontal: true)
                        skyMapAxis(size: size, horizontal: false)

                        Text("N")
                            .font(.caption.bold())
                            .foregroundStyle(.secondary)
                            .position(x: size / 2, y: 10)
                        Text("E")
                            .font(.caption.bold())
                            .foregroundStyle(.secondary)
                            .position(x: size - 10, y: size / 2)
                        Text("S")
                            .font(.caption.bold())
                            .foregroundStyle(.secondary)
                            .position(x: size / 2, y: size - 10)
                        Text("W")
                            .font(.caption.bold())
                            .foregroundStyle(.secondary)
                            .position(x: 10, y: size / 2)

                        ForEach(plottableSignals) { signal in
                            satelliteMarker(signal)
                                .position(point(for: signal, size: size))
                        }
                    }
                    .frame(width: size, height: size)
                    .position(
                        x: geometry.size.width / 2,
                        y: geometry.size.height / 2
                    )
                }
                .frame(minHeight: 260)
                .padding(12)
                .background(
                    LinearGradient(
                        colors: [
                            Color.accentColor.opacity(0.11),
                            Color.secondary.opacity(0.05),
                        ],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    ),
                    in: RoundedRectangle(cornerRadius: 16)
                )

                skyMapLegend
            }
        }
    }

    private func skyMapAxis(size: CGFloat, horizontal: Bool) -> some View {
        Path { path in
            if horizontal {
                path.move(to: CGPoint(x: 16, y: size / 2))
                path.addLine(to: CGPoint(x: size - 16, y: size / 2))
            } else {
                path.move(to: CGPoint(x: size / 2, y: 16))
                path.addLine(to: CGPoint(x: size / 2, y: size - 16))
            }
        }
        .stroke(Color.secondary.opacity(0.18), lineWidth: 0.8)
    }

    private func satelliteMarker(_ signal: ReceiverSatelliteSignal) -> some View {
        ZStack {
            Circle()
                .fill(constellationColor(signal.constellation).opacity(0.88))
                .frame(
                    width: markerSize(signal),
                    height: markerSize(signal)
                )
                .overlay {
                    Circle()
                        .stroke(
                            signal.usedInFix == true
                                ? Color.white.opacity(0.95)
                                : Color.black.opacity(0.25),
                            lineWidth: signal.usedInFix == true ? 2.2 : 0.8
                        )
                }
                .shadow(
                    color: constellationColor(signal.constellation).opacity(0.35),
                    radius: signal.usedInFix == true ? 5 : 2
                )
            Text(signal.satelliteID)
                .font(.system(size: 9, weight: .bold, design: .rounded))
                .foregroundStyle(.white)
                .minimumScaleFactor(0.7)
                .lineLimit(1)
        }
        .help(
            "\(signal.constellation) SV \(signal.satelliteID), "
                + "\(signal.elevationText) elevation, "
                + "\(signal.azimuthText) azimuth, "
                + "\(signal.cn0Text), \(signal.usedText)"
        )
    }

    private var skyMapLegend: some View {
        ScrollView(.horizontal) {
            HStack(spacing: 8) {
                ForEach(constellationsInUse, id: \.self) { constellation in
                    HStack(spacing: 5) {
                        Circle()
                            .fill(constellationColor(constellation))
                            .frame(width: 8, height: 8)
                        Text(constellation)
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(
                        Color.secondary.opacity(0.07),
                        in: Capsule()
                    )
                }
            }
        }
    }

    private var constellationsInUse: [String] {
        Array(Set(plottableSignals.map(\.constellation))).sorted()
    }

    private func point(
        for signal: ReceiverSatelliteSignal,
        size: CGFloat
    ) -> CGPoint {
        let elevation = min(90, max(0, signal.elevation ?? 0))
        let azimuth = Double(signal.azimuth ?? 0) * .pi / 180.0
        let radius = (size / 2) - 24
        let normalizedRadius = CGFloat(90 - elevation) / 90.0
        return CGPoint(
            x: size / 2 + CGFloat(sin(azimuth)) * radius * normalizedRadius,
            y: size / 2 - CGFloat(cos(azimuth)) * radius * normalizedRadius
        )
    }

    private func markerSize(_ signal: ReceiverSatelliteSignal) -> CGFloat {
        let cn0 = min(55, max(10, signal.cn0 ?? 18))
        return 12 + CGFloat(cn0 - 10) * 0.22
    }

    private func constellationColor(_ constellation: String) -> Color {
        switch constellation {
        case "GPS": .blue
        case "GLONASS": .pink
        case "Galileo": .purple
        case "BeiDou": .orange
        case "QZSS": .mint
        case "NavIC": .brown
        case "SBAS": .gray
        case "Mixed GNSS": .cyan
        default: .teal
        }
    }
}

private struct SerialPortCard: View {
    let port: TimeCardSerialPort

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                Text(port.displayName)
                    .font(.headline)
                Spacer()
                StatusPill(port.bsdType ?? "Serial", .teal)
            }

            InfoRow(label: "Callout", value: port.calloutDevice)
            InfoRow(label: "Dial-in", value: port.dialinDevice ?? "Unavailable")
            InfoRow(label: "TTY", value: port.ttyDevice ?? "Unavailable")
        }
        .padding(14)
        .background(Color.secondary.opacity(0.07), in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(Color.teal.opacity(0.18), lineWidth: 1)
        )
    }
}


private struct NMEASentenceRow: View {
    let sentence: NMEASentence

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(sentence.label)
                    .font(.headline)
                Spacer()
                StatusPill(checksumText, checksumColor)
            }

            Text(sentence.summary)
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

            Text(sentence.raw)
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(.tertiary)
                .lineLimit(2)
                .textSelection(.enabled)
        }
        .padding(12)
        .background(Color.secondary.opacity(0.07), in: RoundedRectangle(cornerRadius: 12))
    }

    private var checksumText: String {
        guard let expected = sentence.expectedChecksum else {
            return "No checksum"
        }
        return expected == sentence.computedChecksum
            ? "Checksum OK"
            : String(
                format: "Expected 0x%02x, got 0x%02x",
                expected,
                sentence.computedChecksum
            )
    }

    private var checksumColor: Color {
        guard sentence.raw.contains("*") else {
            return .secondary
        }
        return sentence.checksumValid ? .green : .red
    }
}

private struct I2CAndLEDView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var i2cAddressText = "0x70"
    @State private var i2cSubaddressLength: UInt32 = 0
    @State private var i2cSubaddressText = ""
    @State private var i2cReadLengthText = "1"
    @State private var i2cMuxChannelMask: UInt32 = 0
    @State private var selectedLED: TimeCardLEDKind = .gnss1
    @State private var ledRedText = "145"
    @State private var ledGreenText = "64"
    @State private var ledBlueText = "0"
    @State private var ledCurrentText = "96"
    @State private var i2cFormMessage = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

                if let snapshot = monitor.snapshot {
                    HStack(spacing: 14) {
                        MetricCard(
                            title: "I2C controller",
                            value: i2cControllerValue,
                            detail: i2cControllerDetail,
                            systemImage: "point.3.connected.trianglepath.dotted",
                            accent: monitor.i2cStatus?.isEnabled == true
                                ? .green : .orange
                        )
                        MetricCard(
                            title: "Mux branch",
                            value: muxValue,
                            detail: muxDetail,
                            systemImage: "switch.2",
                            accent: monitor.i2cMux?.isPresent == true
                                ? .cyan : .secondary
                        )
                        MetricCard(
                            title: "LED readback",
                            value: ledValue,
                            detail: "\(presentLEDCount) fitted LED(s)",
                            systemImage: "lightbulb.led",
                            accent: presentLEDCount > 0 ? .yellow : .secondary
                        )
                    }

                    ControlCenterPanel(
                        title: "I2C controller registers",
                        subtitle: "Read-only DriverKit ABI snapshot"
                    ) {
                        if let status = monitor.i2cStatus {
                            InfoRow(
                                label: "Capability",
                                value: snapshot.capabilityNames.contains("I2C")
                                    ? "Available" : "Not present"
                            )
                            InfoRow(
                                label: "BAR offset",
                                value: TimeCardFormatting.hex(status.offset)
                            )
                            InfoRow(
                                label: "Flags",
                                value: i2cFlagSummary(status)
                            )
                            InfoRow(
                                label: "Control",
                                value: TimeCardFormatting.byteHex(status.control)
                            )
                            InfoRow(
                                label: "Status",
                                value: TimeCardFormatting.byteHex(status.status)
                            )
                            InfoRow(
                                label: "Interrupts",
                                value: String(
                                    format: "status 0x%02x, enable 0x%02x",
                                    status.interruptStatus & 0xff,
                                    status.interruptEnable & 0xff
                                )
                            )
                            InfoRow(
                                label: "FIFO",
                                value: "TX \(status.txFifoOccupancy), RX \(status.rxFifoOccupancy)"
                            )
                            InfoRow(
                                label: "Known devices",
                                value: status.knownDeviceNames.isEmpty
                                    ? "None" : status.knownDeviceNames.joined(separator: ", ")
                            )
                        } else {
                            ContentUnavailableView(
                                "I2C status unavailable",
                                systemImage: "exclamationmark.triangle",
                                description: Text(monitor.i2cMessage)
                            )
                        }
                    }

                    ControlCenterPanel(
                        title: "I2C bus laboratory",
                        subtitle: "Bounded reads and bus scan through the DriverKit ABI"
                    ) {
                        VStack(alignment: .leading, spacing: 14) {
                            HStack(alignment: .bottom, spacing: 12) {
                                VStack(alignment: .leading, spacing: 6) {
                                    Text("Mux branch")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    Picker("", selection: $i2cMuxChannelMask) {
                                        Text("None 0x00").tag(UInt32(0))
                                        Text("Channel 0x01").tag(UInt32(1))
                                        Text("Channel 0x02").tag(UInt32(2))
                                        Text("Channel 0x04").tag(UInt32(4))
                                        Text("Channel 0x08").tag(UInt32(8))
                                        Text("All 0x0f").tag(UInt32(15))
                                    }
                                    .labelsHidden()
                                    .frame(width: 142)
                                }

                                VStack(alignment: .leading, spacing: 6) {
                                    Text("Current")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    Text(muxValue)
                                        .font(.system(.body, design: .monospaced))
                                        .padding(.horizontal, 10)
                                        .padding(.vertical, 6)
                                        .frame(width: 84, alignment: .leading)
                                        .background(
                                            Color.secondary.opacity(0.08),
                                            in: RoundedRectangle(cornerRadius: 8)
                                        )
                                        .textSelection(.enabled)
                                }

                                Button("Apply Mux") {
                                    i2cFormMessage = ""
                                    monitor.setI2CMux(channelMask: i2cMuxChannelMask)
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(i2cActionDisabled)

                                Button("Use Current") {
                                    i2cMuxChannelMask =
                                        monitor.i2cMux?.channelMask ?? 0
                                }
                                .buttonStyle(.bordered)
                                .disabled(monitor.i2cMux == nil)

                                Spacer()
                            }

                            Text(
                                "Changing the mux branch controls which downstream "
                                    + "devices the read and scan tools can see. "
                                    + "Sensor and LED refreshes restore their own "
                                    + "temporary branch selection."
                            )
                            .font(.caption)
                            .foregroundStyle(.secondary)

                            HStack(alignment: .bottom, spacing: 12) {
                                VStack(alignment: .leading, spacing: 6) {
                                    Text("Address")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    TextField("0x70", text: $i2cAddressText)
                                        .font(.system(.body, design: .monospaced))
                                        .textFieldStyle(.roundedBorder)
                                        .frame(width: 82)
                                }

                                VStack(alignment: .leading, spacing: 6) {
                                    Text("Subaddress")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    Picker("", selection: $i2cSubaddressLength) {
                                        Text("None").tag(UInt32(0))
                                        Text("1 byte").tag(UInt32(1))
                                        Text("2 bytes").tag(UInt32(2))
                                    }
                                    .labelsHidden()
                                    .frame(width: 104)
                                }

                                VStack(alignment: .leading, spacing: 6) {
                                    Text("Register")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    TextField("0x00", text: $i2cSubaddressText)
                                        .font(.system(.body, design: .monospaced))
                                        .textFieldStyle(.roundedBorder)
                                        .frame(width: 92)
                                        .disabled(i2cSubaddressLength == 0)
                                }

                                VStack(alignment: .leading, spacing: 6) {
                                    Text("Length")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    TextField("1", text: $i2cReadLengthText)
                                        .font(.system(.body, design: .monospaced))
                                        .textFieldStyle(.roundedBorder)
                                        .frame(width: 68)
                                }

                                Spacer()

                                Button("Read") {
                                    performI2CRead()
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(i2cActionDisabled)

                                Button("Scan Bus") {
                                    i2cFormMessage = ""
                                    monitor.scanI2CBus()
                                }
                                .buttonStyle(.bordered)
                                .disabled(i2cActionDisabled)
                            }

                            Text(
                                "Reads are limited to 1 through 255 bytes and "
                                    + "valid 7-bit I2C addresses 0x08 through 0x77."
                            )
                            .font(.caption)
                            .foregroundStyle(.secondary)

                            if !i2cFormMessage.isEmpty {
                                Text(i2cFormMessage)
                                    .font(.caption)
                                    .foregroundStyle(.red)
                                    .textSelection(.enabled)
                            }

                            if !monitor.i2cOperationMessage.isEmpty {
                                Text(monitor.i2cOperationMessage)
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                    .textSelection(.enabled)
                            }

                            if let transfer = monitor.i2cTransfer {
                                I2CTransferResultView(transfer: transfer)
                            }

                            if !monitor.i2cScanResults.isEmpty {
                                I2CScanResultsView(results: monitor.i2cScanResults)
                            }
                        }
                    }

                    ControlCenterPanel(
                        title: "LED color laboratory",
                        subtitle: "Manual GNSS and SMA LED control with verified readback"
                    ) {
                        VStack(alignment: .leading, spacing: 14) {
                            HStack(alignment: .bottom, spacing: 12) {
                                VStack(alignment: .leading, spacing: 6) {
                                    Text("LED")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    Picker("", selection: $selectedLED) {
                                        ForEach(TimeCardLEDKind.allCases) { led in
                                            Text(led.label).tag(led)
                                        }
                                    }
                                    .labelsHidden()
                                    .frame(width: 116)
                                }

                                LEDByteField(label: "Red", text: $ledRedText)
                                LEDByteField(label: "Green", text: $ledGreenText)
                                LEDByteField(label: "Blue", text: $ledBlueText)
                                LEDByteField(label: "Current", text: $ledCurrentText)

                                Circle()
                                    .fill(ledPreviewColor)
                                    .frame(width: 24, height: 24)
                                    .overlay(
                                        Circle().stroke(
                                            .white.opacity(0.35),
                                            lineWidth: 1
                                        )
                                    )
                                    .padding(.bottom, 6)

                                Spacer()

                                Button("Load Readback") {
                                    loadSelectedLEDReadback()
                                }
                                .buttonStyle(.bordered)
                                .disabled(monitor.ledStates.isEmpty)

                                Button("Apply LED") {
                                    performLEDSet()
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(ledActionDisabled)
                            }

                            Text(
                                "Values are validated as 0 through 255 before "
                                    + "selector 7 is called. Current 0 requests "
                                    + "the driver default maximum."
                            )
                            .font(.caption)
                            .foregroundStyle(.secondary)

                            Divider()

                            LEDPolicyPresetGrid(
                                snapshot: monitor.snapshot,
                                smaRoutes: monitor.smaRoutes,
                                ledActionDisabled: ledActionDisabled,
                                smaPolicyDisabled: ledActionDisabled ||
                                    monitor.snapshot?.capabilityNames.contains("SMA") != true,
                                applyGNSS: {
                                    i2cFormMessage = ""
                                    monitor.applyGNSSLEDPolicy()
                                },
                                applySMA: {
                                    i2cFormMessage = ""
                                    monitor.applySMALEDPolicy()
                                },
                                applyAll: {
                                    i2cFormMessage = ""
                                    monitor.applyAllLEDPolicy()
                                }
                            )
                        }
                    }

                    ControlCenterPanel(
                        title: "LED controller readback",
                        subtitle: "GNSS and SMA LED states from the I2C LED driver"
                    ) {
                        if monitor.ledStates.isEmpty {
                            ContentUnavailableView(
                                "No LED states sampled",
                                systemImage: "lightbulb.slash",
                                description: Text(monitor.i2cMessage)
                            )
                        } else {
                            LazyVGrid(
                                columns: [
                                    GridItem(.adaptive(minimum: 210), spacing: 12)
                                ],
                                spacing: 12
                            ) {
                                ForEach(monitor.ledStates) { led in
                                    LEDReadbackCard(led: led)
                                }
                            }
                        }
                    }

                    ControlCenterPanel(
                        title: "Feature coverage",
                        subtitle: "Windows I2C and LED workspace parity"
                    ) {
                        VStack(spacing: 10) {
                            FeatureRow(
                                name: "AXI IIC health",
                                state: monitor.i2cStatus == nil ? "Unavailable" : "Live",
                                note: "Control, status, interrupt, FIFO, and known-device fields are read through ABI v5."
                            )
                            FeatureRow(
                                name: "Mux query",
                                state: monitor.i2cMux?.isPresent == true
                                    ? "Live" : "Unavailable",
                                note: "Current mux channel mask is read and can be set through selector 12."
                            )
                            FeatureRow(
                                name: "RGB subsystem LEDs",
                                state: presentLEDCount > 0 ? "Live" : "Unavailable",
                                note: "Readback, manual setting, and GNSS/SMA policy presets use selectors 6 and 7."
                            )
                            FeatureRow(
                                name: "Known-device scan",
                                state: monitor.i2cScanResults.isEmpty
                                    ? "Ready" : "Live",
                                note: "The app can scan valid 7-bit bus addresses through selector 9."
                            )
                            FeatureRow(
                                name: "Arbitrary I2C reads",
                                state: "Live",
                                note: "Bounded app reads use selector 10 with address, subaddress, and length validation."
                            )
                        }
                    }

                    if !monitor.i2cMessage.isEmpty {
                        Text(monitor.i2cMessage)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .textSelection(.enabled)
                    }
                } else {
                    MonitorUnavailableView()
                }
            }
            .padding(24)
        }
    }

    private var i2cControllerValue: String {
        guard let status = monitor.i2cStatus else { return "Unavailable" }
        if status.isBusBusy { return "Bus busy" }
        if status.isEnabled { return "Enabled" }
        return status.isPresent ? "Present" : "Not present"
    }

    private var i2cControllerDetail: String {
        guard let status = monitor.i2cStatus else {
            return "Controller status has not sampled yet."
        }
        return "status \(TimeCardFormatting.byteHex(status.status)), FIFO TX \(status.txFifoOccupancy) RX \(status.rxFifoOccupancy)"
    }

    private var muxValue: String {
        guard let mux = monitor.i2cMux else { return "Unavailable" }
        return mux.isPresent
            ? TimeCardFormatting.byteHex(mux.channelMask)
            : "Not present"
    }

    private var muxDetail: String {
        guard let mux = monitor.i2cMux else {
            return "Mux query has not sampled yet."
        }
        return String(
            format: "controller 0x%02x, interrupts 0x%02x",
            mux.controllerStatus & 0xff,
            mux.interruptStatus & 0xff
        )
    }

    private var ledValue: String {
        guard !monitor.ledStates.isEmpty else { return "Unavailable" }
        return "\(presentLEDCount)/\(monitor.ledStates.count) present"
    }

    private var presentLEDCount: Int {
        monitor.ledStates.filter(\.isPresent).count
    }

    private func i2cFlagSummary(_ status: TimeCardI2CStatusSnapshot) -> String {
        var flags: [String] = []
        flags.append(status.isPresent ? "present" : "not present")
        if status.isEnabled { flags.append("enabled") }
        if status.isBusBusy { flags.append("bus busy") }
        if status.isReceiveEmpty { flags.append("RX empty") }
        if status.isTransmitEmpty { flags.append("TX empty") }
        return flags.joined(separator: ", ")
    }

    private var i2cActionDisabled: Bool {
        monitor.i2cOperationInProgress ||
            monitor.snapshot?.capabilityNames.contains("I2C") != true
    }

    private var ledActionDisabled: Bool {
        monitor.i2cOperationInProgress ||
            monitor.snapshot?.capabilityNames.contains("LEDs") != true
    }

    private var ledPreviewColor: Color {
        guard let red = try? parseByte(ledRedText, field: "LED red"),
              let green = try? parseByte(ledGreenText, field: "LED green"),
              let blue = try? parseByte(ledBlueText, field: "LED blue") else {
            return .secondary.opacity(0.35)
        }
        return Color(
            red: Double(red) / 255.0,
            green: Double(green) / 255.0,
            blue: Double(blue) / 255.0
        )
    }

    private func loadSelectedLEDReadback() {
        guard let state = monitor.ledStates.first(where: { $0.led == selectedLED }) else {
            i2cFormMessage = "\(selectedLED.label) has no readback sample yet."
            return
        }
        ledRedText = "\(state.red)"
        ledGreenText = "\(state.green)"
        ledBlueText = "\(state.blue)"
        ledCurrentText = "\(state.globalCurrent)"
        i2cFormMessage = "Loaded \(state.led.label) readback into the LED editor."
    }

    private func performLEDSet() {
        do {
            let red = try parseByte(ledRedText, field: "LED red")
            let green = try parseByte(ledGreenText, field: "LED green")
            let blue = try parseByte(ledBlueText, field: "LED blue")
            let current = try parseByte(ledCurrentText, field: "LED current")
            i2cFormMessage = ""
            monitor.setLED(
                led: selectedLED,
                red: red,
                green: green,
                blue: blue,
                globalCurrent: current
            )
        } catch {
            i2cFormMessage = error.localizedDescription
        }
    }

    private func performI2CRead() {
        do {
            let address = try parseUnsigned(
                i2cAddressText,
                field: "I2C address",
                defaultRadix: 16
            )
            guard address >= 0x08 && address <= 0x77 else {
                throw I2CFormError.message(
                    "I2C address must be between 0x08 and 0x77."
                )
            }
            let subaddress = i2cSubaddressLength == 0 ? 0 : try parseUnsigned(
                i2cSubaddressText,
                field: "I2C subaddress",
                defaultRadix: 16
            )
            let length = try parseUnsigned(
                i2cReadLengthText,
                field: "I2C read length",
                defaultRadix: 10
            )
            guard length >= 1 && length <= 255 else {
                throw I2CFormError.message(
                    "I2C read length must be between 1 and 255 bytes."
                )
            }
            if i2cSubaddressLength == 1 && subaddress > 0xff {
                throw I2CFormError.message(
                    "One-byte I2C subaddresses must fit in 0x00 through 0xff."
                )
            }
            if i2cSubaddressLength == 2 && subaddress > 0xffff {
                throw I2CFormError.message(
                    "Two-byte I2C subaddresses must fit in 0x0000 through 0xffff."
                )
            }

            i2cFormMessage = ""
            monitor.readI2C(
                address: address,
                subaddress: subaddress,
                subaddressLength: i2cSubaddressLength,
                length: length
            )
        } catch {
            i2cFormMessage = error.localizedDescription
        }
    }

    private func parseUnsigned(
        _ text: String,
        field: String,
        defaultRadix: Int
    ) throws -> UInt32 {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            throw I2CFormError.message("\(field) is required.")
        }
        let radix: Int
        let digits: Substring
        if trimmed.lowercased().hasPrefix("0x") {
            radix = 16
            digits = trimmed.dropFirst(2)
        } else {
            radix = defaultRadix
            digits = Substring(trimmed)
        }
        guard let value = UInt64(String(digits), radix: radix),
              value <= UInt64(UInt32.max) else {
            throw I2CFormError.message("\(field) is not a valid number.")
        }
        return UInt32(value)
    }

    private func parseByte(_ text: String, field: String) throws -> UInt32 {
        let value = try parseUnsigned(text, field: field, defaultRadix: 10)
        guard value <= 255 else {
            throw I2CFormError.message("\(field) must be between 0 and 255.")
        }
        return value
    }
}

private struct LEDPolicyPresetGrid: View {
    let snapshot: TimeCardDeviceSnapshot?
    let smaRoutes: [TimeCardSMARoute]
    let ledActionDisabled: Bool
    let smaPolicyDisabled: Bool
    let applyGNSS: () -> Void
    let applySMA: () -> Void
    let applyAll: () -> Void

    private let columns = [
        GridItem(.adaptive(minimum: 240), spacing: 12)
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Label("Windows-style LED policy presets", systemImage: "wand.and.stars")
                    .font(.headline)
                Spacer()
                StatusPill(
                    snapshot?.capabilityNames.contains("LEDs") == true
                        ? "LED ABI live" : "LED ABI gated",
                    snapshot?.capabilityNames.contains("LEDs") == true
                        ? .green : .secondary
                )
            }

            LazyVGrid(columns: columns, spacing: 12) {
                LEDPolicyPresetCard(
                    preset: .gnss(snapshot: snapshot),
                    disabled: ledActionDisabled,
                    action: applyGNSS
                )
                LEDPolicyPresetCard(
                    preset: .sma(routes: smaRoutes),
                    disabled: smaPolicyDisabled,
                    action: applySMA
                )
                LEDPolicyPresetCard(
                    preset: .all(snapshot: snapshot, routes: smaRoutes),
                    disabled: ledActionDisabled || smaPolicyDisabled,
                    action: applyAll
                )
            }

            Text(
                "Each preset writes only through the existing verified LED "
                    + "selector and then refreshes readback. SMA presets stay "
                    + "disabled until the active board advertises the SMA route ABI."
            )
            .font(.caption)
            .foregroundStyle(.secondary)
            .textSelection(.enabled)
        }
    }
}

private struct LEDPolicyPresetCard: View {
    let preset: LEDPolicyPreset
    let disabled: Bool
    let action: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                Text(preset.title)
                    .font(.headline)
                Spacer()
                StatusPill(disabled ? "Gated" : preset.state, disabled ? .secondary : preset.accent)
            }

            Text(preset.detail)
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

            HStack(spacing: 8) {
                ForEach(preset.swatches) { swatch in
                    VStack(spacing: 5) {
                        Circle()
                            .fill(swatch.color)
                            .frame(width: 18, height: 18)
                            .overlay(Circle().stroke(.white.opacity(0.35), lineWidth: 1))
                            .shadow(color: swatch.color.opacity(0.35), radius: 4)
                        Text(swatch.label)
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity)
                }
            }

            if preset.prominent {
                Button(preset.buttonTitle) {
                    action()
                }
                .buttonStyle(.borderedProminent)
                .disabled(disabled)
            } else {
                Button(preset.buttonTitle) {
                    action()
                }
                .buttonStyle(.bordered)
                .disabled(disabled)
            }
        }
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            preset.accent.opacity(0.08),
            in: RoundedRectangle(cornerRadius: 14)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(preset.accent.opacity(0.18), lineWidth: 1)
        )
    }
}

private struct LEDPolicyPreset: Identifiable {
    let id: String
    let title: String
    let state: String
    let detail: String
    let buttonTitle: String
    let swatches: [LEDPolicySwatch]
    let accent: Color
    let prominent: Bool

    static func gnss(snapshot: TimeCardDeviceSnapshot?) -> Self {
        let state: String
        let detail: String
        if snapshot?.gnssTelemetryAvailable == true {
            state = "Live"
            detail = "GNSS LEDs follow decoded receiver fix and satellite-valid state."
        } else if snapshot?.clockInSync == true {
            state = "Fallback"
            detail = "GNSS summary is absent, so the preset uses clock sync as the health signal."
        } else {
            state = "Ready"
            detail = "GNSS summary is absent; preset applies amber waiting colors until live fix evidence arrives."
        }

        return LEDPolicyPreset(
            id: "gnss",
            title: "GNSS health",
            state: state,
            detail: detail,
            buttonTitle: "Apply GNSS Policy",
            swatches: [
                LEDPolicySwatch("Fix", .green),
                LEDPolicySwatch("Search", .orange),
                LEDPolicySwatch("Fault", .red),
            ],
            accent: .yellow,
            prominent: false
        )
    }

    static func sma(routes: [TimeCardSMARoute]) -> Self {
        let presentRoutes = routes.filter(\.isPresent)
        let detail = presentRoutes.isEmpty
            ? "SMA routes have not been sampled yet; run refresh or self-test first."
            : "\(presentRoutes.count) SMA route(s) will map input blue, output green, disabled dim amber."
        return LEDPolicyPreset(
            id: "sma",
            title: "SMA direction",
            state: presentRoutes.isEmpty ? "Ready" : "Live",
            detail: detail,
            buttonTitle: "Apply SMA Policy",
            swatches: [
                LEDPolicySwatch("Input", .blue),
                LEDPolicySwatch("Output", .green),
                LEDPolicySwatch("Off", .orange.opacity(0.45)),
            ],
            accent: .orange,
            prominent: false
        )
    }

    static func all(
        snapshot: TimeCardDeviceSnapshot?,
        routes: [TimeCardSMARoute]
    ) -> Self {
        let routeCount = routes.filter(\.isPresent).count
        return LEDPolicyPreset(
            id: "all",
            title: "Full front panel",
            state: routeCount == 0 ? "Ready" : "Live",
            detail: "Applies GNSS health plus SMA direction colors in one verified pass.",
            buttonTitle: "Apply All LED Policy",
            swatches: [
                LEDPolicySwatch("GNSS", snapshot?.clockInSync == true ? .green : .orange),
                LEDPolicySwatch("SMA", routeCount == 0 ? .secondary : .blue),
                LEDPolicySwatch("Readback", .mint),
            ],
            accent: .mint,
            prominent: true
        )
    }
}

private struct LEDPolicySwatch: Identifiable {
    let label: String
    let color: Color

    var id: String { label }

    init(_ label: String, _ color: Color) {
        self.label = label
        self.color = color
    }
}

private struct LEDReadbackCard: View {
    let led: TimeCardLEDState

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Circle()
                    .fill(displayColor)
                    .frame(width: 18, height: 18)
                    .overlay(Circle().stroke(.white.opacity(0.35), lineWidth: 1))
                    .shadow(color: displayColor.opacity(0.45), radius: 6)
                Text(led.led.label)
                    .font(.headline)
                Spacer()
                StatusPill(
                    led.isPresent ? "Present" : "Not fitted",
                    led.isPresent ? .green : .secondary
                )
            }

            InfoRow(label: "RGB", value: led.rgbText)
            InfoRow(label: "Current", value: "\(led.globalCurrent)")
            InfoRow(
                label: "Mux",
                value: TimeCardFormatting.byteHex(led.muxChannelMask)
            )
            InfoRow(
                label: "Controller",
                value: String(
                    format: "0x%02x / 0x%02x",
                    led.controllerStatus & 0xff,
                    led.interruptStatus & 0xff
                )
            )
            if led.faultStateValid {
                InfoRow(
                    label: "Faults",
                    value: String(
                        format: "open 0x%05x, short 0x%05x",
                        led.openOutputMask & 0x3ffff,
                        led.shortOutputMask & 0x3ffff
                    )
                )
            }
        }
        .padding(14)
        .background(Color.secondary.opacity(0.07), in: RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(displayColor.opacity(led.isPresent ? 0.25 : 0.08), lineWidth: 1)
        )
    }

    private var displayColor: Color {
        guard led.isPresent else { return .secondary.opacity(0.4) }
        return Color(
            red: Double(min(led.color.red, 255)) / 255.0,
            green: Double(min(led.color.green, 255)) / 255.0,
            blue: Double(min(led.color.blue, 255)) / 255.0
        )
    }
}

private struct LEDByteField: View {
    let label: String
    @Binding var text: String

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(label)
                .font(.caption)
                .foregroundStyle(.secondary)
            TextField(label, text: $text)
                .font(.system(.body, design: .monospaced))
                .textFieldStyle(.roundedBorder)
                .frame(width: 70)
        }
    }
}

private struct I2CTransferResultView: View {
    let transfer: TimeCardI2CTransferSnapshot

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Divider()
            HStack {
                StatusPill("Readback", .green)
                Text(
                    String(
                        format: "%u byte(s) from %@",
                        transfer.length,
                        transfer.addressText
                    )
                )
                .font(.headline)
                Spacer()
                Text(
                    String(
                        format: "controller 0x%02x, interrupts 0x%02x",
                        transfer.controllerStatus & 0xff,
                        transfer.interruptStatus & 0xff
                    )
                )
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)
            }

            Text(transfer.hexDumpLines.joined(separator: "\n"))
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(.primary)
                .textSelection(.enabled)
                .padding(10)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(
                    Color.secondary.opacity(0.07),
                    in: RoundedRectangle(cornerRadius: 10)
                )

            InfoRow(label: "ASCII", value: transfer.asciiText)
        }
    }
}

private struct I2CScanResultsView: View {
    let results: [TimeCardI2CProbeResult]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Divider()
            HStack {
                StatusPill(
                    presentResults.isEmpty ? "No ACK" : "Devices found",
                    presentResults.isEmpty ? .orange : .green
                )
                Text(scanSummary)
                    .font(.headline)
                Spacer()
            }

            if presentResults.isEmpty {
                Text("No devices responded during the last scan.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            } else {
                LazyVGrid(
                    columns: [
                        GridItem(.adaptive(minimum: 82), spacing: 8)
                    ],
                    alignment: .leading,
                    spacing: 8
                ) {
                    ForEach(presentResults) { result in
                        Text(result.addressText)
                            .font(.system(.caption, design: .monospaced))
                            .padding(.horizontal, 10)
                            .padding(.vertical, 6)
                            .background(
                                Color.green.opacity(0.12),
                                in: Capsule()
                            )
                            .overlay(
                                Capsule().stroke(.green.opacity(0.25))
                            )
                            .textSelection(.enabled)
                    }
                }
            }
        }
    }

    private var presentResults: [TimeCardI2CProbeResult] {
        results.filter(\.isPresent)
    }

    private var scanSummary: String {
        if presentResults.isEmpty {
            return "Scanned \(results.count) address(es)"
        }
        return "\(presentResults.count) of \(results.count) address(es) responded"
    }
}

private enum I2CFormError: LocalizedError {
    case message(String)

    var errorDescription: String? {
        switch self {
        case .message(let message):
            message
        }
    }
}

private enum MacWorkspace: String {
    case gnss = "GNSS and Sky Map"
    case uart = "UART and NMEA Laboratory"
    case sma = "SMA Connector Routing"
    case timing = "Signal Generators and Counters"
    case fpga = "FPGA Engines"
    case sensors = "Sensors and IMU"
    case i2c = "I2C and Status LEDs"
    case flash = "FPGA SPI Flash"

    var summary: String {
        switch self {
        case .gnss:
            "Windows decodes ToD GNSS status plus native u-blox receiver telemetry and sky-view details."
        case .uart:
            "Windows provides Time Card UART streams, NMEA decoding, generic serial ports, capture, filtering, and export."
        case .sma:
            "Windows reads and writes the four-connector SMA route fabric with fixed-direction warnings."
        case .timing:
            "Windows controls four periodic generators, four counters, route selection, starts, repeats, and event readback."
        case .fpga:
            "Windows exposes NMEA, PPS, IRIG-B, DCF77, ToD parser, timestamp, and advanced clock cores."
        case .sensors:
            "Windows reads Meta and Celestica environmental sensors and fitted BNO055 or BNO08x IMU telemetry."
        case .i2c:
            "Windows provides known-device probes, full I2C discovery, EEPROM/register tools, mux routing, and LED control."
        case .flash:
            "Windows validates OCPC images, erases and programs flash, and verifies readback."
        }
    }

    var requiredABI: String {
        switch self {
        case .gnss, .uart: "UART stream and GNSS/ToD ABI"
        case .sma: "SMA route get/set ABI"
        case .timing: "Generator, counter, and event ABI"
        case .fpga: "Versioned FPGA-core register ABI"
        case .sensors: "Environmental sensor ABI and IMU expansion"
        case .i2c: "I2C diagnostics, mux, and LED ABI"
        case .flash: "SPI-flash query/program/readback ABI"
        }
    }

    var symbol: String {
        switch self {
        case .gnss: "location.north.line"
        case .uart: "terminal"
        case .sma: "cable.connector"
        case .timing: "waveform.path.ecg"
        case .fpga: "cpu"
        case .sensors: "gyroscope"
        case .i2c: "lightbulb.led"
        case .flash: "externaldrive.badge.timemachine"
        }
    }

    var accent: Color {
        switch self {
        case .gnss: .cyan
        case .uart: .teal
        case .sma: .orange
        case .timing: .green
        case .fpga: .purple
        case .sensors: .pink
        case .i2c: .yellow
        case .flash: .red
        }
    }

    var rows: [(String, String)] {
        switch self {
        case .gnss:
            return [
                ("ToD core status", "Live when the board exposes ToD status through ABI v2"),
                ("u-blox identity and firmware", "Available through guarded UBX poll writes and UART reads"),
                ("Sky map and constellation counts", "Needs GNSS stream decoder data"),
                ("Survey-in and fixed-position controls", "Needs guarded receiver configuration ABI"),
            ]
        case .uart:
            return [
                ("Hardware UART ports 0-3", "Live through DriverKit ABI v8/v9"),
                ("Generic macOS serial ports", "Live through app-only IOKit serial enumeration"),
                ("NMEA generator control", "Needs NMEA register get/set ABI"),
                ("Capture and export", "Manual capture and support-bundle export are available"),
            ]
        case .sma:
            return [
                ("Connector direction", "Needs route readback ABI"),
                ("Input/output routing menus", "Needs validated SMA set ABI"),
                ("Fixed ART routing", "Can be profile-gated after ABI reports ART map details"),
                ("Output warnings", "UI scaffolded, enabled with route ABI"),
            ]
        case .timing:
            return [
                ("Periodic generators", "Needs generator query/control ABI"),
                ("Frequency counters", "ABI v10 query/control for validated revision-02 LitePCIe images"),
                ("Future PHC/TAI starts", "Needs trusted time-scale contract"),
                ("Completion/error events", "Needs event FIFO ABI"),
            ]
        case .fpga:
            return [
                ("PPS master/slave", "Needs versioned core query/control ABI"),
                ("IRIG-B and DCF77", "Needs image contract and core masks"),
                ("Timestamp laboratory", "Needs timestamp channel ABI"),
                ("Advanced clock controls", "Needs read-back-verified setters"),
            ]
        case .sensors:
            return [
                ("LM75B board temperatures", "Live through DriverKit ABI v7 and CLI"),
                ("SHT3x humidity and temperature", "Live through DriverKit ABI v7 and CLI"),
                ("ICP-10100 pressure sensor", "Live with OTP compensated pressure"),
                ("BME/BMP and INA rails", "Profile-aware, decoder pending"),
                ("BNO055/BNO08x IMU", "Fused motion, 3D orientation, and low-rate vibration with ABI v11"),
                ("Vibration charts", "Needs live IMU fused-motion samples"),
            ]
        case .i2c:
            return [
                ("AXI IIC health", "Available through DriverKit ABI v5 and CLI"),
                ("Known-device probes", "Available through zero-byte probe ABI"),
                ("Mux routing", "Available through guarded mux ABI"),
                ("RGB subsystem LEDs", "Available through LED controller ABI"),
            ]
        case .flash:
            return [
                ("JEDEC and geometry", "Needs flash query ABI"),
                ("OCPC image validation", "Can be app-side once image import exists"),
                ("Erase and program", "Needs guarded flash write ABI"),
                ("Readback verification", "Needs flash read ABI"),
            ]
        }
    }
}

private struct CapabilityWorkspaceView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    let workspace: MacWorkspace

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

                ControlCenterPanel(
                    title: workspace.rawValue,
                    subtitle: "Ported Windows workspace, macOS backend gated"
                ) {
                    HStack(alignment: .top, spacing: 14) {
                        Image(systemName: workspace.symbol)
                            .font(.system(size: 34, weight: .semibold))
                            .foregroundStyle(workspace.accent)
                            .frame(width: 58, height: 58)
                            .background(
                                workspace.accent.opacity(0.14),
                                in: RoundedRectangle(cornerRadius: 16)
                            )

                        VStack(alignment: .leading, spacing: 10) {
                            Text(workspace.summary)
                                .foregroundStyle(.secondary)

                            HStack(spacing: 8) {
                                StatusPill("ABI \(monitor.snapshot.map { "\($0.abiVersion)" } ?? "?")", .blue)
                                StatusPill(liveStatus, statusColor)
                            }
                        }
                    }

                    Divider()

                    InfoRow(label: "macOS backend requirement", value: workspace.requiredABI)
                }

                ControlCenterPanel(
                    title: "Feature coverage",
                    subtitle: "Windows feature mapped to macOS readiness"
                ) {
                    VStack(spacing: 10) {
                        ForEach(workspace.rows, id: \.0) { row in
                            FeatureRow(name: row.0, state: state(for: row.0), note: row.1)
                        }
                    }
                }

                if workspace == .gnss {
                    todTelemetry
                }
            }
            .padding(24)
        }
    }

    @ViewBuilder
    private var todTelemetry: some View {
        if let snapshot = monitor.snapshot {
            ControlCenterPanel(
                title: "Live ToD signals",
                subtitle: "Read through the current macOS ABI when present"
            ) {
                InfoRow(
                    label: "ToD capability",
                    value: snapshot.capabilityNames.contains("ToD")
                        ? "Available" : "Not present"
                )
                InfoRow(
                    label: "ToD version",
                    value: TimeCardFormatting.validHex(
                        snapshot.todVersion,
                        valid: snapshot.hasValidField(1 << 3)
                    )
                )
                InfoRow(
                    label: "ToD status",
                    value: TimeCardFormatting.validHex(
                        snapshot.todStatus,
                        valid: snapshot.hasValidField(1 << 4)
                    )
                )
            }
        }
    }

    private var liveStatus: String {
        switch monitor.state {
        case .connected:
            workspace == .sensors ? "Live" : "Backend gated"
        case .discovering:
            "Discovering"
        case .noService:
            "No driver"
        case .accessUnavailable:
            "Access needed"
        case .failed:
            "Error"
        }
    }

    private var statusColor: Color {
        switch monitor.state {
        case .connected: workspace == .sensors ? .green : .orange
        case .discovering: .blue
        case .noService, .accessUnavailable: .orange
        case .failed: .red
        }
    }

    private func state(for name: String) -> String {
        if workspace == .gnss && name == "ToD core status" {
            return monitor.snapshot?.capabilityNames.contains("ToD") == true
                ? "Live" : "Unavailable"
        }
        if workspace == .sensors {
            switch name {
            case "LM75B board temperatures",
                 "SHT3x humidity and temperature",
                 "ICP-10100 pressure sensor":
                return monitor.snapshot?.capabilityNames.contains("Sensors") == true
                    ? "Live" : "Unavailable"
            case "BNO055/BNO08x IMU":
                return monitor.snapshot?.capabilityNames.contains("Sensors") == true
                    ? "Partial" : "Unavailable"
            default:
                return "Backend pending"
            }
        }
        return "Backend pending"
    }
}

private struct SensorDashboardView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()
                MotionWorkspaceView()
                sensorHeader

                if !sensorCapabilityAvailable {
                    ControlCenterPanel(
                        title: "Sensors not advertised",
                        subtitle: "Board profile does not expose the sensor fabric"
                    ) {
                        Text(
                            "This Time Card profile did not advertise the Sensors "
                                + "capability through the active DriverKit ABI. The "
                                + "Control Center keeps this as unavailable instead "
                                + "of showing fabricated zero readings."
                        )
                        .foregroundStyle(.secondary)
                    }
                } else if let telemetry = monitor.sensorTelemetry {
                    SensorMetricGrid(telemetry: telemetry)
                    SensorBoardTemperatureView(telemetry: telemetry)
                    SensorTemperatureHistoryChart()
                    SensorInventoryView(telemetry: telemetry)
                } else {
                    ControlCenterPanel(
                        title: "Waiting for sensor sample",
                        subtitle: "Automatic refresh is running"
                    ) {
                        ContentUnavailableView(
                            "No sensor sample yet",
                            systemImage: "gyroscope",
                            description: Text(
                                monitor.sensorMessage.isEmpty
                                    ? "Waiting for DriverKit sensor telemetry."
                                    : monitor.sensorMessage
                            )
                        )
                        .frame(height: 170)
                    }
                }
            }
            .padding(24)
        }
    }

    private var sensorHeader: some View {
        ControlCenterPanel(
            title: "Sensors and IMU",
            subtitle: "Environmental telemetry with ABI v7; fused motion with ABI v11"
        ) {
            HStack(alignment: .top, spacing: 14) {
                Image(systemName: "gyroscope")
                    .font(.system(size: 34, weight: .semibold))
                    .foregroundStyle(.pink)
                    .frame(width: 58, height: 58)
                    .background(.pink.opacity(0.14), in: RoundedRectangle(cornerRadius: 16))

                VStack(alignment: .leading, spacing: 10) {
                    Text(
                        "The macOS Control Center now reads the Celestica "
                            + "fixed-channel environmental stack, carries "
                            + "ICP-10100 factory OTP calibration, and supports "
                            + "BNO08x/BNO055 fused motion without treating "
                            + "missing devices as plausible data."
                    )
                    .foregroundStyle(.secondary)

                    HStack(spacing: 8) {
                        StatusPill(
                            sensorCapabilityAvailable
                                ? "ABI v7 sensors" : "Sensors unavailable",
                            sensorCapabilityAvailable ? .green : .secondary
                        )
                        StatusPill(sensorCountText, sensorCountColor)
                        if let telemetry = monitor.sensorTelemetry {
                            StatusPill(
                                telemetry.muxWasRestored
                                    ? "Mux restored" : "Mux changed",
                                telemetry.muxWasRestored ? .blue : .orange
                            )
                        }
                    }
                }

                Spacer()

                Button("Refresh") {
                    monitor.refresh()
                }
                .buttonStyle(.borderedProminent)
            }

            if !monitor.sensorMessage.isEmpty {
                Divider()
                Text(monitor.sensorMessage)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }
        }
    }

    private var sensorCapabilityAvailable: Bool {
        monitor.snapshot?.capabilityNames.contains("Sensors") == true
    }

    private var sensorCountText: String {
        guard let telemetry = monitor.sensorTelemetry else {
            return sensorCapabilityAvailable ? "Sampling" : "No sensor ABI"
        }
        return "\(telemetry.validReadings.count) live block(s)"
    }

    private var sensorCountColor: Color {
        guard let telemetry = monitor.sensorTelemetry else {
            return sensorCapabilityAvailable ? .orange : .secondary
        }
        return telemetry.validReadings.isEmpty ? .orange : .green
    }
}

private struct SensorMetricGrid: View {
    let telemetry: TimeCardSensorSnapshot

    var body: some View {
        LazyVGrid(
            columns: [GridItem(.adaptive(minimum: 210), spacing: 14)],
            spacing: 14
        ) {
            MetricCard(
                title: "Ambient temperature",
                value: TimeCardFormatting.temperature(primaryTemperature?.value),
                detail: primaryTemperature?.source ?? "Waiting for SHT3x or ICP-10100",
                systemImage: "thermometer.medium",
                accent: .pink
            )
            MetricCard(
                title: "Relative humidity",
                value: TimeCardFormatting.humidity(humidity),
                detail: telemetry.humidityReading.map(SensorUIFormatting.route)
                    ?? "SHT3x not sampled",
                systemImage: "humidity",
                accent: .cyan
            )
            MetricCard(
                title: "Pressure",
                value: pressureValue,
                detail: pressureDetail,
                systemImage: "barometer",
                accent: .indigo
            )
            MetricCard(
                title: "Dew point",
                value: TimeCardFormatting.temperature(telemetry.dewPointCelsius),
                detail: telemetry.dewPointCelsius == nil
                    ? "Requires SHT3x humidity" : "Calculated from SHT3x sample",
                systemImage: "drop.degreesign",
                accent: .mint
            )
        }
    }

    private var primaryTemperature: (value: Double, source: String)? {
        if let reading = telemetry.humidityReading,
           reading.isValid,
           let temperature = reading.temperatureCelsius {
            return (temperature, "\(reading.kind.label) " + SensorUIFormatting.route(reading))
        }
        if let reading = telemetry.pressureReading,
           reading.isValid,
           let temperature = reading.temperatureCelsius {
            return (temperature, "\(reading.kind.label) " + SensorUIFormatting.route(reading))
        }
        return nil
    }

    private var humidity: Double? {
        guard let reading = telemetry.humidityReading, reading.isValid else {
            return nil
        }
        return reading.humidityPercent
    }

    private var pressureValue: String {
        if let pressure = telemetry.pressurePascals {
            return TimeCardFormatting.hPa(pressure)
        }
        if let reading = telemetry.pressureReading, reading.hasPressure {
            return TimeCardFormatting.raw24(reading.pressureRaw)
        }
        return "Unavailable"
    }

    private var pressureDetail: String {
        guard let reading = telemetry.pressureReading else {
            return "ICP-10100 not sampled"
        }
        if telemetry.pressurePascals != nil {
            return "\(reading.kind.label) compensated from OTP"
        }
        if reading.isCalibrated {
            return "OTP read, compensation rejected range"
        }
        return reading.isPresent ? "Raw ICP-10100 sample" : "ICP-10100 no ACK"
    }
}

private struct SensorBoardTemperatureView: View {
    let telemetry: TimeCardSensorSnapshot

    var body: some View {
        ControlCenterPanel(
            title: "Board temperature zones",
            subtitle: "LM75B devices on the Celestica sensor mux branch"
        ) {
            if telemetry.boardTemperatures.isEmpty {
                ContentUnavailableView(
                    "No LM75B readings",
                    systemImage: "thermometer.low",
                    description: Text("This profile has no LM75B zone sample.")
                )
                .frame(height: 150)
            } else {
                LazyVGrid(
                    columns: [GridItem(.adaptive(minimum: 180), spacing: 12)],
                    spacing: 12
                ) {
                    ForEach(
                        Array(telemetry.boardTemperatures.enumerated()),
                        id: \.element.id
                    ) { item in
                        MetricCard(
                            title: "Zone \(item.offset + 1)",
                            value: item.element.isValid
                                ? TimeCardFormatting.temperature(
                                    item.element.temperatureCelsius
                                )
                                : "No ACK",
                            detail: SensorUIFormatting.route(item.element),
                            systemImage: "thermometer",
                            accent: item.element.isValid ? .orange : .secondary
                        )
                    }
                }
            }
        }
    }
}

private struct SensorTemperatureHistoryChart: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ControlCenterPanel(
            title: "Temperature history",
            subtitle: "Rolling live readings from valid sensor blocks"
        ) {
            if monitor.sensorHistory.isEmpty {
                ContentUnavailableView(
                    "No temperature history yet",
                    systemImage: "chart.xyaxis.line",
                    description: Text("A valid temperature sample will start this chart.")
                )
                .frame(height: 180)
            } else {
                Chart(monitor.sensorHistory) { point in
                    LineMark(
                        x: .value("Time", point.timestamp),
                        y: .value("Temperature", point.value)
                    )
                    .foregroundStyle(by: .value("Sensor", point.series))
                    .interpolationMethod(.catmullRom)
                }
                .chartXAxis(.hidden)
                .chartYAxisLabel("°C")
                .frame(height: 210)
            }
        }
    }
}

private struct SensorInventoryView: View {
    let telemetry: TimeCardSensorSnapshot

    var body: some View {
        ControlCenterPanel(
            title: "Sensor inventory",
            subtitle: "Raw ABI rows, mux restore state, and CRC/calibration flags"
        ) {
            VStack(alignment: .leading, spacing: 10) {
                InfoRow(
                    label: "Sensor fabric",
                    value: telemetry.isPresent ? "Available" : "Unavailable",
                    valueColor: telemetry.isPresent ? .green : .orange
                )
                InfoRow(
                    label: "Mux route",
                    value: String(
                        format: "prior 0x%02x, restored 0x%02x",
                        telemetry.muxChannelMask,
                        telemetry.restoredMuxChannelMask
                    ),
                    valueColor: telemetry.muxWasRestored ? .primary : .orange
                )
                InfoRow(
                    label: "Controller/events",
                    value: String(
                        format: "0x%02x / 0x%08x",
                        telemetry.controllerStatus,
                        telemetry.interruptStatus
                    )
                )
                InfoRow(
                    label: "Sensor capabilities",
                    value: telemetry.capabilityNames.isEmpty
                        ? "None"
                        : telemetry.capabilityNames.joined(separator: ", ")
                )
                InfoRow(
                    label: "ICP-10100 OTP",
                    value: SensorUIFormatting.otp(telemetry.icp10100Otp)
                )

                Divider()

                if telemetry.readings.isEmpty {
                    ContentUnavailableView(
                        "No sensor rows returned",
                        systemImage: "list.bullet.rectangle",
                        description: Text("The driver returned an empty sensor inventory.")
                    )
                    .frame(height: 150)
                } else {
                    VStack(spacing: 10) {
                        ForEach(telemetry.readings) { reading in
                            SensorInventoryRow(reading: reading)
                        }
                    }
                }
            }
        }
    }
}

private struct SensorInventoryRow: View {
    let reading: TimeCardSensorReading

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(reading.kind.label)
                        .font(.body.weight(.semibold))
                    Text(SensorUIFormatting.route(reading))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Spacer(minLength: 16)

                Text(SensorUIFormatting.rawSummary(reading))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }

            HStack(spacing: 8) {
                ForEach(SensorUIFormatting.badges(reading)) { badge in
                    StatusPill(badge.label, badge.color)
                }
            }
        }
        .padding(12)
        .background(Color.secondary.opacity(0.07), in: RoundedRectangle(cornerRadius: 12))
    }
}

private struct SensorStatusBadge: Identifiable {
    let label: String
    let color: Color
    var id: String { label }
}

private enum SensorUIFormatting {
    static func route(_ reading: TimeCardSensorReading) -> String {
        String(
            format: "mux 0x%02x addr 0x%02x",
            reading.muxChannelMask,
            reading.address
        )
    }

    static func otp(_ values: [Int32]) -> String {
        guard values.count >= 4, values.contains(where: { $0 != 0 }) else {
            return "Unavailable"
        }
        return values.prefix(4)
            .map { String($0) }
            .joined(separator: ", ")
    }

    static func rawSummary(_ reading: TimeCardSensorReading) -> String {
        [
            String(format: "flags 0x%08x", reading.flags),
            String(format: "raw0 0x%08x", reading.raw0),
            String(format: "raw1 0x%08x", reading.raw1),
            String(format: "raw2 0x%08x", reading.raw2),
        ].joined(separator: "  ")
    }

    static func badges(_ reading: TimeCardSensorReading) -> [SensorStatusBadge] {
        var badges: [SensorStatusBadge] = [
            SensorStatusBadge(
                label: reading.isPresent ? "Present" : "No ACK",
                color: reading.isPresent ? .green : .secondary
            ),
            SensorStatusBadge(
                label: reading.isValid ? "Valid" : "No decoded sample",
                color: reading.isValid ? .green : .orange
            ),
        ]
        if reading.isConfigured {
            badges.append(SensorStatusBadge(label: "Configured", color: .blue))
        }
        if reading.hasValidCRC {
            badges.append(SensorStatusBadge(label: "CRC OK", color: .cyan))
        }
        if reading.isCalibrated {
            badges.append(SensorStatusBadge(label: "Calibrated", color: .mint))
        }
        if reading.isIMU {
            badges.append(SensorStatusBadge(label: "IMU", color: .pink))
        }
        if reading.isOverflowed {
            badges.append(SensorStatusBadge(label: "Overflow", color: .red))
        }
        return badges
    }
}

private struct SMARoutingView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

                ControlCenterPanel(
                    title: "SMA Signal Routing",
                    subtitle: "Live DriverKit ABI v3 connector control"
                ) {
                    HStack(alignment: .top, spacing: 14) {
                        Image(systemName: "cable.connector")
                            .font(.system(size: 34, weight: .semibold))
                            .foregroundStyle(.orange)
                            .frame(width: 58, height: 58)
                            .background(.orange.opacity(0.14), in: RoundedRectangle(cornerRadius: 16))

                        VStack(alignment: .leading, spacing: 10) {
                            Text(
                                "Read and route the four Time Card SMA connectors using "
                                    + "the same function selectors as the Windows Control "
                                    + "Center and Linux ptp_ocp driver."
                            )
                            .foregroundStyle(.secondary)

                            HStack(spacing: 8) {
                                StatusPill(
                                    monitor.snapshot?.capabilityNames.contains("SMA") == true
                                        ? "SMA live" : "SMA unavailable",
                                    monitor.snapshot?.capabilityNames.contains("SMA") == true
                                        ? .green : .secondary
                                )
                                StatusPill("Readback verified", .blue)
                            }
                        }

                        Spacer()

                        Button("Refresh") {
                            monitor.refreshSMA()
                        }
                        .buttonStyle(.borderedProminent)
                        .disabled(
                            monitor.snapshot?.capabilityNames.contains("SMA") != true
                        )
                    }

                    if !monitor.smaMessage.isEmpty {
                        Divider()
                        Text(monitor.smaMessage)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .textSelection(.enabled)
                    }
                }

                if monitor.snapshot?.capabilityNames.contains("SMA") == true {
                    LazyVGrid(
                        columns: [
                            GridItem(.adaptive(minimum: 310), spacing: 14)
                        ],
                        spacing: 14
                    ) {
                        ForEach(1...4, id: \.self) { connector in
                            SMARouteCard(
                                connector: UInt32(connector),
                                route: monitor.smaRoutes.first {
                                    $0.connector == UInt32(connector)
                                }
                            )
                        }
                    }
                } else {
                    ControlCenterPanel(
                        title: "SMA backend unavailable",
                        subtitle: "Driver does not advertise SMA capability"
                    ) {
                        Text(
                            "Install the ABI v3 DriverKit extension or use a "
                                + "board profile with SMA routing registers."
                        )
                        .foregroundStyle(.secondary)
                    }
                }
            }
            .padding(24)
        }
    }
}

private struct SMARouteCard: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    let connector: UInt32
    let route: TimeCardSMARoute?
    @State private var direction: TimeCardSMADirection = .disabled
    @State private var function: UInt32 = 0

    var body: some View {
        ControlCenterPanel(
            title: "SMA \(connector)",
            subtitle: routeSubtitle
        ) {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    StatusPill(route?.direction.label ?? "Not queried", routeColor)
                    if route?.isFixedDirection == true {
                        StatusPill("Fixed direction", .secondary)
                    }
                    if route?.isFixedFunction == true {
                        StatusPill("Fixed function", .secondary)
                    }
                    Spacer()
                }

                InfoRow(label: "Current function", value: route?.functionName ?? "Unavailable")
                InfoRow(
                    label: "Raw input",
                    value: route.map { TimeCardFormatting.hex(UInt64($0.inputMap)) } ?? "Unavailable"
                )
                InfoRow(
                    label: "Raw output",
                    value: route.map { TimeCardFormatting.hex(UInt64($0.outputMap)) } ?? "Unavailable"
                )

                Divider()

                Picker("Direction", selection: $direction) {
                    ForEach(TimeCardSMADirection.allCases) { item in
                        Text(item.label).tag(item)
                    }
                }
                .disabled(route?.isFixedDirection == true)
                .onChange(of: direction) { _, newValue in
                    function = TimeCardSMACatalog.functions(for: newValue).first?.id ?? 0
                }

                if direction != .disabled {
                    Picker("Function", selection: $function) {
                        ForEach(TimeCardSMACatalog.functions(for: direction)) { item in
                            Text(item.label).tag(item.id)
                        }
                    }
                    .disabled(route?.isFixedFunction == true)
                }

                Button("Apply Route") {
                    monitor.setSMARoute(
                        connector: connector,
                        direction: direction,
                        function: function
                    )
                }
                .buttonStyle(.borderedProminent)
                .disabled(
                    monitor.commandInProgress ||
                        route == nil ||
                        (route?.isFixedDirection == true &&
                         direction != route?.direction) ||
                        (route?.isFixedFunction == true &&
                         function != route?.function)
                )
            }
        }
        .onAppear(perform: syncState)
        .onChange(of: route) { _, _ in syncState() }
    }

    private var routeSubtitle: String {
        guard let route else { return "Awaiting DriverKit readback" }
        return route.isPresent ? "Function \(TimeCardFormatting.hex(UInt64(route.function)))" :
            "Not present"
    }

    private var routeColor: Color {
        switch route?.direction {
        case .input: .cyan
        case .output: .green
        case .disabled: .secondary
        case nil: .orange
        }
    }

    private func syncState() {
        guard let route else { return }
        direction = route.direction
        function = route.function
        if direction != .disabled &&
            !TimeCardSMACatalog.functions(for: direction).contains(
                where: { $0.id == function }
            ) {
            function = TimeCardSMACatalog.functions(for: direction).first?.id ?? 0
        }
    }
}

private struct TelemetryStudioView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @EnvironmentObject private var lab: DeviceLabMonitor
    @State private var telemetryExportMessage = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()
                TelemetryChartsView()
                TimelineView(.periodic(from: .now, by: 1)) { context in
                    MotionVibrationPanel(now: context.date)
                }

                ControlCenterPanel(
                    title: "Telemetry Studio",
                    subtitle: "Live measurements and remaining hardware capabilities"
                ) {
                    FeatureRow(
                        name: "Sampling-window history",
                        state: monitor.samplingWindowHistory.isEmpty ? "Waiting" : "Live",
                        note: "Uses bracketed DriverKit cross-timestamps."
                    )
                    FeatureRow(
                        name: "PHC offset history",
                        state: "Gated",
                        note: "Requires trusted UTC-to-TAI or clock-domain contract."
                    )
                    FeatureRow(
                        name: "GNSS and temperature history",
                        state: monitor.telemetryHistory.isEmpty ? "Waiting" : "Live",
                        note: "Valid ToD satellite counts and per-sensor temperatures, with pause, point inspection, and session recording."
                    )
                    FeatureRow(
                        name: "Vibration history",
                        state: lab.motionRunning ? "Sampling" : "Stopped",
                        note: "Start in Sensors and IMU. Uses actual gravity-compensated reports, with CSV/JSON export."
                    )
                    FeatureRow(
                        name: "CSV and JSON export",
                        state: monitor.snapshot == nil ? "Waiting" : "Live",
                        note: "Copies or saves structured JSON and CSV samples from the current macOS telemetry model."
                    )
                }

                ControlCenterPanel(
                    title: "Telemetry export",
                    subtitle: "Copy live JSON or CSV for support, notebooks, and release testing"
                ) {
                    if monitor.snapshot == nil {
                        ContentUnavailableView(
                            "No telemetry snapshot",
                            systemImage: "doc.badge.clock",
                            description: Text(
                                "Wait for the driver to connect before exporting telemetry."
                            )
                        )
                        .frame(height: 150)
                    } else {
                        VStack(alignment: .leading, spacing: 12) {
                            HStack {
                                Button("Copy JSON") {
                                    copyTelemetry(
                                        telemetryJSONText,
                                        message: "JSON telemetry copied."
                                    )
                                }
                                .buttonStyle(.borderedProminent)

                                Button("Copy CSV") {
                                    copyTelemetry(
                                        telemetryCSVText,
                                        message: "CSV telemetry copied."
                                    )
                                }
                                .buttonStyle(.bordered)

                                Button("Save JSON") {
                                    saveTelemetry(
                                        telemetryJSONText,
                                        defaultName:
                                            "TimeCardMacOS-Telemetry-\(telemetryTimestampForFilename).json",
                                        allowedContentTypes: [.json],
                                        successPrefix: "Saved JSON telemetry"
                                    )
                                }
                                .buttonStyle(.bordered)

                                Button("Save CSV") {
                                    saveTelemetry(
                                        telemetryCSVText,
                                        defaultName:
                                            "TimeCardMacOS-Telemetry-\(telemetryTimestampForFilename).csv",
                                        allowedContentTypes: [.commaSeparatedText],
                                        successPrefix: "Saved CSV telemetry"
                                    )
                                }
                                .buttonStyle(.bordered)

                                Spacer()

                                Text(telemetryExportSummary)
                                    .font(.caption.monospacedDigit())
                                    .foregroundStyle(.secondary)
                            }

                            Text(
                                telemetryExportMessage.isEmpty
                                    ? telemetryPreviewText
                                    : telemetryExportMessage
                            )
                            .font(.system(.caption, design: .monospaced))
                            .foregroundStyle(.secondary)
                            .textSelection(.enabled)
                        }
                    }
                }
            }
            .padding(24)
        }
    }

    private func copyTelemetry(_ text: String, message: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
        telemetryExportMessage = message
    }

    private func saveTelemetry(
        _ text: String,
        defaultName: String,
        allowedContentTypes: [UTType],
        successPrefix: String
    ) {
        let panel = NSSavePanel()
        panel.title = "Save Time Card Telemetry"
        panel.nameFieldStringValue = defaultName
        panel.allowedContentTypes = allowedContentTypes
        panel.canCreateDirectories = true

        guard panel.runModal() == .OK, let destinationURL = panel.url else {
            telemetryExportMessage = "Telemetry save canceled."
            return
        }

        do {
            try text.write(
                to: destinationURL,
                atomically: true,
                encoding: .utf8
            )
            telemetryExportMessage = "\(successPrefix) to \(destinationURL.path)."
            NSWorkspace.shared.activateFileViewerSelecting([destinationURL])
        } catch {
            telemetryExportMessage =
                "Telemetry save failed: \(error.localizedDescription)"
        }
    }

    private var telemetryTimestampForFilename: String {
        let formatter = DateFormatter()
        formatter.calendar = Calendar(identifier: .gregorian)
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        return formatter.string(from: Date())
    }

    private var telemetryExportSummary: String {
        let sensorCount = monitor.sensorTelemetry?.validReadings.count ?? 0
        let ledCount = monitor.ledStates.filter(\.isPresent).count
        return "\(monitor.samplingWindowHistory.count) samples, \(sensorCount) sensors, \(ledCount) LEDs"
    }

    private var telemetryPreviewText: String {
        [
            "Ready to copy current telemetry.",
            "JSON includes device, clock, GNSS, SMA, sensors, I2C, LEDs, and self-test.",
            "CSV includes sampling-window, sensor, SMA, LED, GNSS, and self-test rows.",
        ].joined(separator: "\n")
    }

    private var telemetryJSONText: String {
        let object = telemetryJSONObject
        guard JSONSerialization.isValidJSONObject(object),
              let data = try? JSONSerialization.data(
                withJSONObject: object,
                options: [.prettyPrinted, .sortedKeys]
              ),
              let text = String(data: data, encoding: .utf8) else {
            return "{\n  \"error\" : \"Telemetry JSON encoding failed\"\n}"
        }
        return text
    }

    private var telemetryJSONObject: [String: Any] {
        guard let snapshot = monitor.snapshot else {
            return [
                "generatedAt": TimeCardFormatting.date(Date()),
                "state": String(describing: monitor.state),
                "error": monitor.errorMessage,
            ]
        }

        return [
            "generatedAt": TimeCardFormatting.date(Date()),
            "state": String(describing: monitor.state),
            "device": [
                "board": snapshot.boardName,
                "pciIdentity": snapshot.pciIdentity,
                "pciRevision": String(
                    format: "0x%02x",
                    snapshot.pciRevision & 0xff
                ),
                "layout": snapshot.layoutName,
                "driverVersion": snapshot.driverVersionText,
                "abiVersion": Int(snapshot.abiVersion),
                "bar0": TimeCardFormatting.hex(snapshot.barSize),
                "capabilities": snapshot.capabilityNames,
            ],
            "clock": [
                "rawTimestamp": TimeCardFormatting.rawTimestamp(snapshot),
                "clockStatus": TimeCardFormatting.validHex(
                    snapshot.clockStatus,
                    valid: snapshot.hasValidField(1 << 1)
                ),
                "sync": TimeCardFormatting.syncStatus(snapshot),
                "source": TimeCardFormatting.clockSource(snapshot),
                "sampleWindowNanoseconds":
                    String(snapshot.sampleWindowNanoseconds),
            ],
            "tod": [
                "available": snapshot.todTelemetryAvailable,
                "status": TimeCardFormatting.validHex(
                    snapshot.todStatus,
                    valid: snapshot.hasValidField(1 << 4)
                ),
                "utcOffsetSeconds": snapshot.utcOffsetSeconds.map(String.init)
                    ?? "unavailable",
            ],
            "gnss": [
                "available": snapshot.gnssTelemetryAvailable,
                "fix": snapshot.gnssFixName,
                "seenSatellites": snapshot.seenSatellites.map(String.init)
                    ?? "unavailable",
                "lockedSatellites": snapshot.lockedSatellites.map(String.init)
                    ?? "unavailable",
            ],
            "samplingWindowHistory": monitor.samplingWindowHistory.map {
                [
                    "timestamp": TimeCardFormatting.date($0.timestamp),
                    "nanoseconds": $0.nanoseconds,
                ] as [String: Any]
            },
            "smaRoutes": monitor.smaRoutes.map(smaRouteJSON),
            "sensors": monitor.sensorTelemetry.map(sensorJSON) ?? NSNull(),
            "i2c": i2cJSON,
            "ledStates": monitor.ledStates.map(ledJSON),
            "selfTest": monitor.selfTestReport.map(selfTestJSON) ?? NSNull(),
        ]
    }

    private var i2cJSON: Any {
        guard monitor.i2cStatus != nil || monitor.i2cMux != nil else {
            return NSNull()
        }
        var object: [String: Any] = [:]
        if let status = monitor.i2cStatus {
            object["status"] = [
                "present": status.isPresent,
                "enabled": status.isEnabled,
                "busBusy": status.isBusBusy,
                "control": TimeCardFormatting.byteHex(status.control),
                "status": TimeCardFormatting.byteHex(status.status),
                "interruptStatus":
                    TimeCardFormatting.byteHex(status.interruptStatus),
                "interruptEnable":
                    TimeCardFormatting.byteHex(status.interruptEnable),
                "txFifoOccupancy": Int(status.txFifoOccupancy),
                "rxFifoOccupancy": Int(status.rxFifoOccupancy),
                "knownDevices": status.knownDeviceNames,
            ]
        }
        if let mux = monitor.i2cMux {
            object["mux"] = [
                "present": mux.isPresent,
                "channelMask": TimeCardFormatting.byteHex(mux.channelMask),
                "controllerStatus":
                    TimeCardFormatting.byteHex(mux.controllerStatus),
                "interruptStatus":
                    TimeCardFormatting.byteHex(mux.interruptStatus),
            ]
        }
        return object
    }

    private func smaRouteJSON(_ route: TimeCardSMARoute) -> [String: Any] {
        [
            "connector": Int(route.connector),
            "direction": route.direction.label,
            "function": TimeCardFormatting.hex(UInt64(route.function)),
            "functionName": route.functionName,
            "present": route.isPresent,
            "fixedDirection": route.isFixedDirection,
            "fixedFunction": route.isFixedFunction,
            "inputMap": TimeCardFormatting.hex(UInt64(route.inputMap)),
            "outputMap": TimeCardFormatting.hex(UInt64(route.outputMap)),
        ]
    }

    private func sensorJSON(_ telemetry: TimeCardSensorSnapshot) -> [String: Any] {
        [
            "present": telemetry.isPresent,
            "valid": telemetry.isValid,
            "validReadings": telemetry.validReadings.count,
            "totalReadings": telemetry.readings.count,
            "muxChannelMask": TimeCardFormatting.byteHex(telemetry.muxChannelMask),
            "restoredMuxChannelMask":
                TimeCardFormatting.byteHex(telemetry.restoredMuxChannelMask),
            "muxWasRestored": telemetry.muxWasRestored,
            "capabilities": telemetry.capabilityNames,
            "pressureHpa": telemetry.pressurePascals.map {
                String(format: "%.3f", $0 / 100.0)
            } ?? "unavailable",
            "dewPointCelsius": telemetry.dewPointCelsius.map {
                String(format: "%.3f", $0)
            } ?? "unavailable",
            "readings": telemetry.readings.map(sensorReadingJSON),
        ]
    }

    private func sensorReadingJSON(
        _ reading: TimeCardSensorReading
    ) -> [String: Any] {
        [
            "kind": reading.kind.label,
            "route": SensorUIFormatting.route(reading),
            "present": reading.isPresent,
            "valid": reading.isValid,
            "configured": reading.isConfigured,
            "crcOk": reading.hasValidCRC,
            "calibrated": reading.isCalibrated,
            "imu": reading.isIMU,
            "temperatureCelsius": reading.temperatureCelsius.map {
                String(format: "%.3f", $0)
            } ?? "unavailable",
            "humidityPercent": reading.humidityPercent.map {
                String(format: "%.3f", $0)
            } ?? "unavailable",
            "raw0": TimeCardFormatting.hex(UInt64(reading.raw0)),
            "raw1": TimeCardFormatting.hex(UInt64(reading.raw1)),
            "raw2": TimeCardFormatting.hex(UInt64(reading.raw2)),
        ]
    }

    private func ledJSON(_ led: TimeCardLEDState) -> [String: Any] {
        [
            "led": led.led.label,
            "present": led.isPresent,
            "enabled": led.isEnabled,
            "rgb": led.rgbText,
            "red": Int(led.red),
            "green": Int(led.green),
            "blue": Int(led.blue),
            "globalCurrent": Int(led.globalCurrent),
            "muxChannelMask": TimeCardFormatting.byteHex(led.muxChannelMask),
            "faultStateValid": led.faultStateValid,
            "openOutputMask": TimeCardFormatting.hex(UInt64(led.openOutputMask)),
            "shortOutputMask": TimeCardFormatting.hex(UInt64(led.shortOutputMask)),
        ]
    }

    private func selfTestJSON(
        _ report: TimeCardSelfTestReport
    ) -> [String: Any] {
        [
            "runAt": TimeCardFormatting.date(report.runAt),
            "overallState": report.overallState,
            "summary": report.summary,
            "passCount": report.passCount,
            "warningCount": report.warningCount,
            "failCount": report.failCount,
            "gatedCount": report.gatedCount,
            "items": report.items.map {
                [
                    "name": $0.name,
                    "state": $0.state,
                    "detail": $0.detail,
                ]
            },
        ]
    }

    private var telemetryCSVText: String {
        var rows = [
            csvLine(["series", "timestamp", "value", "unit", "detail"])
        ]
        let generatedAt = TimeCardFormatting.date(Date())

        for point in monitor.samplingWindowHistory {
            rows.append(
                csvLine([
                    "sampling_window",
                    TimeCardFormatting.date(point.timestamp),
                    String(format: "%.0f", point.nanoseconds),
                    "ns",
                    "bracketed cross timestamp",
                ])
            )
        }

        if let snapshot = monitor.snapshot {
            rows.append(
                csvLine([
                    "clock_status",
                    generatedAt,
                    TimeCardFormatting.syncStatus(snapshot),
                    "state",
                    "source \(TimeCardFormatting.clockSource(snapshot))",
                ])
            )
            rows.append(
                csvLine([
                    "gnss_fix",
                    generatedAt,
                    snapshot.gnssFixName,
                    "state",
                    snapshot.gnssTelemetryAvailable
                        ? satelliteCSVDetail(snapshot)
                        : "summary unavailable in this FPGA image",
                ])
            )
        }

        for route in monitor.smaRoutes {
            rows.append(
                csvLine([
                    "sma_\(route.connector)",
                    generatedAt,
                    route.direction.label,
                    "route",
                    route.functionName,
                ])
            )
        }

        if let telemetry = monitor.sensorTelemetry {
            for reading in telemetry.readings {
                appendSensorCSVRows(
                    reading,
                    generatedAt: generatedAt,
                    rows: &rows
                )
            }
            if let pressure = telemetry.pressurePascals {
                rows.append(
                    csvLine([
                        "icp_10100_pressure",
                        generatedAt,
                        String(format: "%.3f", pressure / 100.0),
                        "hPa",
                        "OTP compensated pressure",
                    ])
                )
            }
            if let dewPoint = telemetry.dewPointCelsius {
                rows.append(
                    csvLine([
                        "dew_point",
                        generatedAt,
                        String(format: "%.3f", dewPoint),
                        "C",
                        "calculated from SHT3x humidity",
                    ])
                )
            }
        }

        for led in monitor.ledStates {
            rows.append(
                csvLine([
                    led.led.label.replacingOccurrences(of: " ", with: "_")
                        .lowercased(),
                    generatedAt,
                    led.rgbText,
                    "rgb",
                    "current \(led.globalCurrent), present \(led.isPresent)",
                ])
            )
        }

        if let report = monitor.selfTestReport {
            for item in report.items {
                rows.append(
                    csvLine([
                        "self_test",
                        TimeCardFormatting.date(report.runAt),
                        item.state,
                        "state",
                        "\(item.name): \(item.detail)",
                    ])
                )
            }
        }

        return rows.joined(separator: "\n")
    }

    private func appendSensorCSVRows(
        _ reading: TimeCardSensorReading,
        generatedAt: String,
        rows: inout [String]
    ) {
        let series = reading.kind.label.replacingOccurrences(
            of: " ",
            with: "_"
        ).lowercased()
        if let temperature = reading.temperatureCelsius {
            rows.append(
                csvLine([
                    "\(series)_temperature",
                    generatedAt,
                    String(format: "%.3f", temperature),
                    "C",
                    SensorUIFormatting.route(reading),
                ])
            )
        }
        if let humidity = reading.humidityPercent {
            rows.append(
                csvLine([
                    "\(series)_humidity",
                    generatedAt,
                    String(format: "%.3f", humidity),
                    "%RH",
                    SensorUIFormatting.route(reading),
                ])
            )
        }
    }

    private func satelliteCSVDetail(
        _ snapshot: TimeCardDeviceSnapshot
    ) -> String {
        guard let seen = snapshot.seenSatellites,
              let locked = snapshot.lockedSatellites else {
            return "satellite summary unavailable"
        }
        return "\(seen) seen, \(locked) locked"
    }

    private func csvLine(_ fields: [String]) -> String {
        fields.map(csvEscape).joined(separator: ",")
    }

    private func csvEscape(_ value: String) -> String {
        if value.contains(",") || value.contains("\"") ||
            value.contains("\n") {
            return "\"" + value.replacingOccurrences(of: "\"", with: "\"\"") + "\""
        }
        return value
    }
}

private struct OperationsView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var copiedDiagnostics = false
    @State private var diagnosticsExportMessage = ""
    @State private var selfTestExportMessage = ""
    @State private var supportBundleMessage = ""
    @State private var copiedSessionLog = false
    @State private var sessionLogExportMessage = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

                if let report = monitor.selfTestReport {
                    HStack(spacing: 14) {
                        MetricCard(
                            title: "Self-test",
                            value: report.overallState,
                            detail: report.summary,
                            systemImage: selfTestSymbol(for: report.overallState),
                            accent: selfTestColor(for: report.overallState)
                        )
                        MetricCard(
                            title: "Passed",
                            value: "\(report.passCount)",
                            detail: "Readback checks completed",
                            systemImage: "checkmark.circle"
                        )
                        MetricCard(
                            title: "Attention",
                            value: "\(report.attentionCount)",
                            detail: "Warnings or failures",
                            systemImage: report.attentionCount == 0
                                ? "checkmark.shield" : "exclamationmark.triangle",
                            accent: report.attentionCount == 0 ? .green : .orange
                        )
                    }
                }

                ConfigurationProfilesView()

                ControlCenterPanel(
                    title: "Configuration profile planner",
                    subtitle: "Windows built-in profiles mapped to macOS readiness"
                ) {
                    ConfigurationProfilePlannerView()
                }

                ControlCenterPanel(
                    title: "Production readiness self-test",
                    subtitle: "One-click read-only live check"
                ) {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Button("Run Self-Test") {
                                monitor.runReadOnlySelfTest()
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(
                                monitor.selfTestInProgress ||
                                    !monitor.serviceDetected
                            )

                            Button("Save Self-Test Report") {
                                selfTestExportMessage = saveOperationText(
                                    selfTestText,
                                    title: "Save Time Card Self-Test Report",
                                    defaultName:
                                        "TimeCardMacOS-SelfTest-\(supportBundleTimestampForFilename).txt",
                                    allowedContentTypes: [.plainText],
                                    successPrefix: "Saved self-test report"
                                )
                            }
                            .buttonStyle(.bordered)
                            .disabled(monitor.selfTestReport == nil)

                            if monitor.selfTestInProgress {
                                ProgressView()
                                    .controlSize(.small)
                            }

                            Spacer()

                            if let report = monitor.selfTestReport {
                                Text(TimeCardFormatting.date(report.runAt))
                                    .font(.caption.monospacedDigit())
                                    .foregroundStyle(.secondary)
                                    .textSelection(.enabled)
                            }
                        }

                        Text(
                            "The self-test opens the DriverKit user client, "
                                + "checks clock readback, and samples each "
                                + "advertised live subsystem without changing "
                                + "clock, SMA route, mux, or LED state."
                        )
                        .font(.caption)
                        .foregroundStyle(.secondary)

                        if !monitor.selfTestMessage.isEmpty {
                            Text(monitor.selfTestMessage)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                        }

                        if !selfTestExportMessage.isEmpty {
                            Text(selfTestExportMessage)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                        }

                        Divider()

                        VStack(spacing: 10) {
                            ForEach(displayedSelfTestItems) { item in
                                FeatureRow(
                                    name: item.name,
                                    state: item.state,
                                    note: item.detail
                                )
                            }
                        }
                    }
                }

                ControlCenterPanel(
                    title: "Diagnostics",
                    subtitle: "Copyable and saveable support report"
                ) {
                    VStack(alignment: .leading, spacing: 10) {
                        HStack {
                            Button("Copy Diagnostics") {
                                NSPasteboard.general.clearContents()
                                NSPasteboard.general.setString(
                                    diagnosticsText,
                                    forType: .string
                                )
                                copiedDiagnostics = true
                            }
                            .buttonStyle(.borderedProminent)

                            Button("Save Diagnostics") {
                                diagnosticsExportMessage = saveOperationText(
                                    diagnosticsText,
                                    title: "Save Time Card Diagnostics",
                                    defaultName:
                                        "TimeCardMacOS-Diagnostics-\(supportBundleTimestampForFilename).txt",
                                    allowedContentTypes: [.plainText],
                                    successPrefix: "Saved diagnostics"
                                )
                            }
                            .buttonStyle(.bordered)

                            Spacer()
                        }

                        if !diagnosticsExportMessage.isEmpty {
                            Text(diagnosticsExportMessage)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                        }

                        Text(copiedDiagnostics ? "Diagnostics copied." : diagnosticsText)
                            .font(.system(.caption, design: .monospaced))
                            .foregroundStyle(.secondary)
                            .textSelection(.enabled)
                    }
                }

                ControlCenterPanel(
                    title: "Session log and support bundle",
                    subtitle: "Windows-style support capture for field debugging"
                ) {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Button("Save Support ZIP") {
                                saveSupportBundle()
                            }
                            .buttonStyle(.borderedProminent)

                            Button("Save Session Log") {
                                sessionLogExportMessage = saveOperationText(
                                    sessionLogText,
                                    title: "Save Time Card Session Log",
                                    defaultName:
                                        "TimeCardMacOS-Session-\(supportBundleTimestampForFilename).txt",
                                    allowedContentTypes: [.plainText],
                                    successPrefix: "Saved session log"
                                )
                            }
                            .buttonStyle(.bordered)

                            Button("Save Session JSON") {
                                sessionLogExportMessage = saveOperationText(
                                    sessionLogJSONText,
                                    title: "Save Time Card Session JSON",
                                    defaultName:
                                        "TimeCardMacOS-Session-\(supportBundleTimestampForFilename).json",
                                    allowedContentTypes: [.json],
                                    successPrefix: "Saved session JSON"
                                )
                            }
                            .buttonStyle(.bordered)

                            Button("Copy Session Log") {
                                NSPasteboard.general.clearContents()
                                NSPasteboard.general.setString(
                                    sessionLogText,
                                    forType: .string
                                )
                                copiedSessionLog = true
                            }
                            .buttonStyle(.bordered)

                            Button("Clear Log") {
                                monitor.clearSessionLog()
                                copiedSessionLog = false
                            }
                            .buttonStyle(.bordered)

                            Spacer()

                            StatusPill(
                                "\(monitor.sessionLog.count) event(s)",
                                monitor.sessionLog.isEmpty ? .orange : .blue
                            )
                        }

                        Text(
                            supportBundleMessage.isEmpty
                                ? "The ZIP includes diagnostics, self-test, configuration profiles, FPGA core readiness, variant compatibility, receiver validation, serial inventory, serial preview, hardware UART, raw UART capture bytes, mixed receiver decode, receiver satellite records, sampling history, SMA, sensors, I2C, LEDs, and the session log."
                                : supportBundleMessage
                        )
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .textSelection(.enabled)

                        if !sessionLogExportMessage.isEmpty {
                            Text(sessionLogExportMessage)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                        }

                        Text(
                            copiedSessionLog
                                ? "Session log copied."
                                : sessionLogPreviewText
                        )
                        .font(.system(.caption, design: .monospaced))
                        .foregroundStyle(.secondary)
                        .textSelection(.enabled)
                        .padding(10)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(
                            Color.secondary.opacity(0.07),
                            in: RoundedRectangle(cornerRadius: 10)
                        )
                    }
                }
            }
            .padding(24)
        }
    }

    private func saveOperationText(
        _ text: String,
        title: String,
        defaultName: String,
        allowedContentTypes: [UTType],
        successPrefix: String
    ) -> String {
        let panel = NSSavePanel()
        panel.title = title
        panel.nameFieldStringValue = defaultName
        panel.allowedContentTypes = allowedContentTypes
        panel.canCreateDirectories = true

        guard panel.runModal() == .OK, let destinationURL = panel.url else {
            return "Save canceled."
        }

        do {
            try text.write(
                to: destinationURL,
                atomically: true,
                encoding: .utf8
            )
            NSWorkspace.shared.activateFileViewerSelecting([destinationURL])
            return "\(successPrefix) to \(destinationURL.path)."
        } catch {
            return "Save failed: \(error.localizedDescription)"
        }
    }

    private var diagnosticsText: String {
        guard let snapshot = monitor.snapshot else {
            return "Time Card diagnostics: no snapshot available. State: \(monitor.state)"
        }
        var lines = [
            "Time Card macOS diagnostics",
            "Board: \(snapshot.boardName)",
            "PCI: \(snapshot.pciIdentity) revision \(String(format: "0x%02x", snapshot.pciRevision & 0xff))",
            "Driver ABI: \(snapshot.abiVersion)",
            "Driver version: \(snapshot.driverVersionText)",
            "Layout: \(snapshot.layoutName)",
            "BAR0: \(TimeCardFormatting.hex(snapshot.barSize))",
            "Capabilities: \(snapshot.capabilityNames.joined(separator: ", "))",
            "Clock status: \(TimeCardFormatting.syncStatus(snapshot))",
            "Clock source: \(TimeCardFormatting.clockSource(snapshot))",
            "Sampling window: \(TimeCardFormatting.duration(snapshot.sampleWindowNanoseconds))",
            "Last update: \(TimeCardFormatting.date(monitor.lastUpdated))",
        ]
        if let report = monitor.selfTestReport {
            lines.append("Self-test: \(report.overallState), \(report.summary)")
            lines.append("Self-test run: \(TimeCardFormatting.date(report.runAt))")
            for item in report.items {
                lines.append(
                    "Self-test \(item.name): \(item.state) - \(item.detail)"
                )
            }
        }
        if snapshot.gnssTelemetryAvailable {
            lines.append("GNSS fix: \(snapshot.gnssFixName)")
            lines.append("Satellites: \(satelliteDiagnostic(snapshot))")
        } else {
            lines.append("GNSS summary: unavailable in this FPGA image")
        }
        if snapshot.todTelemetryAvailable {
            lines.append("UTC offset: \(snapshot.utcOffsetSeconds ?? 0) s")
        } else {
            lines.append("UTC summary: unavailable in this FPGA image")
        }
        if !monitor.smaRoutes.isEmpty {
            let routeText = monitor.smaRoutes.map {
                "SMA \($0.connector) \($0.direction.label) \($0.functionName)"
            }
            lines.append("SMA routes: \(routeText.joined(separator: ", "))")
        }
        if let sensors = monitor.sensorTelemetry {
            lines.append("Sensor blocks: \(sensors.validReadings.count)/\(sensors.readings.count) valid")
            lines.append(
                "Sensor capabilities: " +
                    (sensors.capabilityNames.isEmpty
                        ? "None"
                        : sensors.capabilityNames.joined(separator: ", "))
            )
            if let pressure = sensors.pressurePascals {
                lines.append("ICP-10100 pressure: \(TimeCardFormatting.hPa(pressure))")
            }
            lines.append("ICP-10100 OTP: \(SensorUIFormatting.otp(sensors.icp10100Otp))")
        }
        if let i2cStatus = monitor.i2cStatus {
            let knownDevices = i2cStatus.knownDeviceNames.isEmpty
                ? "none" : i2cStatus.knownDeviceNames.joined(separator: ", ")
            lines.append(
                "I2C status: control \(TimeCardFormatting.byteHex(i2cStatus.control)), status \(TimeCardFormatting.byteHex(i2cStatus.status)), known devices \(knownDevices)"
            )
        }
        if let i2cMux = monitor.i2cMux {
            lines.append(
                "I2C mux: present \(i2cMux.isPresent), channel \(TimeCardFormatting.byteHex(i2cMux.channelMask))"
            )
        }
        if !monitor.ledStates.isEmpty {
            let ledText = monitor.ledStates.map {
                "\($0.led.label) \($0.rgbText) current \($0.globalCurrent)"
            }
            lines.append("LED states: \(ledText.joined(separator: ", "))")
        }
        if let observation = monitor.uartObservation {
            lines.append("UART observe: \(observation.summary)")
        }
        if let write = monitor.uartWriteResult {
            lines.append("UART write: \(write.summary)")
        }
        if let read = monitor.uartReadResult {
            lines.append(
                "UART read: \(read.port.label), \(read.byteCount) byte(s), LSR \(read.lineStatusText)"
            )
        }
        return lines.joined(separator: "\n")
    }

    private enum SupportBundleError: LocalizedError {
        case zipFailed(Int32)

        var errorDescription: String? {
            switch self {
            case .zipFailed(let status):
                "Support ZIP creation failed with exit status \(status)."
            }
        }
    }

    private func saveSupportBundle() {
        let panel = NSSavePanel()
        panel.title = "Save Time Card Support Bundle"
        panel.nameFieldStringValue =
            "TimeCardMacOS-Support-\(supportBundleTimestampForFilename).zip"
        panel.allowedContentTypes = [.zip]
        panel.canCreateDirectories = true

        guard panel.runModal() == .OK, let destinationURL = panel.url else {
            supportBundleMessage = "Support bundle save canceled."
            return
        }

        do {
            try writeSupportBundle(to: destinationURL)
            supportBundleMessage =
                "Saved support bundle to \(destinationURL.path)."
            NSWorkspace.shared.activateFileViewerSelecting([destinationURL])
        } catch {
            supportBundleMessage =
                "Support bundle failed: \(error.localizedDescription)"
        }
    }

    private func writeSupportBundle(to destinationURL: URL) throws {
        let fileManager = FileManager.default
        let stagingURL = fileManager.temporaryDirectory.appendingPathComponent(
            "TimeCardMacOS-Support-\(UUID().uuidString)",
            isDirectory: true
        )
        try fileManager.createDirectory(
            at: stagingURL,
            withIntermediateDirectories: true
        )
        defer { try? fileManager.removeItem(at: stagingURL) }

        try writeSupportText(
            supportBundleManifestText,
            named: "manifest.json",
            into: stagingURL
        )
        try writeSupportText(diagnosticsText, named: "diagnostics.txt", into: stagingURL)
        try writeSupportText(sessionLogText, named: "session-log.txt", into: stagingURL)
        try writeSupportText(sessionLogJSONText, named: "session-log.json", into: stagingURL)
        try writeSupportText(liveSnapshotText, named: "live-snapshot.json", into: stagingURL)
        try writeSupportText(selfTestText, named: "self-test.txt", into: stagingURL)
        if let profile = monitor.configurationProfile {
            if let data = try? profile.encoded() {
                try writeSupportData(data, named: "staged-profile.json", into: stagingURL)
            } else {
                try writeSupportText(monitor.profileMessage, named: "profile-validation.txt", into: stagingURL)
            }
        }
        if let report = monitor.profileReport {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            encoder.dateEncodingStrategy = .iso8601
            try writeSupportData(encoder.encode(report), named: "profile-last-apply.json", into: stagingURL)
        }
        try writeSupportText(
            configurationProfilesText,
            named: "configuration-profiles.txt",
            into: stagingURL
        )
        try writeSupportText(
            fpgaCoreReadinessText,
            named: "fpga-core-readiness.txt",
            into: stagingURL
        )
        try writeSupportText(
            variantCompatibilityText,
            named: "variant-compatibility.txt",
            into: stagingURL
        )
        try writeSupportText(
            receiverValidationText,
            named: "receiver-validation.txt",
            into: stagingURL
        )
        try writeSupportText(serialPortsCSVText, named: "serial-ports.csv", into: stagingURL)
        try writeSupportText(serialCaptureText, named: "serial-capture.txt", into: stagingURL)
        try writeSupportText(hardwareUARTText, named: "hardware-uart.txt", into: stagingURL)
        try writeSupportText(receiverStreamText, named: "receiver-stream.txt", into: stagingURL)
        try writeSupportData(
            Data(monitor.uartCapture?.data ?? []),
            named: "hardware-uart-capture.bin",
            into: stagingURL
        )
        try writeSupportText(
            receiverSatellitesCSVText,
            named: "receiver-satellites.csv",
            into: stagingURL
        )
        try writeSupportText(samplingHistoryCSVText, named: "sampling-history.csv", into: stagingURL)
        try writeSupportText(smaRoutesCSVText, named: "sma-routes.csv", into: stagingURL)
        try writeSupportText(sensorCSVText, named: "sensors.csv", into: stagingURL)
        try writeSupportText(i2cText, named: "i2c.txt", into: stagingURL)
        try writeSupportText(ledCSVText, named: "leds.csv", into: stagingURL)

        if fileManager.fileExists(atPath: destinationURL.path) {
            try fileManager.removeItem(at: destinationURL)
        }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/ditto")
        process.arguments = ["-c", "-k", stagingURL.path, destinationURL.path]
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            throw SupportBundleError.zipFailed(process.terminationStatus)
        }
    }

    private func writeSupportText(
        _ text: String,
        named fileName: String,
        into directoryURL: URL
    ) throws {
        try text.write(
            to: directoryURL.appendingPathComponent(fileName),
            atomically: true,
            encoding: .utf8
        )
    }

    private func writeSupportData(
        _ data: Data,
        named fileName: String,
        into directoryURL: URL
    ) throws {
        try data.write(
            to: directoryURL.appendingPathComponent(fileName),
            options: .atomic
        )
    }

    private var supportBundleManifestText: String {
        var files = [
            "diagnostics.txt",
            "session-log.txt",
            "session-log.json",
            "live-snapshot.json",
            "self-test.txt",
            "configuration-profiles.txt",
            "fpga-core-readiness.txt",
            "variant-compatibility.txt",
            "receiver-validation.txt",
            "serial-ports.csv",
            "serial-capture.txt",
            "hardware-uart.txt",
            "receiver-stream.txt",
            "hardware-uart-capture.bin",
            "receiver-satellites.csv",
            "sampling-history.csv",
            "sma-routes.csv",
            "sensors.csv",
            "i2c.txt",
            "leds.csv",
        ]
        if let profile = monitor.configurationProfile {
            files.append((try? profile.encoded()) == nil ? "profile-validation.txt" : "staged-profile.json")
        }
        if monitor.profileReport != nil { files.append("profile-last-apply.json") }
        let object: [String: Any] = [
            "generatedAt": TimeCardFormatting.date(Date()),
            "app": "TimeCardMacOS",
            "state": String(describing: monitor.state),
            "serviceCount": monitor.services.count,
            "selectedServiceID": monitor.selectedServiceID.map(String.init)
                ?? "none",
            "files": files,
        ]
        return supportJSONText(object)
    }

    private var liveSnapshotText: String {
        guard let snapshot = monitor.snapshot else {
            return supportJSONText([
                "generatedAt": TimeCardFormatting.date(Date()),
                "state": String(describing: monitor.state),
                "error": monitor.errorMessage,
            ])
        }
        return supportJSONText([
            "generatedAt": TimeCardFormatting.date(Date()),
            "board": snapshot.boardName,
            "pciIdentity": snapshot.pciIdentity,
            "pciRevision": String(
                format: "0x%02x",
                snapshot.pciRevision & 0xff
            ),
            "driverVersion": snapshot.driverVersionText,
            "abiVersion": Int(snapshot.abiVersion),
            "layout": snapshot.layoutName,
            "bar0": TimeCardFormatting.hex(snapshot.barSize),
            "capabilities": snapshot.capabilityNames,
            "clockStatus": TimeCardFormatting.syncStatus(snapshot),
            "clockSource": TimeCardFormatting.clockSource(snapshot),
            "sampleWindowNanoseconds":
                String(snapshot.sampleWindowNanoseconds),
            "gnssTelemetryAvailable": snapshot.gnssTelemetryAvailable,
            "gnssFix": snapshot.gnssFixName,
            "seenSatellites": snapshot.seenSatellites.map(String.init)
                ?? "unavailable",
            "lockedSatellites": snapshot.lockedSatellites.map(String.init)
                ?? "unavailable",
            "todTelemetryAvailable": snapshot.todTelemetryAvailable,
            "utcOffsetSeconds": snapshot.utcOffsetSeconds.map(String.init)
                ?? "unavailable",
        ])
    }

    private func supportJSONText(_ object: [String: Any]) -> String {
        guard JSONSerialization.isValidJSONObject(object),
              let data = try? JSONSerialization.data(
                withJSONObject: object,
                options: [.prettyPrinted, .sortedKeys]
              ),
              let text = String(data: data, encoding: .utf8) else {
            return "{\n  \"error\" : \"JSON encoding failed\"\n}"
        }
        return text
    }

    private var selfTestText: String {
        guard let report = monitor.selfTestReport else {
            return "No self-test run yet."
        }
        var lines = [
            "Self-test run: \(TimeCardFormatting.date(report.runAt))",
            "Overall: \(report.overallState)",
            "Summary: \(report.summary)",
        ]
        for item in report.items {
            lines.append("\(item.state): \(item.name): \(item.detail)")
        }
        return lines.joined(separator: "\n")
    }

    private var configurationProfilesText: String {
        TimeCardConfigurationProfilePlan.reportText(for: monitor.snapshot)
    }

    private var fpgaCoreReadinessText: String {
        TimeCardFPGACoreReadiness.reportText(for: monitor.snapshot)
    }

    private var variantCompatibilityText: String {
        TimeCardBoardVariantCompatibility.reportText(for: monitor.snapshot)
    }

    private var receiverValidationText: String {
        let data = monitor.uartCapture?.data ??
            monitor.uartReadResult?.data ??
            monitor.serialCapture?.data ?? []
        let messages = ReceiverStreamMessage.parse(from: data)
        let nmeaText = String(decoding: data, as: UTF8.self)
        let nmeaSentences = nmeaText
            .split(whereSeparator: \.isNewline)
            .compactMap { NMEASentence.parse(String($0)) }
        let ubxFrames = TimeCardUBXFrame.parseFrames(from: data)
        let sourceDescription = data.isEmpty
            ? "No receiver stream bytes recorded in this support bundle."
            : "Latest captured receiver stream data in this support bundle."
        return ReceiverValidationChecklist.reportText(
            nmeaSentences: nmeaSentences,
            ubxFrames: ubxFrames,
            mixedMessages: messages,
            hardwareUARTAvailable: monitor.snapshot?.supportsUART == true,
            hardwareUARTWriteAvailable:
                monitor.snapshot?.supportsUARTWrite == true,
            sourceDescription: sourceDescription
        )
    }

    private var sessionLogText: String {
        guard !monitor.sessionLog.isEmpty else {
            return "No session events recorded."
        }
        return monitor.sessionLog.map { entry in
            "\(TimeCardFormatting.date(entry.timestamp)) [\(entry.severity.rawValue)] \(entry.category): \(entry.message)"
        }.joined(separator: "\n")
    }

    private var sessionLogJSONText: String {
        let object: [String: Any] = [
            "generatedAt": TimeCardFormatting.date(Date()),
            "eventCount": monitor.sessionLog.count,
            "events": monitor.sessionLog.map { entry in
                [
                    "timestamp": TimeCardFormatting.date(entry.timestamp),
                    "severity": entry.severity.rawValue,
                    "category": entry.category,
                    "message": entry.message,
                ]
            },
        ]
        return supportJSONText(object)
    }

    private var sessionLogPreviewText: String {
        let text = monitor.sessionLog.suffix(12).map { entry in
            "\(TimeCardFormatting.date(entry.timestamp)) [\(entry.severity.rawValue)] \(entry.category): \(entry.message)"
        }.joined(separator: "\n")
        return text.isEmpty ? "No session events recorded." : text
    }

    private var serialPortsCSVText: String {
        var rows = [supportCSVLine([
            "display_name",
            "callout_device",
            "dialin_device",
            "tty_device",
            "bsd_type",
        ])]
        for port in monitor.serialPorts {
            rows.append(supportCSVLine([
                port.displayName,
                port.calloutDevice,
                port.dialinDevice ?? "",
                port.ttyDevice ?? "",
                port.bsdType ?? "",
            ]))
        }
        return rows.joined(separator: "\n")
    }

    private var serialCaptureText: String {
        guard let capture = monitor.serialCapture else {
            return "No serial preview capture recorded."
        }
        return [
            "Port: \(capture.portPath)",
            "Baud: \(capture.baudRate)",
            "Captured at: \(TimeCardFormatting.date(capture.capturedAt))",
            "Duration: \(String(format: "%.2f", capture.durationSeconds)) s",
            "Bytes: \(capture.byteCount)",
            "",
            capture.text.isEmpty
                ? "No bytes arrived during the preview window."
                : capture.text,
        ].joined(separator: "\n")
    }

    private var hardwareUARTText: String {
        var lines: [String] = []
        if let observation = monitor.uartObservation {
            lines.append("Observation")
            lines.append("Port: \(observation.port.label)")
            lines.append("Present: \(observation.isPresent)")
            lines.append("Activity: \(observation.hasActivity)")
            lines.append("Line status: \(observation.lineStatusText)")
            lines.append("Timeout: \(observation.timeoutMilliseconds) ms")
        } else {
            lines.append("No hardware UART observation recorded.")
        }
        lines.append("")
        if let write = monitor.uartWriteResult {
            lines.append("Write")
            lines.append("Port: \(write.port.label)")
            lines.append("Bytes: \(write.byteCount)/\(write.requestedByteCount)")
            lines.append("Complete: \(write.complete)")
            lines.append("Line status: \(write.lineStatusText)")
            lines.append("Timeout: \(write.timeoutMilliseconds) ms")
        } else {
            lines.append("No hardware UART write recorded.")
        }
        lines.append("")
        if let read = monitor.uartReadResult {
            lines.append("Read")
            lines.append("Port: \(read.port.label)")
            lines.append("Bytes: \(read.byteCount)")
            lines.append("Line status: \(read.lineStatusText)")
            lines.append("Timeout: \(read.timeoutMilliseconds) ms")
            lines.append("")
            lines.append(read.data.isEmpty ? "No bytes arrived." : read.dataHex)
        } else {
            lines.append("No hardware UART read recorded.")
        }
        lines.append("")
        if let capture = monitor.uartCapture {
            lines.append("Capture")
            lines.append("Port: \(capture.port.label)")
            lines.append("Baud: \(capture.baudRate)")
            lines.append("Captured at: \(TimeCardFormatting.date(capture.capturedAt))")
            lines.append(
                "Duration: \(String(format: "%.2f", capture.durationSeconds)) s"
            )
            lines.append(
                "Requested duration: \(String(format: "%.2f", capture.requestedDurationSeconds)) s"
            )
            lines.append("Stop reason: \(capture.stopReason)")
            lines.append("Bytes: \(capture.byteCount)")
            lines.append("Read windows: \(capture.readCount)")
            lines.append("Empty windows: \(capture.emptyReadCount)")
            lines.append("Line status: \(capture.lineStatusText)")
            lines.append("")
            lines.append(
                capture.data.isEmpty
                    ? "No bytes arrived during the capture window."
                    : capture.dataHex
            )
        } else {
            lines.append("No hardware UART capture recorded.")
        }
        return lines.joined(separator: "\n")
    }

    private var receiverStreamText: String {
        let data = monitor.uartCapture?.data ??
            monitor.uartReadResult?.data ??
            monitor.serialCapture?.data ?? []
        let messages = ReceiverStreamMessage.parse(from: data)
        guard !messages.isEmpty else {
            return "No mixed receiver stream messages decoded from latest UART capture or read."
        }
        let header = [
            "Receiver stream summary",
            "Protocols: \(ReceiverStreamDecoder.protocolSummary(messages: messages))",
            "Checksums: \(ReceiverStreamDecoder.checksumSummary(messages: messages))",
            "RTCM3: \(ReceiverStreamDecoder.rtcmSummary(messages: messages))",
        ]
        let rows = messages.map { message in
            [
                "\(message.offsetText) \(message.protocolName) \(message.name)",
                "Bytes: \(message.byteCount)",
                "Checksum: \(message.checksumState.label)",
                "Summary: \(message.summary)",
                "Detail: \(message.detail)",
            ].joined(separator: "\n")
        }
        return (header + rows).joined(separator: "\n\n")
    }

    private var receiverSatelliteSignals: [ReceiverSatelliteSignal] {
        let data = monitor.uartCapture?.data ??
            monitor.uartReadResult?.data ??
            monitor.serialCapture?.data ?? []
        let nmeaText = String(decoding: data, as: UTF8.self)
        let nmeaSentences = nmeaText
            .split(whereSeparator: \.isNewline)
            .compactMap { NMEASentence.parse(String($0)) }
        let ubxFrames = TimeCardUBXFrame.parseFrames(from: data)
        return ReceiverStreamDecoder.satelliteSignals(
            nmeaSentences: nmeaSentences,
            ubxFrames: ubxFrames
        )
    }

    private var receiverSatellitesCSVText: String {
        ReceiverStreamDecoder.satelliteCSVText(
            signals: receiverSatelliteSignals,
            csvLine: supportCSVLine
        )
    }

    private var samplingHistoryCSVText: String {
        var rows = [supportCSVLine(["timestamp", "nanoseconds"])]
        for point in monitor.samplingWindowHistory {
            rows.append(supportCSVLine([
                TimeCardFormatting.date(point.timestamp),
                String(format: "%.0f", point.nanoseconds),
            ]))
        }
        return rows.joined(separator: "\n")
    }

    private var smaRoutesCSVText: String {
        var rows = [supportCSVLine([
            "connector",
            "direction",
            "function",
            "function_name",
            "present",
            "fixed_direction",
            "fixed_function",
            "input_map",
            "output_map",
        ])]
        for route in monitor.smaRoutes {
            rows.append(supportCSVLine([
                String(route.connector),
                route.direction.label,
                TimeCardFormatting.hex(UInt64(route.function)),
                route.functionName,
                String(route.isPresent),
                String(route.isFixedDirection),
                String(route.isFixedFunction),
                TimeCardFormatting.hex(UInt64(route.inputMap)),
                TimeCardFormatting.hex(UInt64(route.outputMap)),
            ]))
        }
        return rows.joined(separator: "\n")
    }

    private var sensorCSVText: String {
        var rows = [supportCSVLine([
            "kind",
            "route",
            "present",
            "valid",
            "temperature_c",
            "humidity_percent",
            "raw0",
            "raw1",
            "raw2",
        ])]
        guard let telemetry = monitor.sensorTelemetry else {
            return rows.joined(separator: "\n")
        }
        for reading in telemetry.readings {
            rows.append(supportCSVLine([
                reading.kind.label,
                SensorUIFormatting.route(reading),
                String(reading.isPresent),
                String(reading.isValid),
                reading.temperatureCelsius.map {
                    String(format: "%.3f", $0)
                } ?? "",
                reading.humidityPercent.map {
                    String(format: "%.3f", $0)
                } ?? "",
                TimeCardFormatting.hex(UInt64(reading.raw0)),
                TimeCardFormatting.hex(UInt64(reading.raw1)),
                TimeCardFormatting.hex(UInt64(reading.raw2)),
            ]))
        }
        return rows.joined(separator: "\n")
    }

    private var i2cText: String {
        var lines: [String] = []
        if let status = monitor.i2cStatus {
            lines.append("I2C controller")
            lines.append("present: \(status.isPresent)")
            lines.append("enabled: \(status.isEnabled)")
            lines.append("bus busy: \(status.isBusBusy)")
            lines.append("control: \(TimeCardFormatting.byteHex(status.control))")
            lines.append("status: \(TimeCardFormatting.byteHex(status.status))")
            lines.append(
                "interrupt status: \(TimeCardFormatting.byteHex(status.interruptStatus))"
            )
            lines.append(
                "known devices: \(status.knownDeviceNames.joined(separator: ", "))"
            )
        } else {
            lines.append("No I2C status snapshot recorded.")
        }
        if let mux = monitor.i2cMux {
            lines.append("")
            lines.append("I2C mux")
            lines.append("present: \(mux.isPresent)")
            lines.append(
                "channel mask: \(TimeCardFormatting.byteHex(mux.channelMask))"
            )
            lines.append(
                "controller status: \(TimeCardFormatting.byteHex(mux.controllerStatus))"
            )
        }
        if let transfer = monitor.i2cTransfer {
            lines.append("")
            lines.append("Last I2C transfer")
            lines.append("address: \(transfer.addressText)")
            lines.append("length: \(transfer.length)")
            lines.append("data: \(transfer.dataHex)")
        }
        return lines.joined(separator: "\n")
    }

    private var ledCSVText: String {
        var rows = [supportCSVLine([
            "led",
            "present",
            "enabled",
            "rgb",
            "current",
            "open_mask",
            "short_mask",
        ])]
        for led in monitor.ledStates {
            rows.append(supportCSVLine([
                led.led.label,
                String(led.isPresent),
                String(led.isEnabled),
                led.rgbText,
                String(led.globalCurrent),
                TimeCardFormatting.hex(UInt64(led.openOutputMask)),
                TimeCardFormatting.hex(UInt64(led.shortOutputMask)),
            ]))
        }
        return rows.joined(separator: "\n")
    }

    private var supportBundleTimestampForFilename: String {
        let formatter = DateFormatter()
        formatter.calendar = Calendar(identifier: .gregorian)
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyyMMdd-HHmmss"
        return formatter.string(from: Date())
    }

    private func supportCSVLine(_ fields: [String]) -> String {
        fields.map(supportCSVField).joined(separator: ",")
    }

    private func supportCSVField(_ value: String) -> String {
        if value.contains(",") || value.contains("\"") ||
            value.contains("\n") {
            return "\"" + value.replacingOccurrences(of: "\"", with: "\"\"") + "\""
        }
        return value
    }

    private var displayedSelfTestItems: [TimeCardSelfTestItem] {
        if let report = monitor.selfTestReport {
            return report.items
        }
        return [
            TimeCardSelfTestItem(
                "Driver service",
                severity: monitor.serviceDetected ? .pass : .waiting,
                detail: "Checks DriverKit service discovery."
            ),
            TimeCardSelfTestItem(
                "User-client access",
                severity: monitor.state == .connected ? .pass : .waiting,
                detail: "Opening the user client proves entitlement and profile wiring."
            ),
            TimeCardSelfTestItem(
                "Clock read and cross-timestamp",
                severity: monitor.snapshot == nil ? .waiting : .pass,
                detail: "Uses the same ABI path as the CLI status and get commands."
            ),
            TimeCardSelfTestItem(
                "Live subsystem readback",
                severity: monitor.snapshot == nil ? .waiting : .gated,
                detail: "Run the self-test to sample SMA, sensors, I2C, mux, and LEDs."
            ),
        ]
    }

    private func selfTestColor(for state: String) -> Color {
        switch state.lowercased() {
        case "pass": .green
        case "warning": .orange
        case "fail": .red
        default: .secondary
        }
    }

    private func selfTestSymbol(for state: String) -> String {
        switch state.lowercased() {
        case "pass": "checkmark.shield"
        case "warning": "exclamationmark.triangle"
        case "fail": "xmark.octagon"
        default: "clock.badge.questionmark"
        }
    }

    private func satelliteDiagnostic(_ snapshot: TimeCardDeviceSnapshot) -> String {
        guard let seen = snapshot.seenSatellites,
              let locked = snapshot.lockedSatellites else {
            return "unavailable"
        }
        return "\(seen) seen, \(locked) locked"
    }
}

private struct ConfigurationProfilePlannerView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var copiedPlan = false

    private let columns = [
        GridItem(.adaptive(minimum: 300), spacing: 12)
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(
                    "Profiles mirror the Windows Control Center catalog. "
                        + "This view is intentionally read-only until each "
                        + "write path has readback, rollback, and persistence "
                        + "warnings."
                )
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

                Spacer()

                Button("Copy Profile Plan") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(
                        TimeCardConfigurationProfilePlan.reportText(
                            for: monitor.snapshot
                        ),
                        forType: .string
                    )
                    copiedPlan = true
                }
                .buttonStyle(.bordered)
            }

            if copiedPlan {
                Text("Profile plan copied.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            LazyVGrid(columns: columns, alignment: .leading, spacing: 12) {
                ForEach(TimeCardConfigurationProfilePlan.plans(for: monitor.snapshot)) { plan in
                    ConfigurationProfileCard(plan: plan)
                }
            }
        }
    }
}

private struct ConfigurationProfileCard: View {
    let plan: TimeCardConfigurationProfilePlan

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                Text(plan.name)
                    .font(.headline)
                Spacer()
                StatusPill(plan.state, accent)
            }

            Text(plan.description)
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

            Text(plan.detail)
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

            Divider()

            VStack(alignment: .leading, spacing: 6) {
                Text("Planned actions")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                ForEach(plan.actions, id: \.self) { action in
                    Label(action, systemImage: "checklist")
                        .font(.caption)
                        .textSelection(.enabled)
                }
            }

            VStack(alignment: .leading, spacing: 6) {
                Text("Before one-click apply")
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                ForEach(plan.blockers.isEmpty ? ["None"] : plan.blockers, id: \.self) { blocker in
                    Label(
                        blocker,
                        systemImage: plan.blockers.isEmpty
                            ? "checkmark.circle" : "lock"
                    )
                    .font(.caption)
                    .textSelection(.enabled)
                }
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            Color.secondary.opacity(0.07),
            in: RoundedRectangle(cornerRadius: 12)
        )
    }

    private var accent: Color {
        switch plan.state {
        case "Manual-ready", "UART-ready":
            return .green
        case "ABI needed", "Planning only", "Backend pending", "Planned":
            return .orange
        case "Blocked":
            return .red
        default:
            return .secondary
        }
    }
}

private struct SubsystemMapView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

                ControlCenterPanel(
                    title: "Live Time Card topology",
                    subtitle: "Windows topology view rebuilt as native SwiftUI"
                ) {
                    TimeCardTopologyDiagramView(snapshot: monitor.snapshot)
                }

                ControlCenterPanel(
                    title: "FPGA core readiness matrix",
                    subtitle: "Windows FPGA Engines features mapped to this card"
                ) {
                    FPGACoreReadinessMatrixView(snapshot: monitor.snapshot)
                }

                ControlCenterPanel(
                    title: "Board variant compatibility",
                    subtitle: "Driver profile coverage for supported Time Card variants"
                ) {
                    BoardVariantCompatibilityView(snapshot: monitor.snapshot)
                }

                ControlCenterPanel(
                    title: "Subsystem capability map",
                    subtitle: "Windows topology imported as a macOS readiness map"
                ) {
                    LazyVGrid(
                        columns: [
                            GridItem(.adaptive(minimum: 220), spacing: 12)
                        ],
                        spacing: 12
                    ) {
                        SubsystemCard("PCIe", "Live", "Device identity and BAR")
                        SubsystemCard("PHC", "Live", "Read, set, cross-timestamp")
                        SubsystemCard(
                            "ToD",
                            monitor.snapshot?.capabilityNames.contains("ToD") == true
                                ? "Live" : "Not present",
                            "Version and status when fitted"
                        )
                        SubsystemCard(
                            "GNSS",
                            monitor.snapshot?.supportsUART == true
                                ? "Available" : "Gated",
                            "UART and UBX poll labs"
                        )
                        SubsystemCard(
                            "SMA",
                            monitor.snapshot?.capabilityNames.contains("SMA") == true
                                ? "Live" : "Gated",
                            "Route query and guarded route updates"
                        )
                        SubsystemCard(
                            "I2C",
                            monitor.snapshot?.capabilityNames.contains("I2C") == true
                                ? "Live" : "Gated",
                            "Controller status, scan, reads, mux, and LED policy"
                        )
                        SubsystemCard(
                            "Sensors",
                            monitor.snapshot?.capabilityNames.contains("Sensors") == true
                                ? "Live" : "Gated",
                            "Environmental telemetry with IMU expansion next"
                        )
                        SubsystemCard("FPGA flash", "Gated", "Needs flash ABI")
                    }
                }
            }
            .padding(24)
        }
    }
}

private struct TimeCardTopologyDiagramView: View {
    let snapshot: TimeCardDeviceSnapshot?

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 10) {
                topologyNode(
                    title: "PCIe",
                    state: snapshot == nil ? "Waiting" : "Live",
                    detail: snapshot?.pciIdentity ?? "No device selected",
                    color: snapshot == nil ? .secondary : .green
                )
                topologyLink()
                topologyNode(
                    title: "DriverKit",
                    state: snapshot == nil ? "Waiting" : "Connected",
                    detail: snapshot.map { "ABI v\($0.abiVersion)" }
                        ?? "User client not open",
                    color: snapshot == nil ? .secondary : .green
                )
                topologyLink()
                topologyNode(
                    title: "PHC",
                    state: hasCapability("Clock read") ? "Live" : "Gated",
                    detail: clockDetail,
                    color: hasCapability("Clock read") ? .green : .orange
                )
                topologyLink()
                topologyNode(
                    title: "macOS service",
                    state: hasCapability("Clock set") ? "Ready" : "Read-only",
                    detail: "Control Center and future discipline daemon",
                    color: hasCapability("Clock set") ? .teal : .secondary
                )
            }

            HStack(spacing: 10) {
                topologyNode(
                    title: "GNSS",
                    state: gnssState,
                    detail: gnssDetail,
                    color: gnssColor
                )
                topologyNode(
                    title: "ToD",
                    state: hasCapability("ToD") ? "Present" : "Not present",
                    detail: snapshot?.todTelemetryAvailable == true
                        ? "UTC and leap summaries live"
                        : "Summary registers gated",
                    color: hasCapability("ToD") ? .cyan : .secondary
                )
                topologyNode(
                    title: "SMA I/O",
                    state: hasCapability("SMA") ? "Live" : "Gated",
                    detail: "Connector route fabric",
                    color: hasCapability("SMA") ? .orange : .secondary
                )
                topologyNode(
                    title: "I2C fabric",
                    state: hasCapability("I2C") ? "Live" : "Gated",
                    detail: "Mux, probes, LEDs, and sensors",
                    color: hasCapability("I2C") ? .yellow : .secondary
                )
            }

            HStack(spacing: 10) {
                topologyNode(
                    title: "Sensors",
                    state: hasCapability("Sensors") ? "Live" : "Gated",
                    detail: "Temperature, humidity, pressure, IMU path",
                    color: hasCapability("Sensors") ? .pink : .secondary
                )
                topologyNode(
                    title: "Status LEDs",
                    state: hasCapability("LEDs") ? "Live" : "Gated",
                    detail: "Manual and policy-driven RGB state",
                    color: hasCapability("LEDs") ? .mint : .secondary
                )
                topologyNode(
                    title: "FPGA cores",
                    state: hasCapability("ToD") || hasCapability("SMA")
                        ? "Partially live" : "Gated",
                    detail: "PPS, parser, counters, generators, flash roadmap",
                    color: hasCapability("ToD") || hasCapability("SMA")
                        ? .purple : .secondary
                )
            }

            Text(
                "Green nodes are live through the current DriverKit user "
                    + "client. Orange or purple nodes are partially available "
                    + "or waiting on a guarded ABI. Gray nodes are absent or "
                    + "not exposed by this FPGA image."
            )
            .font(.caption)
            .foregroundStyle(.secondary)
            .textSelection(.enabled)
        }
    }

    private func topologyNode(
        title: String,
        state: String,
        detail: String,
        color: Color
    ) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                Text(title)
                    .font(.headline)
                Spacer()
                StatusPill(state, color)
            }
            Text(detail)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(2)
                .textSelection(.enabled)
        }
        .padding(12)
        .frame(maxWidth: .infinity, minHeight: 92, alignment: .leading)
        .background(
            color.opacity(0.10),
            in: RoundedRectangle(cornerRadius: 14)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(color.opacity(0.22), lineWidth: 1)
        )
    }

    private func topologyLink() -> some View {
        Image(systemName: "arrow.right")
            .font(.caption.weight(.semibold))
            .foregroundStyle(.secondary)
    }

    private func hasCapability(_ name: String) -> Bool {
        snapshot?.capabilityNames.contains(name) == true
    }

    private var clockDetail: String {
        guard let snapshot else { return "No PHC snapshot yet" }
        return "\(TimeCardFormatting.syncStatus(snapshot)), source \(TimeCardFormatting.clockSource(snapshot))"
    }

    private var gnssState: String {
        if snapshot?.gnssTelemetryAvailable == true { return "Live" }
        if snapshot?.supportsUART == true { return "UART-ready" }
        return "Gated"
    }

    private var gnssDetail: String {
        if snapshot?.gnssTelemetryAvailable == true {
            return snapshot?.gnssFixName ?? "GNSS summary live"
        }
        if snapshot?.supportsUART == true {
            return "Receiver traffic available through UART labs"
        }
        return "GNSS path is not advertised"
    }

    private var gnssColor: Color {
        if snapshot?.gnssTelemetryAvailable == true { return .green }
        if snapshot?.supportsUART == true { return .teal }
        return .secondary
    }
}

private struct FPGACoreReadinessMatrixView: View {
    let snapshot: TimeCardDeviceSnapshot?

    private var rows: [TimeCardFPGACoreReadiness] {
        TimeCardFPGACoreReadiness.rows(for: snapshot)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 10) {
                readinessSummaryPill("Live", rows.filter { $0.state == "Live" }.count, .green)
                readinessSummaryPill("Ready", rows.filter { $0.state == "Ready" }.count, .teal)
                readinessSummaryPill(
                    "ABI needed",
                    rows.filter { $0.state == "ABI needed" }.count,
                    .orange
                )
                readinessSummaryPill("Roadmap", rows.filter { $0.state == "Roadmap" }.count, .purple)
                Spacer()
                StatusPill(
                    snapshot.map { "ABI v\($0.abiVersion)" } ?? "No snapshot",
                    snapshot == nil ? .secondary : .blue
                )
            }

            ForEach(TimeCardFPGACoreReadiness.groupedRows(for: snapshot), id: \.0) { group in
                VStack(alignment: .leading, spacing: 9) {
                    Text(group.0)
                        .font(.subheadline.weight(.semibold))
                        .foregroundStyle(.secondary)
                    ForEach(group.1) { row in
                        FPGACoreReadinessRow(row: row)
                    }
                }
            }

            Text(
                "This matrix mirrors the Windows FPGA Engines catalog but "
                    + "only marks a core live when the current DriverKit ABI "
                    + "and FPGA image expose readback evidence."
            )
            .font(.caption)
            .foregroundStyle(.secondary)
            .textSelection(.enabled)
        }
    }

    private func readinessSummaryPill(
        _ label: String,
        _ count: Int,
        _ color: Color
    ) -> some View {
        HStack(spacing: 6) {
            Text("\(count)")
                .font(.caption.weight(.bold))
                .monospacedDigit()
            Text(label)
                .font(.caption)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
        .foregroundStyle(color)
        .background(color.opacity(0.12), in: Capsule())
        .overlay(Capsule().stroke(color.opacity(0.24), lineWidth: 1))
    }
}

private struct FPGACoreReadinessRow: View {
    let row: TimeCardFPGACoreReadiness

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: row.symbol)
                .font(.system(size: 20, weight: .semibold))
                .foregroundStyle(row.accent)
                .frame(width: 34, height: 34)
                .background(
                    row.accent.opacity(0.12),
                    in: RoundedRectangle(cornerRadius: 10)
                )

            VStack(alignment: .leading, spacing: 5) {
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text(row.core)
                        .font(.headline)
                    StatusPill(row.state, row.accent)
                    Spacer()
                    Text(row.windowsFeature)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Text(row.evidence)
                    .font(.caption)
                    .foregroundStyle(.primary)
                    .textSelection(.enabled)

                Text(row.nextStep)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }
        }
        .padding(12)
        .background(
            row.accent.opacity(0.07),
            in: RoundedRectangle(cornerRadius: 14)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(row.accent.opacity(0.18), lineWidth: 1)
        )
    }
}

private struct TimeCardFPGACoreReadiness: Identifiable {
    let id: String
    let family: String
    let core: String
    let windowsFeature: String
    let state: String
    let evidence: String
    let nextStep: String
    let symbol: String

    var accent: Color {
        switch state {
        case "Live": .green
        case "Ready": .teal
        case "Partial", "ABI needed": .orange
        case "Roadmap": .purple
        case "Waiting": .blue
        default: .secondary
        }
    }

    static func groupedRows(
        for snapshot: TimeCardDeviceSnapshot?
    ) -> [(String, [Self])] {
        let rows = rows(for: snapshot)
        return [
            "Clock and time scale",
            "Receiver and serial",
            "Signal I/O",
            "Board services",
            "Programming and maintenance",
        ].compactMap { family in
            let matches = rows.filter { $0.family == family }
            return matches.isEmpty ? nil : (family, matches)
        }
    }

    static func rows(for snapshot: TimeCardDeviceSnapshot?) -> [Self] {
        let connected = snapshot != nil
        let capabilities = snapshot?.capabilityNames ?? []

        func has(_ name: String) -> Bool {
            capabilities.contains(name)
        }

        func status(
            live: Bool,
            ready: Bool = false,
            partial: Bool = false
        ) -> String {
            if live { return "Live" }
            if ready { return "Ready" }
            if partial { return "Partial" }
            return connected ? "ABI needed" : "Waiting"
        }

        func row(
            _ id: String,
            _ family: String,
            _ core: String,
            _ windowsFeature: String,
            _ state: String,
            _ evidence: String,
            _ nextStep: String,
            _ symbol: String
        ) -> Self {
            Self(
                id: id,
                family: family,
                core: core,
                windowsFeature: windowsFeature,
                state: state,
                evidence: evidence,
                nextStep: nextStep,
                symbol: symbol
            )
        }

        let clockEvidence: String
        if let snapshot {
            clockEvidence = "Clock v\(TimeCardFormatting.hex(UInt64(snapshot.clockVersion))) reports \(TimeCardFormatting.syncStatus(snapshot)); source \(TimeCardFormatting.clockSource(snapshot))."
        } else {
            clockEvidence = "Waiting for a live DriverKit snapshot."
        }

        let todEvidence: String
        if let snapshot, snapshot.todTelemetryAvailable {
            todEvidence = "ToD v\(TimeCardFormatting.hex(UInt64(snapshot.todVersion))) exposes UTC and leap summaries."
        } else if has("ToD") {
            todEvidence = "ToD capability is advertised, but summary fields are gated on this image."
        } else {
            todEvidence = "ToD summary registers are not exposed by this FPGA image."
        }

        let gnssEvidence: String
        if let snapshot, snapshot.gnssTelemetryAvailable {
            let satellites = snapshot.seenSatellites.map { "\($0) seen" } ?? "satellites unavailable"
            let locked = snapshot.lockedSatellites.map { "\($0) locked" } ?? "locks unavailable"
            gnssEvidence = "\(snapshot.gnssFixName), \(satellites), \(locked)."
        } else if snapshot?.supportsUART == true {
            gnssEvidence = "GNSS register summary is absent, but the receiver path is reachable through UART labs."
        } else {
            gnssEvidence = connected
                ? "GNSS receiver access is not advertised."
                : "Waiting for a live DriverKit snapshot."
        }

        return [
            row(
                "phc-core",
                "Clock and time scale",
                "Precision clock core",
                "PHC monitor",
                status(live: has("Clock read")),
                clockEvidence,
                has("Clock set")
                    ? "Use guarded source selection in Precision Clock; system discipline remains separate."
                    : "Expose the guarded clock-set selector for discipline workflows.",
                "clock"
            ),
            row(
                "cross-timestamp",
                "Clock and time scale",
                "Cross timestamp",
                "Clock correlation",
                status(live: has("Cross timestamp")),
                has("Cross timestamp")
                    ? "DriverKit exposes bounded host/card correlation."
                    : "Cross timestamp capability is not advertised.",
                "Keep sampling-window history in support bundles for drift analysis.",
                "arrow.triangle.2.circlepath"
            ),
            row(
                "tod-parser",
                "Clock and time scale",
                "ToD parser",
                "UTC and leap summary",
                snapshot?.todTelemetryAvailable == true
                    ? "Live" : (has("ToD") ? "Partial" : (connected ? "Unavailable" : "Waiting")),
                todEvidence,
                "Validate the time-scale contract before automated profile apply.",
                "calendar.badge.clock"
            ),
            row(
                "clock-source-selector",
                "Clock and time scale",
                "Clock-source selector",
                "Configured and active source",
                status(live: false, ready: snapshot?.supportsClockSource == true),
                snapshot?.supportsClockSource == true
                    ? "ABI v10 exposes the source mask, configured input, and active input."
                    : "Clock-source controls require ABI v10 and a supported clock version.",
                "Apply with confirmation in Precision Clock or Generators; stale writes are rejected.",
                "arrow.triangle.branch"
            ),
            row(
                "pps-master-slave",
                "Clock and time scale",
                "PPS master/slave",
                "PPS controls",
                connected ? "ABI needed" : "Waiting",
                has("SMA") && has("Clock set")
                    ? "Route and clock-set primitives are present; PPS phase control is not exposed yet."
                    : "Needs SMA route and PPS phase measurement exposure.",
                "Add versioned PPS generator, capture, and phase selectors.",
                "waveform.path.ecg"
            ),
            row(
                "timestamp-channels",
                "Clock and time scale",
                "Timestamp channels",
                "Timestamp laboratory",
                connected ? "ABI needed" : "Waiting",
                "The Windows tool exposes timestamp channels; macOS has no channel table ABI yet.",
                "Define bounded event readback and channel capability masks.",
                "timer"
            ),
            row(
                "gnss-summary",
                "Receiver and serial",
                "GNSS summary",
                "Fix and satellite dashboard",
                snapshot?.gnssTelemetryAvailable == true
                    ? "Live" : (snapshot?.supportsUART == true ? "Partial" : (connected ? "Unavailable" : "Waiting")),
                gnssEvidence,
                "Prefer UBX NAV-PVT, NAV-SAT, NAV-TIMELS, and TIM-TP when registers are absent.",
                "location.north.line"
            ),
            row(
                "gnss-uart",
                "Receiver and serial",
                "GNSS UART",
                "Receiver poll and capture",
                status(
                    live: false,
                    ready: snapshot?.supportsUARTWrite == true,
                    partial: snapshot?.supportsUART == true
                ),
                snapshot?.supportsUARTWrite == true
                    ? "Safe UBX poll writes and bounded UART capture are available."
                    : "UART read/write support is not fully exposed.",
                "Add persistent receiver configuration only with explicit confirmation.",
                "terminal"
            ),
            row(
                "nmea-generator",
                "Receiver and serial",
                "NMEA generator",
                "NMEA output engine",
                connected ? "ABI needed" : "Waiting",
                snapshot?.supportsUART == true
                    ? "Serial data path is present; NMEA engine registers are not exposed."
                    : "Needs UART and NMEA register discovery.",
                "Add NMEA enable, rate, sentence mask, and readback selectors.",
                "dot.radiowaves.left.and.right"
            ),
            row(
                "sma-fabric",
                "Signal I/O",
                "SMA route fabric",
                "Connector routing",
                status(live: has("SMA")),
                has("SMA")
                    ? "SMA routing capability is advertised for this board profile."
                    : "Route fabric is not advertised.",
                "Keep fixed-route and variant warnings visible before applying routes.",
                "cable.connector"
            ),
            row(
                "periodic-generators",
                "Signal I/O",
                "Periodic generators",
                "Signal generator panel",
                connected ? "ABI needed" : "Waiting",
                "The Windows generator controls are cataloged, but macOS lacks generator selectors.",
                "Add frequency, duty, phase, start time, repeat, and status readback.",
                "waveform"
            ),
            row(
                "frequency-counters",
                "Signal I/O",
                "Frequency counters",
                "Counter panel",
                snapshot?.supportsFrequency == true ? "Ready" : (connected ? "Unavailable" : "Waiting"),
                snapshot?.supportsFrequency == true
                    ? "ABI v10 provides four counters, integration control, error and overrun reporting."
                    : "Counter presence is not verified for this image; optional addresses are not probed.",
                "Open Generators for validated LitePCIe counters. Classic/ART/ADVA need exact-image contracts.",
                "number"
            ),
            row(
                "irigb-decoder",
                "Signal I/O",
                "IRIG-B decoder",
                "IRIG-B workspace",
                "Roadmap",
                "Needs an FPGA image contract that identifies the IRIG-B core and register span.",
                "Add core detection before any decode or generator controls.",
                "wave.3.forward"
            ),
            row(
                "dcf77-decoder",
                "Signal I/O",
                "DCF77 decoder",
                "DCF77 workspace",
                "Roadmap",
                "Needs an FPGA image contract that identifies the DCF77 core and register span.",
                "Add core detection before any decode or generator controls.",
                "antenna.radiowaves.left.and.right"
            ),
            row(
                "i2c-fabric",
                "Board services",
                "I2C fabric",
                "I2C diagnostics",
                status(live: has("I2C")),
                has("I2C")
                    ? "I2C controller, mux, and known-device probes are available."
                    : "I2C capability is not advertised.",
                "Continue expanding known-device decoders by board profile.",
                "memorychip"
            ),
            row(
                "led-controller",
                "Board services",
                "Status LED controller",
                "LED controls",
                status(live: has("LEDs")),
                has("LEDs")
                    ? "Manual and policy-driven LED readback paths are available."
                    : "LED capability is not advertised.",
                "Add named Windows LED policies after state restore is covered.",
                "lightbulb.led"
            ),
            row(
                "sensor-fabric",
                "Board services",
                "Sensor fabric",
                "Sensors and IMU",
                status(live: has("Sensors")),
                has("Sensors")
                    ? "Environmental telemetry is exposed through DriverKit."
                    : "Sensor capability is not advertised.",
                "Finish IMU fused-motion decoding for BNO055 and BNO08x.",
                "gyroscope"
            ),
            row(
                "spi-flash",
                "Programming and maintenance",
                "SPI flash programmer",
                "FPGA flash update",
                connected ? "Roadmap" : "Waiting",
                "Flash erase, program, and readback are intentionally not exposed yet.",
                "Add image validation first, then guarded erase, program, and verify selectors.",
                "externaldrive.badge.timemachine"
            ),
        ]
    }

    static func reportText(for snapshot: TimeCardDeviceSnapshot?) -> String {
        var lines = ["Time Card macOS FPGA core readiness matrix"]
        for group in groupedRows(for: snapshot) {
            lines.append("")
            lines.append(group.0)
            for row in group.1 {
                lines.append("\(row.core): \(row.state)")
                lines.append("  Windows feature: \(row.windowsFeature)")
                lines.append("  Evidence: \(row.evidence)")
                lines.append("  Next: \(row.nextStep)")
            }
        }
        return lines.joined(separator: "\n")
    }
}

private struct BoardVariantCompatibilityView: View {
    let snapshot: TimeCardDeviceSnapshot?

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 10) {
                StatusPill(
                    snapshot?.boardName ?? "No active board",
                    snapshot == nil ? .secondary : .green
                )
                if let snapshot {
                    StatusPill(snapshot.pciIdentity, .blue)
                    StatusPill(snapshot.layoutName, .purple)
                }
                Spacer()
            }

            LazyVGrid(
                columns: [GridItem(.adaptive(minimum: 255), spacing: 12)],
                spacing: 12
            ) {
                ForEach(TimeCardBoardVariantCompatibility.rows(for: snapshot)) { row in
                    BoardVariantCompatibilityCard(row: row)
                }
            }

            Text(
                "The driver matches each supported PCI identity before MMIO "
                    + "access and selects a profile-specific register map. "
                    + "Only the active card is marked Active; the others remain "
                    + "Supported until hardware is installed for live validation."
            )
            .font(.caption)
            .foregroundStyle(.secondary)
            .textSelection(.enabled)
        }
    }
}

private struct BoardVariantCompatibilityCard: View {
    let row: TimeCardBoardVariantCompatibility

    var body: some View {
        VStack(alignment: .leading, spacing: 9) {
            HStack(alignment: .firstTextBaseline) {
                Text(row.name)
                    .font(.headline)
                Spacer()
                StatusPill(row.state, row.accent)
            }
            InfoRow(label: "PCI ID", value: row.pciIdentity)
            InfoRow(label: "Register map", value: row.registerMap)
            InfoRow(label: "Live coverage", value: row.liveCoverage)
            Text(row.note)
                .font(.caption)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)
        }
        .padding(14)
        .background(
            row.accent.opacity(0.08),
            in: RoundedRectangle(cornerRadius: 14)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(row.accent.opacity(0.18), lineWidth: 1)
        )
    }
}

private struct TimeCardBoardVariantCompatibility: Identifiable {
    let id: UInt32
    let name: String
    let pciIdentity: String
    let registerMap: String
    let liveCoverage: String
    let note: String
    let active: Bool

    var state: String {
        active ? "Active" : "Supported"
    }

    var accent: Color {
        active ? .green : .blue
    }

    static func rows(
        for snapshot: TimeCardDeviceSnapshot?
    ) -> [TimeCardBoardVariantCompatibility] {
        func row(
            id: UInt32,
            name: String,
            pciIdentity: String,
            registerMap: String,
            liveCoverage: String,
            note: String
        ) -> TimeCardBoardVariantCompatibility {
            TimeCardBoardVariantCompatibility(
                id: id,
                name: name,
                pciIdentity: pciIdentity,
                registerMap: registerMap,
                liveCoverage: liveCoverage,
                note: note,
                active: snapshot?.boardProfile == id ||
                    snapshot?.pciIdentity == pciIdentity
            )
        }

        return [
            row(
                id: 1,
                name: "Meta/Facebook Time Card",
                pciIdentity: "1d9b:0400",
                registerMap: "Classic or shifted LitePCIe",
                liveCoverage: "PHC, ToD, SMA, I2C, LEDs, sensors, UART",
                note: "Hardware-validated on the Mac Pro with the classic map."
            ),
            row(
                id: 2,
                name: "Celestica R4006",
                pciIdentity: "18d4:1008",
                registerMap: "Classic or shifted LitePCIe",
                liveCoverage: "PHC, ToD, SMA, I2C, LEDs, sensors, UART",
                note: "Uses the Celestica sensor mux profile and LED path."
            ),
            row(
                id: 3,
                name: "Orolia/Safran ART",
                pciIdentity: "1ad7:a000",
                registerMap: "Fixed ART map",
                liveCoverage: "PHC, ToD, fixed SMA route handling",
                note: "Profile keeps ART fixed-route warnings visible before control."
            ),
            row(
                id: 4,
                name: "ADVA Time Card",
                pciIdentity: "ad5a:0400",
                registerMap: "Fixed common clock and ToD map",
                liveCoverage: "PHC, ToD, SMA, I2C, LEDs, sensors",
                note: "Uses the upstream Linux ADVA PCI identity and fixed map."
            ),
            row(
                id: 5,
                name: "ADVA Time Card X1",
                pciIdentity: "ad5a:0410",
                registerMap: "Fixed common clock and ToD map",
                liveCoverage: "PHC, ToD, SMA, I2C, LEDs, sensors",
                note: "Uses the upstream Linux ADVA X1 PCI identity and fixed map."
            ),
        ]
    }

    static func reportText(for snapshot: TimeCardDeviceSnapshot?) -> String {
        var lines = ["Time Card macOS board variant compatibility"]
        if let snapshot {
            lines.append("Active board: \(snapshot.boardName)")
            lines.append("Active PCI ID: \(snapshot.pciIdentity)")
            lines.append("Active layout: \(snapshot.layoutName)")
        } else {
            lines.append("Active board: unavailable")
        }
        for row in rows(for: snapshot) {
            lines.append("")
            lines.append("\(row.name): \(row.state)")
            lines.append("PCI ID: \(row.pciIdentity)")
            lines.append("Register map: \(row.registerMap)")
            lines.append("Live coverage: \(row.liveCoverage)")
            lines.append("Note: \(row.note)")
        }
        return lines.joined(separator: "\n")
    }
}

private struct HardwareView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                if let snapshot = monitor.snapshot {
                    HStack(alignment: .top, spacing: 18) {
                        ControlCenterPanel(
                            title: "PCI identity",
                            subtitle: "Validated before MMIO access"
                        ) {
                            InfoRow(label: "Board", value: snapshot.boardName)
                            InfoRow(
                                label: "Vendor and device",
                                value: snapshot.pciIdentity
                            )
                            InfoRow(
                                label: "Revision",
                                value: String(
                                    format: "0x%02x",
                                    snapshot.pciRevision & 0xff
                                )
                            )
                            InfoRow(
                                label: "Registry service",
                                value: snapshot.service.displayName
                            )
                        }

                        ControlCenterPanel(
                            title: "PCI resources",
                            subtitle: "Driver-selected safe register window"
                        ) {
                            InfoRow(
                                label: "BAR0 size",
                                value: TimeCardFormatting.bytes(snapshot.barSize)
                            )
                            InfoRow(
                                label: "Register map",
                                value: snapshot.layoutName
                            )
                            InfoRow(
                                label: "MSI-X capacity",
                                value: snapshot.advertisedMSIXVectors == 0
                                    ? "Not advertised"
                                    : "\(snapshot.advertisedMSIXVectors) advertised"
                            )
                            InfoRow(
                                label: "Driver version",
                                value: snapshot.driverVersionText
                            )
                        }
                    }

                    ControlCenterPanel(
                        title: "Driver capabilities",
                        subtitle: "Features exposed by this board profile and ABI"
                    ) {
                        HStack(spacing: 8) {
                            ForEach(snapshot.capabilityNames, id: \.self) { name in
                                Text(name)
                                    .font(.caption.weight(.medium))
                                    .padding(.horizontal, 10)
                                    .padding(.vertical, 6)
                                    .background(
                                        Color.accentColor.opacity(0.14),
                                        in: Capsule()
                                    )
                            }
                        }
                    }

                    ControlCenterPanel(
                        title: "Supported variants",
                        subtitle: "Exact primary PCI identities"
                    ) {
                        InfoRow(label: "Meta/Facebook", value: "1d9b:0400")
                        InfoRow(label: "Celestica R4006", value: "18d4:1008")
                        InfoRow(label: "Orolia/Safran ART", value: "1ad7:a000")
                        InfoRow(label: "ADVA Time Card", value: "ad5a:0400")
                        InfoRow(label: "ADVA Time Card X1", value: "ad5a:0410")
                    }

                    ControlCenterPanel(
                        title: "Future DriverKit workspaces",
                        subtitle: "Requires versioned ABI additions"
                    ) {
                        Text(
                            "UART and GNSS streams, SMA routing, signal "
                                + "generators, frequency counters, timestamp "
                                + "events, sensors, LEDs, I2C, oscillator "
                                + "control, and FPGA update are intentionally "
                                + "not guessed from unvalidated registers."
                        )
                        .foregroundStyle(.secondary)
                    }
                } else {
                    ControlCenterHeader()
                    MonitorUnavailableView()
                }
            }
            .padding(24)
        }
    }
}

private struct DriverView: View {
    @EnvironmentObject private var driverManager: DriverManager
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterPanel(
                    title: "DriverKit extension",
                    subtitle: "org.opentimeserver.timecard.macos.driver"
                ) {
                    InfoRow(
                        label: "Service",
                        value: monitor.serviceDetected
                            ? "Detected" : "Not detected",
                        valueColor: monitor.serviceDetected ? .green : .orange
                    )
                    InfoRow(label: "Request", value: driverManager.status)

                    HStack {
                        Button("Install or Update Driver") {
                            driverManager.activate()
                        }
                        .buttonStyle(.borderedProminent)

                        Button("Remove Driver") {
                            driverManager.deactivate()
                        }
                        .buttonStyle(.bordered)
                    }
                    .disabled(driverManager.requestInProgress)
                }

                if monitor.state == .accessUnavailable {
                    ControlCenterPanel(
                        title: "Telemetry entitlement required",
                        subtitle: "Driver is running; user-client access is blocked"
                    ) {
                        Label(
                            monitor.errorMessage,
                            systemImage: "lock.trianglebadge.exclamationmark"
                        )
                        Text(monitor.recoverySuggestion)
                            .foregroundStyle(.secondary)
                            .textSelection(.enabled)
                    }
                }

                ControlCenterPanel(
                    title: "Approval location",
                    subtitle: approvalSubtitle
                ) {
                    Text(approvalPath)
                    .textSelection(.enabled)
                    Text(
                        "A driver replacement can be approved immediately but "
                            + "may not take ownership of an active PCI device "
                            + "until the next restart."
                    )
                    .foregroundStyle(.secondary)
                }

                ControlCenterPanel(
                    title: "Safety policy",
                    subtitle: "Control Center defaults"
                ) {
                    Label(
                        "Monitoring is read-only and never changes either clock.",
                        systemImage: "checkmark.shield"
                    )
                    Label(
                        "The driver enables PCI memory space and explicitly "
                            + "keeps bus mastering disabled.",
                        systemImage: "memorychip"
                    )
                    Label(
                        "Unknown hardware identities and inconsistent register "
                            + "layouts fail closed before MMIO.",
                        systemImage: "xmark.shield"
                    )
                }
            }
            .padding(24)
        }
    }

    private var approvalSubtitle: String {
        if #available(macOS 26.0, *) {
            return "macOS 26 and later"
        }
        return "macOS 14 and 15"
    }

    private var approvalPath: String {
        if #available(macOS 26.0, *) {
            return "System Settings > General > Login Items & Extensions "
                + "> Driver Extensions"
        }
        return "System Settings > Privacy & Security"
    }
}

private struct TimeCardConfigurationProfilePlan: Identifiable {
    let id: String
    let name: String
    let description: String
    let state: String
    let detail: String
    let actions: [String]
    let blockers: [String]

    static func plans(for snapshot: TimeCardDeviceSnapshot?) -> [Self] {
        let connected = snapshot != nil
        let capabilities = snapshot?.capabilityNames ?? []
        let hasClockSet = capabilities.contains("Clock set")
        let hasToD = capabilities.contains("ToD")
        let hasSMA = capabilities.contains("SMA")
        let hasUARTWrite = snapshot?.supportsUARTWrite == true
        let hasGNSSSummary = snapshot?.gnssTelemetryAvailable == true

        return [
            TimeCardConfigurationProfilePlan(
                id: "gnss-disciplined",
                name: "GNSS disciplined",
                description: "Use the GNSS Time-of-Day engine as the PHC reference.",
                state: connected ? "Planning only" : "Waiting",
                detail: connected
                    ? "Matches the Windows built-in profile, but macOS still needs the trusted time-scale contract and discipline service before one-click apply."
                    : "Connect to the Time Card driver to evaluate this profile.",
                actions: [
                    "Select GNSS as the PHC source.",
                    "Verify ToD UTC, leap-second, NAV-PVT, NAV-TIMELS, and TIM-TP data.",
                    "Start the macOS discipline service once the guarded service path lands.",
                ],
                blockers: missingProfileRequirements([
                    (hasClockSet, "Clock set capability"),
                    (hasToD, "ToD engine capability"),
                    (hasGNSSSummary, "GNSS summary registers"),
                ]) + [
                    "Trusted UTC-to-TAI contract",
                    "macOS PHC discipline daemon",
                ]
            ),
            TimeCardConfigurationProfilePlan(
                id: "external-pps",
                name: "External PPS",
                description: "Discipline the PHC from an external PPS reference.",
                state: hasSMA && hasClockSet ? "ABI needed" :
                    (connected ? "Blocked" : "Waiting"),
                detail: hasSMA && hasClockSet
                    ? "The card exposes SMA and clock-set paths, but PPS phase capture is not in the macOS ABI yet."
                    : "Needs SMA input routing, clock set, and phase measurement support.",
                actions: [
                    "Route a selected SMA connector as PPS input.",
                    "Pair the PPS edge with PHC phase samples.",
                    "Enable a read-back-verified external PPS discipline loop.",
                ],
                blockers: missingProfileRequirements([
                    (hasClockSet, "Clock set capability"),
                    (hasSMA, "SMA routing capability"),
                ]) + [
                    "PPS phase measurement ABI",
                    "external-reference discipline loop",
                ]
            ),
            TimeCardConfigurationProfilePlan(
                id: "ptp-disciplined",
                name: "PTP disciplined",
                description: "Use the PTP clock source exposed by the FPGA.",
                state: connected ? "Planned" : "Waiting",
                detail: "The PTP FPGA source is selectable on supported cores through ABI v10. A macOS PTP discipline service is still required.",
                actions: [
                    "Expose a macOS PHC or service-owned PTP discipline endpoint.",
                    "Read back the FPGA PTP source selection.",
                    "Start clock discipline only after source and time scale are verified.",
                ],
                blockers: connected
                    ? missingProfileRequirements([(snapshot?.supportsClockSource == true, "Clock-source capability")]) + ["macOS PHC service surface"]
                    : ["Time Card driver connection"]
            ),
            TimeCardConfigurationProfilePlan(
                id: "nmea-service",
                name: "NMEA service",
                description: "Enable standard NMEA output at 115200 baud.",
                state: hasUARTWrite ? "UART-ready" :
                    (connected ? "Backend pending" : "Waiting"),
                detail: hasUARTWrite
                    ? "Safe UBX poll writes and bounded reads are available. Persistent receiver configuration is still intentionally gated."
                    : "Needs the UART write ABI before macOS can safely talk to the receiver.",
                actions: [
                    "Use GNSS UART at 115200 baud.",
                    "Poll MON-VER, NAV-PVT, NAV-SAT, NAV-TIMELS, and TIM-TP for validation.",
                    "Apply CFG-VALSET NMEA output presets after persistent write confirmation exists.",
                ],
                blockers: hasUARTWrite
                    ? ["Persistent u-blox configuration confirmation flow"]
                    : ["UART write ABI"]
            ),
            TimeCardConfigurationProfilePlan(
                id: "lab-timing-outputs",
                name: "Lab timing outputs",
                description: "Route GNSS PPS, PHC pulse, atomic clock, and 10 MHz reference to SMA 1-4.",
                state: hasSMA ? "Manual-ready" :
                    (connected ? "Backend pending" : "Waiting"),
                detail: hasSMA
                    ? "Capture a profile above, edit supported routes, and preview before applying. Fixed routes remain blocked; this built-in plan is still a checklist."
                    : "Needs the SMA route ABI for this board profile.",
                actions: [
                    "SMA 1 output: GNSS PPS.",
                    "SMA 2 output: PHC pulse.",
                    "SMA 3 output: atomic clock.",
                    "SMA 4 output: 10 MHz reference.",
                ],
                blockers: hasSMA
                    ? ["Validate requested output routes against the active FPGA image"]
                    : ["SMA routing capability"]
            ),
        ]
    }

    static func reportText(for snapshot: TimeCardDeviceSnapshot?) -> String {
        var lines = ["Time Card macOS configuration profile planner"]
        for plan in plans(for: snapshot) {
            lines.append("")
            lines.append("\(plan.name): \(plan.state)")
            lines.append(plan.description)
            lines.append(plan.detail)
            lines.append("Actions:")
            lines.append(contentsOf: plan.actions.map { "  - \($0)" })
            lines.append("Blockers:")
            lines.append(
                contentsOf: (plan.blockers.isEmpty ? ["None"] : plan.blockers)
                    .map { "  - \($0)" }
            )
        }
        return lines.joined(separator: "\n")
    }

    private static func missingProfileRequirements(
        _ requirements: [(Bool, String)]
    ) -> [String] {
        requirements.compactMap { available, name in
            available ? nil : name
        }
    }
}

struct ControlCenterHeader: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        HStack(alignment: .center, spacing: 18) {
            ZStack {
                RoundedRectangle(cornerRadius: 20)
                    .fill(
                        LinearGradient(
                            colors: [
                                headerColor.opacity(0.25),
                                Color.blue.opacity(0.12),
                            ],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                    )
                    .frame(width: 72, height: 72)
                    .shadow(color: headerColor.opacity(0.2), radius: 18)

                Image(systemName: headerSymbol)
                    .font(.system(size: 34, weight: .semibold))
                    .foregroundStyle(headerColor)
            }

            VStack(alignment: .leading, spacing: 8) {
                Text("OCP Time Card Control Center")
                    .font(.system(.caption, design: .rounded).weight(.bold))
                    .foregroundStyle(.secondary)
                    .textCase(.uppercase)
                    .tracking(1.4)

                Text(monitor.snapshot?.boardName ?? "Waiting for Time Card")
                    .font(.system(size: 30, weight: .semibold, design: .rounded))
                    .lineLimit(1)
                    .minimumScaleFactor(0.75)

                Text(headerDetail)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }

            Spacer(minLength: 18)

            VStack(alignment: .trailing, spacing: 8) {
                Text(headerLabel)
                    .font(.caption.weight(.bold))
                    .tracking(0.8)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 7)
                    .foregroundStyle(headerColor)
                    .background(headerColor.opacity(0.14), in: Capsule())
                    .overlay(
                        Capsule().stroke(headerColor.opacity(0.35), lineWidth: 1)
                    )

                if let lastUpdated = monitor.lastUpdated {
                    Text(TimeCardFormatting.date(lastUpdated))
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(22)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
        .overlay(
            RoundedRectangle(cornerRadius: 24)
                .stroke(.white.opacity(0.16), lineWidth: 1)
        )
    }

    private var headerLabel: String {
        switch monitor.state {
        case .discovering: "DISCOVERING"
        case .noService: "WAITING FOR DRIVER"
        case .connected: "LIVE"
        case .accessUnavailable: "ACCESS REQUIRED"
        case .failed: "TELEMETRY ERROR"
        }
    }

    private var headerDetail: String {
        if let snapshot = monitor.snapshot {
            return "\(snapshot.pciIdentity)  |  \(snapshot.layoutName) map"
        }
        return monitor.errorMessage.isEmpty
            ? "Discovering DriverKit services"
            : monitor.errorMessage
    }

    private var headerSymbol: String {
        switch monitor.state {
        case .discovering: "magnifyingglass"
        case .noService: "clock.badge.questionmark"
        case .connected: "checkmark.circle.fill"
        case .accessUnavailable: "lock.fill"
        case .failed: "exclamationmark.triangle.fill"
        }
    }

    private var headerColor: Color {
        switch monitor.state {
        case .connected: .green
        case .discovering: .blue
        case .noService, .accessUnavailable: .orange
        case .failed: .red
        }
    }
}

private struct MonitorUnavailableView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @EnvironmentObject private var driverManager: DriverManager

    var body: some View {
        ControlCenterPanel(
            title: monitor.serviceDetected
                ? "Driver telemetry unavailable" : "Time Card not available",
            subtitle: monitor.serviceDetected
                ? "The DriverKit service is active"
                : "Install the extension and verify PCI enumeration"
        ) {
            Label(
                monitor.errorMessage.isEmpty
                    ? "Waiting for a Time Card service."
                    : monitor.errorMessage,
                systemImage: monitor.serviceDetected
                    ? "lock.trianglebadge.exclamationmark"
                    : "puzzlepiece.extension"
            )

            if !monitor.recoverySuggestion.isEmpty {
                Text(monitor.recoverySuggestion)
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }

            HStack {
                Button("Install or Update Driver") {
                    driverManager.activate()
                }
                .buttonStyle(.borderedProminent)
                .disabled(driverManager.requestInProgress)

                Button("Refresh") {
                    monitor.refresh()
                }
            }
        }
    }
}

private struct SamplingWindowChart: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ControlCenterPanel(
            title: "Sampling-window history",
            subtitle: "Last \(monitor.samplingWindowHistory.count) bracketed reads"
        ) {
            if monitor.samplingWindowHistory.isEmpty {
                ContentUnavailableView(
                    "No samples yet",
                    systemImage: "chart.xyaxis.line",
                    description: Text("Waiting for DriverKit telemetry.")
                )
                .frame(height: 170)
            } else {
                Chart(monitor.samplingWindowHistory) { point in
                    AreaMark(
                        x: .value("Time", point.timestamp),
                        y: .value("Nanoseconds", point.nanoseconds)
                    )
                    .foregroundStyle(
                        .linearGradient(
                            colors: [Color.accentColor.opacity(0.35), .clear],
                            startPoint: .top,
                            endPoint: .bottom
                        )
                    )

                    LineMark(
                        x: .value("Time", point.timestamp),
                        y: .value("Nanoseconds", point.nanoseconds)
                    )
                    .foregroundStyle(Color.accentColor)
                    .interpolationMethod(.catmullRom)
                }
                .chartXAxis(.hidden)
                .chartYAxisLabel("ns")
                .frame(height: 190)
            }
        }
    }
}

private struct FeatureRow: View {
    let name: String
    let state: String
    let note: String

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            statusIcon
                .frame(width: 24)

            VStack(alignment: .leading, spacing: 3) {
                HStack {
                    Text(name)
                        .font(.body.weight(.medium))
                    Spacer()
                    Text(state)
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(statusColor)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(statusColor.opacity(0.12), in: Capsule())
                }
                Text(note)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private var statusIcon: some View {
        Image(systemName: statusSymbol)
            .foregroundStyle(statusColor)
    }

    private var statusSymbol: String {
        switch state.lowercased() {
        case "live", "pass":
            "checkmark.circle.fill"
        case "fail":
            "xmark.octagon.fill"
        case "warning":
            "exclamationmark.triangle.fill"
        case "waiting", "check", "partial":
            "clock.badge.questionmark"
        case "gated", "unavailable":
            "lock.circle"
        default:
            "wrench.and.screwdriver"
        }
    }

    private var statusColor: Color {
        switch state.lowercased() {
        case "live", "pass":
            .green
        case "fail":
            .red
        case "warning", "waiting", "check", "partial":
            .orange
        case "gated", "unavailable":
            .secondary
        default:
            .blue
        }
    }
}

private struct StatusPill: View {
    let text: String
    let color: Color

    init(_ text: String, _ color: Color) {
        self.text = text
        self.color = color
    }

    var body: some View {
        Text(text)
            .font(.caption.weight(.bold))
            .tracking(0.5)
            .padding(.horizontal, 9)
            .padding(.vertical, 5)
            .foregroundStyle(color)
            .background(color.opacity(0.12), in: Capsule())
            .overlay(Capsule().stroke(color.opacity(0.25), lineWidth: 1))
    }
}

private struct SubsystemCard: View {
    let title: String
    let state: String
    let detail: String

    init(_ title: String, _ state: String, _ detail: String) {
        self.title = title
        self.state = state
        self.detail = detail
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Image(systemName: state == "Live" ? "checkmark.circle.fill" : "lock.circle")
                    .foregroundStyle(state == "Live" ? .green : .secondary)
                Text(title)
                    .font(.headline)
                Spacer()
            }
            Text(state)
                .font(.caption.weight(.semibold))
                .foregroundStyle(state == "Live" ? .green : .secondary)
            Text(detail)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(14)
        .background(Color.secondary.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
    }
}

private struct WorkspaceTile: View {
    let title: String
    let state: String
    let detail: String
    let systemImage: String
    let accent: Color

    init(
        _ title: String,
        _ state: String,
        _ detail: String,
        _ systemImage: String,
        _ accent: Color
    ) {
        self.title = title
        self.state = state
        self.detail = detail
        self.systemImage = systemImage
        self.accent = accent
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Image(systemName: systemImage)
                    .font(.title3.weight(.semibold))
                    .foregroundStyle(accent)
                    .frame(width: 36, height: 36)
                    .background(accent.opacity(0.14), in: RoundedRectangle(cornerRadius: 10))

                Spacer()

                Text(state)
                    .font(.caption2.weight(.bold))
                    .tracking(0.6)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .foregroundStyle(accent)
                    .background(accent.opacity(0.12), in: Capsule())
            }

            Text(title)
                .font(.headline)
            Text(detail)
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity, minHeight: 128, alignment: .topLeading)
        .padding(16)
        .background(
            LinearGradient(
                colors: [accent.opacity(0.10), Color.secondary.opacity(0.06)],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            ),
            in: RoundedRectangle(cornerRadius: 16)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 16)
                .stroke(accent.opacity(0.18), lineWidth: 1)
        )
    }
}

private struct ControlCenterBackground: View {
    var body: some View {
        ZStack {
            LinearGradient(
                colors: [
                    Color(nsColor: .windowBackgroundColor),
                    Color.blue.opacity(0.06),
                    Color.cyan.opacity(0.04),
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            RadialGradient(
                colors: [Color.cyan.opacity(0.16), .clear],
                center: .topTrailing,
                startRadius: 40,
                endRadius: 520
            )
            RadialGradient(
                colors: [Color.purple.opacity(0.12), .clear],
                center: .bottomLeading,
                startRadius: 60,
                endRadius: 500
            )
        }
        .ignoresSafeArea()
    }
}

private struct MetricCard: View {
    let title: String
    let value: String
    let detail: String
    let systemImage: String
    var accent: Color = .accentColor

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Label(title, systemImage: systemImage)
                    .font(.caption.weight(.semibold))
                    .foregroundStyle(.secondary)
                Spacer()
                Circle()
                    .fill(accent)
                    .frame(width: 8, height: 8)
                    .shadow(color: accent.opacity(0.55), radius: 6)
            }
            Text(value)
                .font(.system(.title3, design: .monospaced, weight: .semibold))
                .lineLimit(1)
                .minimumScaleFactor(0.65)
                .textSelection(.enabled)
            Text(detail)
                .font(.caption)
                .foregroundStyle(.tertiary)
        }
        .frame(maxWidth: .infinity, minHeight: 94, alignment: .leading)
        .padding(16)
        .background(
            LinearGradient(
                colors: [accent.opacity(0.10), Color.secondary.opacity(0.06)],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            ),
            in: RoundedRectangle(cornerRadius: 16)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 16)
                .stroke(accent.opacity(0.18), lineWidth: 1)
        )
    }
}

struct ControlCenterPanel<Content: View>: View {
    let title: String
    let subtitle: String
    private let content: Content

    init(
        title: String,
        subtitle: String = "",
        @ViewBuilder content: () -> Content
    ) {
        self.title = title
        self.subtitle = subtitle
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.headline)
                if !subtitle.isEmpty {
                    Text(subtitle)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Divider()
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(18)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 18))
        .overlay(
            RoundedRectangle(cornerRadius: 18)
                .stroke(.white.opacity(0.14), lineWidth: 1)
        )
        .shadow(color: .black.opacity(0.05), radius: 12, y: 5)
    }
}

private struct InfoRow: View {
    let label: String
    let value: String
    var valueColor: Color = .primary

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Text(label)
                .foregroundStyle(.secondary)
            Spacer(minLength: 24)
            Text(value)
                .font(.system(.body, design: .monospaced))
                .foregroundStyle(valueColor)
                .multilineTextAlignment(.trailing)
                .textSelection(.enabled)
        }
    }
}

private enum TimeCardFormatting {
    static func rawTimestamp(_ snapshot: TimeCardDeviceSnapshot) -> String {
        String(
            format: "%llu.%09u",
            snapshot.cardSeconds,
            snapshot.cardNanoseconds
        )
    }

    static func systemTimestamp(_ snapshot: TimeCardDeviceSnapshot) -> String {
        let seconds = Double(snapshot.systemMidpointNanoseconds) / 1_000_000_000
        return date(Date(timeIntervalSince1970: seconds))
    }

    static func date(_ value: Date?) -> String {
        guard let value else { return "Unavailable" }
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [
            .withInternetDateTime,
            .withFractionalSeconds,
        ]
        formatter.timeZone = TimeZone(secondsFromGMT: 0)
        return formatter.string(from: value)
    }

    static func duration(_ nanoseconds: UInt64) -> String {
        if nanoseconds < 1_000 {
            return "\(nanoseconds) ns"
        }
        if nanoseconds < 1_000_000 {
            return String(format: "%.3f us", Double(nanoseconds) / 1_000)
        }
        if nanoseconds < 1_000_000_000 {
            return String(format: "%.3f ms", Double(nanoseconds) / 1_000_000)
        }
        return String(format: "%.6f s", Double(nanoseconds) / 1_000_000_000)
    }

    static func syncStatus(_ snapshot: TimeCardDeviceSnapshot) -> String {
        guard let inSync = snapshot.clockInSync else { return "Unavailable" }
        return inSync ? "In sync" : "Not in sync"
    }

    static func clockSource(_ snapshot: TimeCardDeviceSnapshot) -> String {
        guard let source = snapshot.configuredClockSource else {
            return "Unavailable"
        }
        return String(format: "0x%02x", source)
    }

    static func validHex(_ value: UInt32, valid: Bool) -> String {
        valid ? String(format: "0x%08x", value) : "Unavailable"
    }

    static func byteHex(_ value: UInt32) -> String {
        String(format: "0x%02x", value & 0xff)
    }

    static func hex(_ value: UInt64) -> String {
        String(format: "0x%llx", value)
    }

    static func bytes(_ value: UInt64) -> String {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .memory
        let byteCount = value > UInt64(Int64.max) ? Int64.max : Int64(value)
        return "\(formatter.string(fromByteCount: byteCount)) (\(hex(value)))"
    }

    static func temperature(_ value: Double?) -> String {
        guard let value else { return "Unavailable" }
        return String(format: "%.1f °C", value)
    }

    static func humidity(_ value: Double?) -> String {
        guard let value else { return "Unavailable" }
        return String(format: "%.1f %%RH", value)
    }

    static func hPa(_ pressurePascals: Double) -> String {
        String(format: "%.3f hPa", pressurePascals / 100.0)
    }

    static func raw24(_ value: UInt32) -> String {
        String(format: "0x%06x", value & 0x00ff_ffff)
    }
}
