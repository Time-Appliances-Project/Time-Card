/* SPDX-License-Identifier: BSD-3-Clause */
import AppKit
import Charts
import SwiftUI
import UniformTypeIdentifiers

struct PeripheralMetric: View {
    let title: String, value: String, detail: String
    var color: Color = .cyan
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title.uppercased()).font(.caption.weight(.semibold)).foregroundStyle(.secondary)
            Text(value).font(.title2.monospacedDigit().weight(.semibold)).foregroundStyle(color)
            Text(detail).font(.caption).foregroundStyle(.secondary)
        }.frame(maxWidth: .infinity, alignment: .leading).padding(18)
            .background(color.opacity(0.07), in: RoundedRectangle(cornerRadius: 16))
            .overlay(RoundedRectangle(cornerRadius: 16).strokeBorder(color.opacity(0.15)))
    }
}

enum DeviceLabExport {
    @MainActor static func json<T: Encodable>(_ value: T, name: String) -> String {
        do {
            let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]; encoder.dateEncodingStrategy = .iso8601
            return try save(encoder.encode(value), name: name, type: .json)
        } catch { return "Export failed: " + error.localizedDescription }
    }
    @MainActor static func csv(_ value: String, name: String) -> String {
        do { return try save(Data(value.utf8), name: name, type: .commaSeparatedText) }
        catch { return "Export failed: " + error.localizedDescription }
    }
    @MainActor private static func save(_ data: Data, name: String, type: UTType) throws -> String {
        let panel = NSSavePanel(); panel.allowedContentTypes = [type]; panel.nameFieldStringValue = name
        guard panel.runModal() == .OK, let url = panel.url else { return "" }
        try data.write(to: url, options: .atomic)
        return "Saved " + url.lastPathComponent
    }
}

struct AtomicClockView: View {
    @EnvironmentObject private var lab: DeviceLabMonitor
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var parameter = "DigitalTuning"
    @State private var valueText = ""
    @State private var automatic = false
    @State private var exportMessage = ""
    @State private var pending: Pending?
    private enum Pending {
        case parameter(String, Int64, String), action(SA53Client.Action)
        var detail: String {
            switch self {
            case .parameter(let name, let value, let previous): return "\(name): \(previous) → \(value)."
            case .action(let action): return action.rawValue + "."
            }
        }
    }
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterHeader()
                ControlCenterPanel(title: "SA53 atomic clock", subtitle: "Microchip MAC-SA5x · C3 protocol · MAC UART 2") {
                    HStack(spacing: 18) {
                        Image(systemName: "atom").font(.system(size: 48)).foregroundStyle(.cyan)
                        VStack(alignment: .leading, spacing: 5) {
                            Text(lab.atomic?.values["device?"] ?? "Identify the fitted oscillator").font(.title2.weight(.semibold))
                            Text("57,600 baud · checksummed, sequence-matched replies · serialized UART access")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer()
                        if lab.atomicBusy { ProgressView().controlSize(.small) }
                        Button("Refresh SA53", systemImage: "arrow.clockwise") { lab.refreshAtomic() }
                            .buttonStyle(.borderedProminent).disabled(!lab.supportsAtomic || lab.atomicBusy)
                    }
                    HStack {
                        Toggle("Refresh every 5 seconds while this page is open", isOn: $automatic).toggleStyle(.checkbox)
                            .disabled(!lab.supportsAtomic)
                        Spacer()
                        Button("Export JSON…") {
                            if let snapshot = lab.atomic { exportMessage = DeviceLabExport.json(snapshot, name: "timecard-sa53.json") }
                        }.disabled(lab.atomic == nil)
                    }
                    Text(lab.atomicMessage).font(.callout).textSelection(.enabled)
                    if let snapshot = lab.atomic {
                        TimelineView(.periodic(from: .now, by: 1)) { context in
                            Text("Snapshot: " + snapshot.capturedAt.formatted(date: .abbreviated, time: .standard) +
                                 (context.date.timeIntervalSince(snapshot.capturedAt) > 120 ? " · STALE, refresh before changing settings" : " · Refresh to update these readings"))
                                .font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    if !lab.supportsAtomic {
                        Text("Requires the Meta/Celestica MAC UART and ABI v9 or newer. ART uses a different oscillator protocol and is not sent SA53 commands.")
                            .font(.caption).foregroundStyle(.orange)
                    }
                    if !exportMessage.isEmpty { Text(exportMessage).font(.caption) }
                }
                if let snapshot = lab.atomic {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 190))], spacing: 14) {
                        PeripheralMetric(title: "Physics lock", value: flag(snapshot, "Locked", yes: "Locked", no: "Acquiring"), detail: "Progress: " + formatted(snapshot, "LockProgress", unit: "%"), color: snapshot.boolean("Locked") == true ? .green : .orange)
                        PeripheralMetric(title: "PPS discipline", value: flag(snapshot, "DisciplineLocked", yes: "Locked", no: "Not locked"), detail: "Input: " + flag(snapshot, "PpsInDetected", yes: "Detected", no: "Absent"), color: snapshot.boolean("DisciplineLocked") == true ? .green : .orange)
                        PeripheralMetric(title: "Temperature", value: formatted(snapshot, "Temperature", divisor: 1000, unit: "°C"), detail: "SA53 ambient temperature", color: .pink)
                        PeripheralMetric(title: "Supply", value: formatted(snapshot, "PowerSupply", divisor: 1000, unit: "V"), detail: "Oscillator input telemetry", color: .mint)
                    }
                    ControlCenterPanel(title: "Identity and alarms", subtitle: "Live oscillator identity, not just PCI-card identity") {
                        Grid(alignment: .leading, horizontalSpacing: 28, verticalSpacing: 10) {
                            ForEach(Array(SA53Snapshot.identity.dropFirst()), id: \.self) { key in
                                GridRow { Text(key).foregroundStyle(.secondary); Text(snapshot.values[key] ?? "Unavailable").textSelection(.enabled) }
                            }
                            GridRow {
                                Text("Active alarm mask").foregroundStyle(.secondary)
                                Text(snapshot.integer("Alarms").map { String(format: "0x%08llX", $0) } ?? "Unavailable")
                                    .foregroundStyle(snapshot.integer("Alarms") == 0 ? .green : .orange)
                            }
                        }.font(.callout.monospaced())
                        Text(snapshot.alarmDescription).foregroundStyle(snapshot.integer("Alarms") == 0 ? .green : .orange)
                        Text("Alarm acknowledgement clears the alarm output latch, not the underlying condition.").font(.caption).foregroundStyle(.secondary)
                    }
                    phaseChart
                    ControlCenterPanel(title: "Timing and steering controls", subtitle: "One reviewed parameter per operation, followed by readback") {
                        HStack {
                            Picker("Parameter", selection: $parameter) {
                                ForEach(SA53Parameter.writable) { Text($0.id).tag($0.id) }
                            }.frame(maxWidth: 330)
                            TextField("Integer value", text: $valueText).textFieldStyle(.roundedBorder).frame(width: 160)
                            Text(definition.unit).font(.caption).foregroundStyle(.secondary)
                            Spacer()
                            Button("Review Change…") { review(snapshot) }
                                .disabled(snapshot.integer(parameter) == nil || Int64(valueText) == nil || lab.atomicBusy)
                        }
                        Text("Current: \(snapshot.values[parameter] ?? "Unavailable") · Allowed: \(definition.range.lowerBound)...\(definition.range.upperBound), step \(definition.step)")
                            .font(.caption.monospaced()).foregroundStyle(.secondary)
                        Text("Disciplining and phase metering are mutually exclusive. Analog tuning requires zero digital tuning and disabled disciplining. Disable conflicting modes explicitly before enabling another mode. PPS width is limited to at least 100 ns in this UI.")
                            .font(.caption).foregroundStyle(.secondary)
                        HStack {
                            ForEach(SA53Client.Action.allCases) { action in
                                Button(action.rawValue + "…") { automatic = false; pending = .action(action) }
                            }
                        }
                        Text("Close other oscillator-control applications before applying. These controls affect the oscillator's 10 MHz and 1PPS outputs. JamSync and discipline activation can move PPS phase. Store writes oscillator flash; Load replaces volatile settings. PpsQErr is a transient correction, not a persistent readback setting. No CPU reset, calibration latch, or baud-change command is exposed. This does not set the Mac's clock or establish the card's UTC epoch.")
                            .font(.caption).foregroundStyle(.orange)
                    }
                    ControlCenterPanel(title: "Operating telemetry", subtitle: "Unavailable fields stay explicit when firmware does not implement them") {
                        LazyVGrid(columns: [GridItem(.adaptive(minimum: 250), alignment: .leading)], alignment: .leading, spacing: 14) {
                            ForEach(SA53Snapshot.telemetry, id: \.self) { key in
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(key).font(.caption).foregroundStyle(.secondary)
                                    Text(snapshot.values[key] ?? "Unavailable").font(.callout.monospaced()).textSelection(.enabled)
                                }
                            }
                        }
                        ForEach(snapshot.warnings, id: \.self) { Text($0).font(.caption).foregroundStyle(.orange) }
                    }
                }
            }.padding(24)
        }
        .onChange(of: parameter) { _, _ in valueText = lab.atomic?.values[parameter] ?? "" }
        .onChange(of: lab.atomic?.capturedAt) { _, _ in if valueText.isEmpty { valueText = lab.atomic?.values[parameter] ?? "" } }
        .onChange(of: monitor.selectedServiceID) { _, _ in pending = nil; valueText = ""; automatic = false }
        .task(id: automatic) {
            guard automatic else { return }
            while !Task.isCancelled {
                if !lab.atomicBusy { lab.refreshAtomic() }
                do { try await Task.sleep(for: .seconds(5)) } catch { break }
            }
        }
        .confirmationDialog("Apply this SA53 operation?", isPresented: Binding(get: { pending != nil }, set: { if !$0 { pending = nil } }), titleVisibility: .visible) {
            Button("Apply to SA53", role: .destructive) {
                if let pending {
                    switch pending {
                    case .parameter(let name, let value, _): lab.setAtomic(name, value: value)
                    case .action(let action): lab.atomicAction(action)
                    }
                }
                pending = nil
            }
            Button("Cancel", role: .cancel) { pending = nil }
        } message: {
            Text((pending?.detail ?? "") + " Timing outputs may change immediately. A fresh serial identity and safe mode combination are checked before sending. A timed-out write is never retried automatically; phase changes cannot be undone by a register rollback.")
        }
    }
    private var definition: SA53Parameter { SA53Parameter.writable.first { $0.id == parameter }! }
    private func review(_ snapshot: SA53Snapshot) {
        do {
            guard let value = Int64(valueText) else { return }
            try definition.validate(value)
            automatic = false; pending = .parameter(parameter, value, snapshot.values[parameter] ?? "Unknown")
        } catch { exportMessage = error.localizedDescription }
    }
    private func flag(_ snapshot: SA53Snapshot, _ key: String, yes: String, no: String) -> String {
        snapshot.boolean(key).map { $0 ? yes : no } ?? "Unknown"
    }
    private func formatted(_ snapshot: SA53Snapshot, _ key: String, divisor: Double = 1, unit: String) -> String {
        snapshot.number(key).map { String(format: "%.2f %@", $0 / divisor, unit) } ?? "Unavailable"
    }
    private var phaseChart: some View {
        ControlCenterPanel(title: "PPS phase history", subtitle: "Oscillator phase meter, in nanoseconds; up to 120 refreshes retained") {
            if lab.atomicHistory.contains(where: { $0.phaseMeasurement != nil }) {
                Chart(Array(lab.atomicHistory.enumerated()), id: \.offset) { _, sample in
                    if let phase = sample.phaseMeasurement {
                        LineMark(x: .value("Time", sample.capturedAt), y: .value("Phase", phase)).foregroundStyle(.cyan)
                        PointMark(x: .value("Time", sample.capturedAt), y: .value("Phase", phase)).foregroundStyle(.cyan)
                    }
                }.chartYAxisLabel("ns").frame(height: 180)
            } else { Text("Phase history requires a detected PPS input and active disciplining or phase metering. An inactive raw Phase register is not plotted as zero.").foregroundStyle(.secondary).frame(height: 100) }
        }
    }
}
