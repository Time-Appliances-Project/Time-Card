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
                PPSEnginesPanel()
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
                    Text("Programmable signal generators and timestamp interrupt capture still require their own validated register contracts and driver APIs. The PPS engine controls above do not enable those independent outputs.")
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
            let ppsEngines: [TimeCardPPSState]
            let ppsErrors: [String]
            let counterAvailability: String
            let clockError: String
            let counterError: String
        }
        let record = Export(schemaVersion: 1, capturedAt: Date(), serviceID: String(snapshot.service.id),
                            pciIdentity: snapshot.pciIdentity, driverABI: snapshot.abiVersion,
                            registerLayout: snapshot.layoutName, clock: monitor.clockControl,
                            counters: monitor.frequencyStates, ppsEngines: monitor.ppsStates, ppsErrors: monitor.ppsErrors,
                            counterAvailability: counterAvailability,
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

struct PPSEnginesPanel: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var editing: TimeCardPPSState?
    @State private var editingService: UInt64 = 0
    var body: some View {
        ControlCenterPanel(title: "PPS timing engines", subtitle: "FPGA one-pulse-per-second output and input supervision") {
            if monitor.snapshot?.supportsPPS != true {
                Label("Requires active driver 28 / ABI v12 on a supported Meta or Celestica card.", systemImage: "lock.shield")
                    .foregroundStyle(.secondary)
            }
            ForEach(monitor.ppsStates) { engine in
                VStack(alignment: .leading, spacing: 12) {
                    HStack {
                        Label(engine.title, systemImage: engine.core == 1 ? "waveform.path" : "waveform.path.ecg").font(.headline)
                        Spacer()
                        Text("Core " + engine.versionText).font(.caption.monospaced()).foregroundStyle(.secondary)
                        Button("Configure…") { editingService = monitor.selectedServiceID ?? 0; editing = engine }
                            .disabled(engine.writableFields == 0 || monitor.commandInProgress || monitor.state != .connected)
                    }
                    if engine.validFields == 0 {
                        Text("Core absent or register version unrecognized. Only the version was read; settings remain unavailable.")
                            .foregroundStyle(.orange)
                    } else {
                        HStack(spacing: 32) {
                            metric("ENGINE", engine.enabled ? "Enabled" : "Disabled")
                            metric(engine.core == 1 ? "OUTPUT WIDTH" : "MEASURED WIDTH", engine.measuredWidth.map { "\($0) ms" } ?? "Unavailable")
                            metric("ACTIVE LEVEL", engine.has(4) ? (engine.polarity & 1 == 1 ? "High" : "Low") : "Not exposed")
                            metric("CABLE DELAY", engine.delayNanoseconds.map { "\($0) ns" } ?? "Not exposed")
                        }
                        if !engine.enabled { Text("The engine is disabled. Width and delay are register readbacks, not evidence of an active pulse.").font(.caption).foregroundStyle(.secondary) }
                        if engine.has(2) {
                            if engine.errors.isEmpty { Label("No latched PPS errors", systemImage: "checkmark.circle").foregroundStyle(.green) }
                            ForEach(engine.errors, id: \.self) { Label($0, systemImage: "exclamationmark.triangle").foregroundStyle(.orange) }
                        } else { Text("Error status is not exposed by this core version.").foregroundStyle(.secondary) }
                    }
                }.padding(16).background(.quaternary, in: RoundedRectangle(cornerRadius: 12))
            }
            ForEach(monitor.ppsErrors, id: \.self) { Text($0).font(.caption).foregroundStyle(.orange) }
            Text("Changes may interrupt synchronization and connected equipment. The driver checks the previous configuration, disables the engine, and verifies settings before re-enabling. SMA routing, PHC epoch, macOS time and SA53 settings are not changed. Alarm clearing is not yet exposed.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .sheet(item: $editing) { engine in PPSEngineEditor(baseline: engine, serviceID: editingService) }
        .onChange(of: monitor.selectedServiceID) { _, _ in editing = nil }
    }
    private func metric(_ title: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 6) { Text(title).font(.caption).foregroundStyle(.secondary); Text(value).font(.title3.monospacedDigit()) }
    }
}

private struct PPSEngineEditor: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @Environment(\.dismiss) private var dismiss
    let baseline: TimeCardPPSState
    let serviceID: UInt64
    @State private var settings: TimeCardPPSSettings
    @State private var confirm = false
    @State private var openedAt = Date()
    @State private var message = ""
    init(baseline: TimeCardPPSState, serviceID: UInt64) {
        self.baseline = baseline; self.serviceID = serviceID
        _settings = State(initialValue: TimeCardPPSSettings(baseline))
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("Configure " + baseline.title).font(.title2.bold())
            Text("Captured core version " + baseline.versionText + ". Applying briefly disables this engine.")
                .foregroundStyle(.secondary)
            Toggle("Enable PPS engine", isOn: $settings.enabled)
            if baseline.canWrite(4) { Toggle("Active-high pulse", isOn: $settings.activeHigh) }
            if baseline.canWrite(8) { TextField("Output pulse width (1...999 ms)", text: $settings.width).textFieldStyle(.roundedBorder) }
            if baseline.canWrite(16) {
                TextField("Cable delay (ns)", text: $settings.delay).textFieldStyle(.roundedBorder)
                Text("Allowed range: -\(baseline.maximumDelay)...\(baseline.maximumDelay) ns. Signed-magnitude hardware encoding.").font(.caption).foregroundStyle(.secondary)
            }
            if baseline.core == 2 { Text("Input pulse width is measured by the FPGA and cannot be written.").font(.caption).foregroundStyle(.secondary) }
            if let failure = validationError { Text(failure).font(.caption).foregroundStyle(.orange) }
            if !message.isEmpty { Text(message).foregroundStyle(.orange) }
            Text("Close other card-control tools. Settings are checked against this captured baseline. A failed recovery may leave the engine disabled; inspect its state before another operation.")
                .font(.caption).foregroundStyle(.secondary)
            HStack {
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Spacer()
                Button("Review Change…") { confirm = true }
                    .buttonStyle(.borderedProminent)
                    .disabled(validationError != nil || settings.matches(baseline) || monitor.commandInProgress || monitor.state != .connected)
            }
        }.padding(28).frame(width: 520)
        .confirmationDialog("Apply this PPS configuration?", isPresented: $confirm, titleVisibility: .visible) {
            Button("Apply and Verify") {
                guard monitor.selectedServiceID == serviceID, Date().timeIntervalSince(openedAt) < 120 else {
                    message = "The baseline expired or the card changed. Close this editor and refresh."; return
                }
                monitor.setPPS(baseline: baseline, settings: settings, serviceID: serviceID)
                dismiss()
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text(reviewSummary + "\nThis can interrupt PPS synchronization or change signals supplied to connected equipment. PHC epoch and macOS time are unchanged.")
        }
    }
    private var validationError: String? {
        do { _ = try settings.request(baseline: baseline); return nil } catch { return error.localizedDescription }
    }
    private var reviewSummary: String {
        var lines = [baseline.title, "Engine: \(baseline.enabled ? "enabled" : "disabled") → \(settings.enabled ? "enabled" : "disabled")"]
        if baseline.canWrite(4) { lines.append("Active level: \(baseline.polarity & 1 != 0 ? "high" : "low") → \(settings.activeHigh ? "high" : "low")") }
        if baseline.canWrite(8) { lines.append("Width: \(baseline.pulseWidth & 0x3ff) → \(settings.width) ms") }
        if baseline.canWrite(16) { lines.append("Cable delay: \(baseline.delayNanoseconds ?? 0) → \(settings.delay) ns") }
        return lines.joined(separator: "\n")
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
