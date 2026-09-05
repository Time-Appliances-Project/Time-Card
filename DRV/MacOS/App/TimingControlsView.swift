/* SPDX-License-Identifier: BSD-3-Clause */
import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct ClockSourceControlPanel: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var selectedSource: UInt32 = 3
    @State private var pending: SourceChange?
    private struct SourceChange { let source: UInt32; let expected: UInt32; let serviceID: UInt64 }

    var body: some View {
        ControlCenterPanel(title: "Clock-source control", subtitle: "Configured selection and active FPGA input") {
            if monitor.state == .connected, let clock = monitor.clockControl {
                HStack(spacing: 24) {
                    sourceMetric("Configured", source: clock.source, symbol: "slider.horizontal.3")
                    sourceMetric("Active input", source: clock.activeSource, symbol: "arrow.triangle.branch")
                    Spacer()
                    Label(clock.status & 1 != 0 ? "In sync" : "Not in sync",
                          systemImage: clock.status & 1 != 0 ? "checkmark.circle.fill" : "exclamationmark.circle")
                        .foregroundStyle(clock.status & 1 != 0 ? .green : .orange)
                }
                Divider()
                HStack {
                    Picker("Synchronization source", selection: $selectedSource) {
                        ForEach(clock.availableSources, id: \.self) { source in
                            Text(TimeCardClockControlState.name(source)).tag(source)
                        }
                        if !clock.supports(clock.source) {
                            Text(TimeCardClockControlState.name(clock.source) + " (read-only)").tag(clock.source)
                        }
                    }.frame(maxWidth: 360)
                    Button("Apply Source…") {
                        if let serviceID = monitor.selectedServiceID {
                            pending = SourceChange(source: selectedSource, expected: clock.source, serviceID: serviceID)
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(!clock.supports(selectedSource) || selectedSource == clock.source ||
                              monitor.commandInProgress || monitor.timingRefreshInProgress)
                    Spacer()
                    Button { monitor.refreshTiming() } label: { Image(systemName: "arrow.clockwise") }
                        .help("Refresh timing registers")
                        .disabled(monitor.commandInProgress || monitor.timingRefreshInProgress)
                }
                Text("Only sources supported by the clock-core contract are selectable. A selectable input is not a guarantee of a connected or locked reference. NTP, SyncE, and Dynamic require additional image validation.")
                    .font(.caption).foregroundStyle(.secondary)
            } else {
                Label(unavailableReason, systemImage: "lock.shield")
                    .foregroundStyle(.secondary)
                if !monitor.clockControlError.isEmpty {
                    Text(monitor.clockControlError).font(.caption).foregroundStyle(.orange)
                }
            }
            if !monitor.timingMessage.isEmpty {
                Text(monitor.timingMessage).font(.caption).textSelection(.enabled)
            }
        }
        .onChange(of: monitor.clockControl?.source, initial: true) { _, source in
            if let source { selectedSource = source }
        }
        .confirmationDialog("Change the Time Card synchronization source?", isPresented: Binding(
            get: { pending != nil }, set: { if !$0 { pending = nil } }
        ), titleVisibility: .visible) {
            if let change = pending {
                Button("Use " + TimeCardClockControlState.name(change.source)) {
                    monitor.setClockSource(change.source, expectedSource: change.expected, serviceID: change.serviceID)
                    pending = nil
                }
            }
            Button("Cancel", role: .cancel) { pending = nil }
        } message: {
            Text("Changing source can interrupt synchronization and affect downstream timing users. This does not set macOS time. A concurrent source change causes the request to be rejected.")
        }
        .task {
            while !Task.isCancelled {
                monitor.refreshTiming()
                do { try await Task.sleep(for: .seconds(2)) } catch { break }
            }
        }
    }

    private var unavailableReason: String {
        guard let snapshot = monitor.snapshot else { return "Connect a Time Card to inspect and configure the clock source." }
        if snapshot.abiVersion < 10 { return "Activate the updated driver (ABI 10) to enable clock-source control. If it is already activated, restart the Mac to replace the driver still running on the card." }
        return snapshot.supportsClockSource ? "Reading clock-source state…" : "Clock-source writes are not enabled for this core version."
    }
    private func sourceMetric(_ title: String, source: UInt32, symbol: String) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Label(title, systemImage: symbol).font(.caption).foregroundStyle(.secondary)
            Text(TimeCardClockControlState.name(source)).font(.title2.weight(.semibold))
        }
    }
}

struct TimingControlsView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var exportMessage = ""
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ClockSourceControlPanel()
                ControlCenterPanel(title: "Frequency counters", subtitle: "Four FPGA measurement channels, 1 to 255 second integration") {
                    HStack {
                        Label("Readback-verified controls", systemImage: "checkmark.shield")
                            .foregroundStyle(.secondary)
                        Spacer()
                        Button("Export Timing Snapshot…") { exportSnapshot() }
                            .disabled(monitor.snapshot == nil || monitor.state != .connected)
                    }
                    if monitor.state == .connected, !monitor.frequencyStates.isEmpty {
                        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 16) {
                            ForEach(monitor.frequencyStates) { counter in
                                FrequencyCounterCard(counter: counter)
                            }
                        }
                    } else {
                        VStack(alignment: .leading, spacing: 10) {
                            Label("Counter hardware not verified", systemImage: "waveform.badge.exclamationmark")
                                .font(.headline)
                            Text(counterAvailability)
                                .foregroundStyle(.secondary)
                            if !monitor.frequencyError.isEmpty {
                                Text(monitor.frequencyError).font(.caption).foregroundStyle(.orange)
                            }
                        }.frame(maxWidth: .infinity, alignment: .leading).padding(16)
                            .background(.quaternary, in: RoundedRectangle(cornerRadius: 12))
                    }
                    Text("Counter controls do not reroute SMA connectors. Select the intended counter input in SMA Routing. Disabled, waiting, overrun, and error states never display a stale frequency as a valid measurement.")
                        .font(.caption).foregroundStyle(.secondary)
                    if !exportMessage.isEmpty { Text(exportMessage).font(.caption).textSelection(.enabled) }
                }
                ControlCenterPanel(title: "Signal generators and event timestamps", subtitle: "Additional Windows parity work") {
                    Label("Not yet enabled", systemImage: "lock.shield")
                    Text("Pulse generation, programmable signal outputs, and timestamp interrupt capture still require their own validated register contracts and driver APIs. Frequency measurement does not enable those outputs.")
                        .foregroundStyle(.secondary)
                }
            }.padding(24)
        }
    }
    private var counterAvailability: String {
        guard let snapshot = monitor.snapshot else { return "Connect a supported card to read frequency measurements." }
        if snapshot.abiVersion < 10 { return "Frequency controls require DriverKit ABI 10. Activate the bundled update, then restart the Mac if the previous driver is still serving the card." }
        if snapshot.supportsFrequency { return "Reading counters from the validated LitePCIe register layout…" }
        return "This card's image does not have a verified counter-presence contract. Original classic FPGA images can omit these cores entirely. Classic, ART, and ADVA counter addresses are not probed. The four-counter backend is enabled only for the supported Meta/Celestica revision-02 LitePCIe layout."
    }
    private func exportSnapshot() {
        guard let snapshot = monitor.snapshot else { return }
        struct Export: Encodable {
            let schemaVersion: Int
            let capturedAt: Date
            let serviceID: String
            let pciIdentity: String
            let driverABI: UInt32
            let registerLayout: String
            let clock: TimeCardClockControlState?
            let counters: [TimeCardFrequencyState]
            let counterAvailability: String
            let clockError: String
            let counterError: String
        }
        let record = Export(schemaVersion: 1, capturedAt: Date(), serviceID: String(snapshot.service.id),
                            pciIdentity: snapshot.pciIdentity, driverABI: snapshot.abiVersion,
                            registerLayout: snapshot.layoutName, clock: monitor.clockControl,
                            counters: monitor.frequencyStates, counterAvailability: counterAvailability,
                            clockError: monitor.clockControlError, counterError: monitor.frequencyError)
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = "timecard-timing.json"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            encoder.dateEncodingStrategy = .iso8601
            try encoder.encode(record).write(to: url, options: .atomic)
            exportMessage = "Saved " + url.lastPathComponent
        } catch { exportMessage = "Export failed: " + error.localizedDescription }
    }
}

private struct FrequencyCounterCard: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    let counter: TimeCardFrequencyState
    @State private var interval = "1"
    private var seconds: UInt32? { UInt32(interval.trimmingCharacters(in: .whitespaces)) }
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Label("Counter \(counter.counter)", systemImage: "waveform.path")
                    .font(.headline)
                Spacer()
                Circle().fill(counter.measurementHz == nil ? Color.secondary : .green).frame(width: 8, height: 8)
            }
            if let hertz = counter.measurementHz {
                Text("\(hertz.formatted()) Hz").font(.system(.title, design: .monospaced)).contentTransition(.numericText())
            } else { Text("No valid reading").font(.title3).foregroundStyle(.secondary) }
            Text(counter.stateLabel).font(.caption)
                .foregroundStyle(counter.hasError || counter.hasOverrun ? .orange : .secondary)
            HStack {
                TextField("Seconds", text: $interval).frame(width: 60).textFieldStyle(.roundedBorder)
                    .accessibilityLabel("Counter \(counter.counter) integration seconds")
                Text("s").foregroundStyle(.secondary)
                Button("Apply") {
                    if let seconds, let id = monitor.selectedServiceID {
                        monitor.setFrequency(counter: counter.counter, seconds: seconds,
                                             expectedControl: counter.control, serviceID: id)
                    }
                }.disabled(seconds == nil || (seconds ?? 256) > 255 || monitor.commandInProgress || monitor.timingRefreshInProgress)
                Spacer()
            }
            Text("0 disables measurement. Current interval: \(counter.integrationSeconds) s.")
                .font(.caption).foregroundStyle(.secondary)
            Text(String(format: "Control 0x%08x · Status 0x%08x", counter.control, counter.status))
                .font(.caption2.monospaced()).foregroundStyle(.secondary).textSelection(.enabled)
        }.padding(16).background(.quaternary, in: RoundedRectangle(cornerRadius: 12))
            .onAppear { interval = String(counter.integrationSeconds) }
            .onChange(of: counter.integrationSeconds) { _, value in interval = String(value) }
    }
}
