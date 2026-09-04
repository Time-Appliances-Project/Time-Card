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
                    CapabilityWorkspaceView(workspace: .gnss)
                case .uart:
                    CapabilityWorkspaceView(workspace: .uart)
                case .sma:
                    SMARoutingView()
                case .timing:
                    CapabilityWorkspaceView(workspace: .timing)
                case .fpga:
                    CapabilityWorkspaceView(workspace: .fpga)
                case .sensors:
                    CapabilityWorkspaceView(workspace: .sensors)
                case .i2c:
                    CapabilityWorkspaceView(workspace: .i2c)
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
                                "Gated",
                                "Route fabric ABI next",
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
                                "Gated",
                                "I2C and IMU backend next",
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
                ("LM75B board temperatures", "Available through DriverKit ABI v6 and CLI"),
                ("SHT3x humidity and temperature", "Available through DriverKit ABI v6 and CLI"),
                ("ICP-10100 pressure sensor", "Available with raw pressure and temperature"),
                ("BME/BMP and INA rails", "Transport ready, decoder pending"),
                ("BNO055/BNO08x IMU", "Needs sensor transport and decoding"),
                ("Vibration charts", "Needs live IMU samples"),
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
            "Backend gated"
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
        case .connected: .orange
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
        return "Backend pending"
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
                        state: "Backend pending",
                        note: "Requires GNSS, sensor, and IMU ABI expansion."
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
        return [
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
        ].joined(separator: "\n")
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
                        SubsystemCard("SMA", "Gated", "Needs route ABI")
                        SubsystemCard("I2C", "Gated", "Needs transaction ABI")
                        SubsystemCard("Sensors", "Gated", "Needs sensor ABI")
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
        case "waiting", "check":
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
        case "waiting", "check":
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

    static func hex(_ value: UInt64) -> String {
        String(format: "0x%llx", value)
    }

    static func bytes(_ value: UInt64) -> String {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .memory
        let byteCount = value > UInt64(Int64.max) ? Int64.max : Int64(value)
        return "\(formatter.string(fromByteCount: byteCount)) (\(hex(value)))"
    }
}
