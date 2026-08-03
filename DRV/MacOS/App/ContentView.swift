/* SPDX-License-Identifier: BSD-3-Clause */

import Charts
import SwiftUI

private enum ControlCenterPage: String, CaseIterable, Identifiable {
    case overview = "Overview"
    case timing = "Precision Clock"
    case hardware = "Hardware"
    case driver = "Driver"

    var id: Self { self }

    var systemImage: String {
        switch self {
        case .overview: "gauge.with.dots.needle.67percent"
        case .timing: "clock.badge.checkmark"
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
            .navigationTitle("Time Card")
            .navigationSplitViewColumnWidth(min: 190, ideal: 215)
        } detail: {
            Group {
                switch selectedPage {
                case .overview:
                    OverviewView()
                case .timing:
                    PrecisionClockView()
                case .hardware:
                    HardwareView()
                case .driver:
                    DriverView()
                }
            }
            .navigationTitle(selectedPage.rawValue)
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
                            systemImage: "clock"
                        )
                        MetricCard(
                            title: "macOS",
                            value: TimeCardFormatting.systemTimestamp(snapshot),
                            detail: "Realtime sampling midpoint",
                            systemImage: "desktopcomputer"
                        )
                        MetricCard(
                            title: "Sampling window",
                            value: TimeCardFormatting.duration(
                                snapshot.sampleWindowNanoseconds
                            ),
                            detail: "Bracketed cross timestamp",
                            systemImage: "arrow.left.and.right"
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
                        title: "Read-only monitoring",
                        subtitle: "Safe first macOS Control Center milestone"
                    ) {
                        Label(
                            "The dashboard reads card identity, clock status, "
                                + "Time of Day status, and bracketed timestamps. "
                                + "It never changes the Time Card or macOS clock.",
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
                } else {
                    ControlCenterHeader()
                    MonitorUnavailableView()
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
        HStack(spacing: 14) {
            Image(systemName: headerSymbol)
                .font(.system(size: 30, weight: .semibold))
                .foregroundStyle(headerColor)
                .frame(width: 48, height: 48)
                .background(headerColor.opacity(0.12), in: RoundedRectangle(cornerRadius: 12))

            VStack(alignment: .leading, spacing: 3) {
                Text(monitor.snapshot?.boardName ?? "OCP Time Card Control Center")
                    .font(.title2.weight(.semibold))
                Text(headerDetail)
                    .foregroundStyle(.secondary)
            }

            Spacer()

            Text(headerLabel)
                .font(.caption.weight(.semibold))
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .foregroundStyle(headerColor)
                .background(headerColor.opacity(0.12), in: Capsule())
        }
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

private struct MetricCard: View {
    let title: String
    let value: String
    let detail: String
    let systemImage: String

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Label(title, systemImage: systemImage)
                .font(.caption.weight(.semibold))
                .foregroundStyle(.secondary)
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
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
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
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
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
