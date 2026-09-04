/* SPDX-License-Identifier: BSD-3-Clause */

import Charts
import AppKit
import SwiftUI

private enum ControlCenterPage: String, CaseIterable, Identifiable {
    case overview = "Overview"
    case clock = "Precision Clock"
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

    var systemImage: String {
        switch self {
        case .overview: "gauge.with.dots.needle.67percent"
        case .clock: "clock.badge.checkmark"
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
    @State private var selectedPage: ControlCenterPage = .overview

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
                case .gnss:
                    GNSSWorkspaceView()
                case .uart:
                    CapabilityWorkspaceView(workspace: .uart)
                case .sma:
                    SMARoutingView()
                case .timing:
                    CapabilityWorkspaceView(workspace: .timing)
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
                }
            }
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
                                state: "Backend pending",
                                note: "Needs a guarded DriverKit UART stream ABI before UBX requests can be sent."
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

private struct I2CAndLEDView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var i2cAddressText = "0x70"
    @State private var i2cSubaddressLength: UInt32 = 0
    @State private var i2cSubaddressText = ""
    @State private var i2cReadLengthText = "1"
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
                                note: "Current mux channel mask is read without changing the active branch."
                            )
                            FeatureRow(
                                name: "RGB subsystem LEDs",
                                state: presentLEDCount > 0 ? "Live" : "Unavailable",
                                note: "GNSS and SMA LED color/current readback is live. App-side writes stay CLI-only for now."
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
                ("u-blox identity and firmware", "Needs native serial/UBX transport"),
                ("Sky map and constellation counts", "Needs GNSS stream decoder data"),
                ("Survey-in and fixed-position controls", "Needs guarded receiver configuration ABI"),
            ]
        case .uart:
            return [
                ("Hardware UART ports 0-3", "Needs DriverKit stream read/write ABI"),
                ("Generic macOS serial ports", "Planned as app-only IOKit serial enumeration"),
                ("NMEA generator control", "Needs NMEA register get/set ABI"),
                ("Capture and export", "UI scaffold ready, backend pending stream data"),
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
                ("Frequency counters", "Needs counter query/control ABI"),
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
                ("BNO055/BNO08x IMU", "Probe path live, fusion decoder pending"),
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
                    SensorIMUView(telemetry: telemetry)
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
            subtitle: "Live DriverKit ABI v7 environmental telemetry"
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
                            + "ICP-10100 factory OTP calibration, and tracks "
                            + "BNO08x/BNO055 IMU bring-up without treating "
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

private struct SensorIMUView: View {
    let telemetry: TimeCardSensorSnapshot

    var body: some View {
        ControlCenterPanel(
            title: "IMU workspace",
            subtitle: "BNO055/BNO08x detection and fused-motion gap tracking"
        ) {
            VStack(alignment: .leading, spacing: 10) {
                if imuReadings.isEmpty {
                    FeatureRow(
                        name: "BNO055/BNO08x probe",
                        state: "Waiting",
                        note: "The active profile did not return an IMU probe row."
                    )
                } else {
                    ForEach(imuReadings) { reading in
                        FeatureRow(
                            name: reading.kind.label,
                            state: imuState(reading),
                            note: imuDetail(reading)
                        )
                    }
                }

                Divider()

                Text(
                    "Full Windows parity still needs the SH-2/BNO055 stream "
                        + "decoder, quaternion orientation, calibration levels, "
                        + "and the 3D cube. This page now exposes the live "
                        + "presence path so that the next decoder slice has a "
                        + "real macOS telemetry row to attach to."
                )
                .font(.caption)
                .foregroundStyle(.secondary)
            }
        }
    }

    private var imuReadings: [TimeCardSensorReading] {
        telemetry.readings.filter {
            $0.isIMU || $0.kind == .bno08x || $0.kind == .bno055
        }
    }

    private func imuState(_ reading: TimeCardSensorReading) -> String {
        if reading.isValid { return "Live" }
        if reading.isPresent { return "Partial" }
        return "Unavailable"
    }

    private func imuDetail(_ reading: TimeCardSensorReading) -> String {
        if reading.isValid {
            return "\(SensorUIFormatting.route(reading)) responded with a valid identity."
        }
        if reading.isPresent {
            if reading.kind == .bno08x {
                let channel = (reading.raw2 >> 8) & 0xff
                return "\(SensorUIFormatting.route(reading)) SHTP header " +
                    "length \(reading.raw0), channel \(channel). " +
                    "Decoder bring-up is next."
            }
            return "\(SensorUIFormatting.route(reading)) ACKed. Decoder bring-up is next."
        }
        return "\(SensorUIFormatting.route(reading)) did not ACK on this sample."
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

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()
                SamplingWindowChart()

                ControlCenterPanel(
                    title: "Telemetry Studio",
                    subtitle: "Windows charts mapped to current macOS data"
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
                        name: "GNSS, temperature, vibration charts",
                        state: monitor.sensorHistory.isEmpty ? "Partial" : "Live",
                        note: "Temperature chart is live; GNSS and vibration need stream decoders."
                    )
                    FeatureRow(
                        name: "CSV and JSON export",
                        state: "Planned",
                        note: "Diagnostics copy is available today."
                    )
                }
            }
            .padding(24)
        }
    }
}

private struct OperationsView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var copiedDiagnostics = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

                ControlCenterPanel(
                    title: "Guided self-test",
                    subtitle: "Read-only macOS coverage"
                ) {
                    FeatureRow(
                        name: "Driver service",
                        state: monitor.serviceDetected ? "Pass" : "Waiting",
                        note: "Checks DriverKit service discovery."
                    )
                    FeatureRow(
                        name: "User-client entitlement",
                        state: monitor.state == .connected ? "Pass" : "Check",
                        note: "Opening the user client proves Apple entitlement/profile wiring."
                    )
                    FeatureRow(
                        name: "Clock read and cross-timestamp",
                        state: monitor.snapshot == nil ? "Waiting" : "Pass",
                        note: "Uses the same ABI path as the CLI status and get commands."
                    )
                    FeatureRow(
                        name: "Advanced workspaces",
                        state: "Gated",
                        note: "Requires the next macOS DriverKit ABI milestone."
                    )
                }

                ControlCenterPanel(
                    title: "Diagnostics",
                    subtitle: "Copyable support report"
                ) {
                    Button("Copy Diagnostics") {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(
                            diagnosticsText,
                            forType: .string
                        )
                        copiedDiagnostics = true
                    }
                    .buttonStyle(.borderedProminent)

                    Text(copiedDiagnostics ? "Diagnostics copied." : diagnosticsText)
                        .font(.system(.caption, design: .monospaced))
                        .foregroundStyle(.secondary)
                        .textSelection(.enabled)
                }
            }
            .padding(24)
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
        return lines.joined(separator: "\n")
    }
}

private struct SubsystemMapView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()

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
                        SubsystemCard("GNSS", "Gated", "Needs UART/UBX ABI")
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

private struct ControlCenterHeader: View {
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
        case "waiting", "check", "partial":
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

private struct ControlCenterPanel<Content: View>: View {
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
