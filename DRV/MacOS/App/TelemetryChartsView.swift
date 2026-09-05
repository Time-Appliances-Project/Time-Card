/* SPDX-License-Identifier: BSD-3-Clause */

import AppKit
import Charts
import SwiftUI
import UniformTypeIdentifiers

struct TelemetryChartsView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var frozen: [TimeCardTelemetrySample]?
    @State private var duration: Double = 300
    @State private var selectedDate: Date?
    @State private var message = ""
    @State private var confirmClear = false

    private var samples: [TimeCardTelemetrySample] {
        let history = frozen ?? monitor.telemetryHistory
        guard let end = history.last?.timestamp else { return [] }
        return history.filter { $0.timestamp >= end.addingTimeInterval(-duration) }
    }
    private var statistics: SamplingWindowStatistics {
        SamplingWindowStatistics(samples.map(\.samplingWindowNanoseconds))
    }
    private var inspected: TimeCardTelemetrySample? {
        guard let date = selectedDate else { return nil }
        return samples.min { abs($0.timestamp.timeIntervalSince(date)) < abs($1.timestamp.timeIntervalSince(date)) }
    }

    var body: some View {
        VStack(spacing: 18) {
            ControlCenterPanel(title: "Live telemetry", subtitle: "Sampling precision, receiver satellites, and board temperatures") {
                HStack {
                    Picker("History", selection: $duration) {
                        Text("1 minute").tag(60.0)
                        Text("5 minutes").tag(300.0)
                        Text("15 minutes").tag(900.0)
                        Text("1 hour").tag(3600.0)
                    }.frame(width: 190)
                    Button(frozen == nil ? "Pause Charts" : "Resume Charts",
                           systemImage: frozen == nil ? "pause" : "play") {
                        frozen = frozen == nil ? monitor.telemetryHistory : nil
                        selectedDate = nil
                    }.disabled(monitor.telemetryHistory.isEmpty && frozen == nil)
                    Spacer()
                    Menu("Export Visible History") {
                        Button("Save JSON…") { export(recording: false, json: true) }
                        Button("Save CSV…") { export(recording: false, json: false) }
                    }.disabled(samples.isEmpty)
                }
                Text("\(samples.count) samples in this view. Click or drag across a chart to inspect a sample.")
                    .font(.caption).foregroundStyle(.secondary)
                if frozen != nil {
                    Text("Charts paused. Collection and session recording continue.")
                        .font(.caption).foregroundStyle(.orange)
                }
                samplingChart
                if let sample = inspected {
                    Text("\(sample.timestamp.formatted(date: .omitted, time: .standard)) · \(ns(sample.samplingWindowNanoseconds)) · locked satellites: \(sample.lockedSatellites.map(String.init) ?? "unavailable")")
                        .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                }
            }
            histogram
            satelliteChart
            temperatureChart
            recordingPanel
        }
        .onChange(of: monitor.selectedServiceID) { _, _ in frozen = nil; selectedDate = nil }
        .confirmationDialog("Discard this recording? Export it first to keep a copy.", isPresented: $confirmClear) {
            Button("Discard Recording", role: .destructive) { monitor.clearTelemetryRecording() }
            Button("Cancel", role: .cancel) { }
        }
    }

    private var samplingChart: some View {
        Chart {
            ForEach(samples) { sample in
                LineMark(x: .value("Observed at", sample.timestamp),
                         y: .value("Window (ns)", sample.samplingWindowNanoseconds))
                    .foregroundStyle(.cyan)
            }
            if let date = selectedDate { RuleMark(x: .value("Selected", date)).foregroundStyle(.secondary.opacity(0.4)) }
        }
        .chartXSelection(value: $selectedDate)
        .chartYAxisLabel("Sampling window (ns)")
        .frame(height: 190)
        .overlay { if samples.isEmpty { Text("Waiting for live cross-timestamps").foregroundStyle(.secondary) } }
    }

    private var histogram: some View {
        ControlCenterPanel(title: "Sampling-window distribution", subtitle: "Percentiles use the visible history and linear interpolation") {
            HStack {
                statistic("Median", statistics.percentile(0.5))
                statistic("95th percentile", statistics.percentile(0.95))
                statistic("99th percentile", statistics.percentile(0.99))
                statistic("Maximum", statistics.values.last)
            }
            Chart(statistics.histogram()) { bin in
                BarMark(xStart: .value("From", bin.lower), xEnd: .value("To", bin.upper),
                        y: .value("Samples", bin.count))
                    .foregroundStyle(.cyan.gradient)
            }
            .chartXAxisLabel("Sampling window (ns)")
            .chartYAxisLabel("Samples")
            .frame(height: 170)
        }
    }

    private var satelliteChart: some View {
        ControlCenterPanel(title: "GNSS satellite history", subtitle: "Only samples marked valid by the ToD engine are plotted") {
            if samples.contains(where: { $0.seenSatellites != nil || $0.lockedSatellites != nil }) {
                Chart(samples) { sample in
                    if let seen = sample.seenSatellites {
                        LineMark(x: .value("Time", sample.timestamp), y: .value("Satellites", seen), series: .value("Status", "Seen"))
                            .foregroundStyle(by: .value("Status", "Seen"))
                    }
                    if let locked = sample.lockedSatellites {
                        LineMark(x: .value("Time", sample.timestamp), y: .value("Satellites", locked), series: .value("Status", "Locked"))
                            .foregroundStyle(by: .value("Status", "Locked"))
                    }
                }
                .chartXSelection(value: $selectedDate).frame(height: 170)
            } else { Text("No valid satellite-count samples in this interval.").foregroundStyle(.secondary) }
        }
    }

    private var temperatureChart: some View {
        ControlCenterPanel(title: "Board temperature history", subtitle: "Independent sensor series, in degrees Celsius") {
            if samples.contains(where: { !$0.temperaturesCelsius.isEmpty }) {
                Chart(samples) { sample in
                    ForEach(sample.temperaturesCelsius.keys.sorted(), id: \.self) { name in
                        if let value = sample.temperaturesCelsius[name] {
                            LineMark(x: .value("Time", sample.timestamp), y: .value("Temperature (°C)", value), series: .value("Sensor", name))
                                .foregroundStyle(by: .value("Sensor", name))
                        }
                    }
                }
                .chartXSelection(value: $selectedDate).frame(height: 190)
            } else { Text("No valid temperature samples in this interval.").foregroundStyle(.secondary) }
        }
    }

    private var recordingPanel: some View {
        ControlCenterPanel(title: "Session recording", subtitle: "Record up to 21,600 observations, approximately six hours at one sample per second") {
            HStack {
                if monitor.telemetryRecording == nil {
                    Button("Start Recording", systemImage: "record.circle") { monitor.startTelemetryRecording() }
                        .buttonStyle(.borderedProminent).disabled(monitor.snapshot == nil)
                } else if monitor.telemetryRecording?.isRecording == true {
                    Button("Stop Recording", systemImage: "stop.circle") { monitor.stopTelemetryRecording() }
                } else {
                    Button("Discard Recording…", role: .destructive) { confirmClear = true }
                }
                Spacer()
                Text("\(monitor.telemetryRecording?.samples.count ?? 0) recorded samples")
                    .font(.caption.monospacedDigit())
                Menu("Export Recording") {
                    Button("Save JSON…") { export(recording: true, json: true) }
                    Button("Save CSV…") { export(recording: true, json: false) }
                }.disabled(monitor.telemetryRecording?.samples.isEmpty != false)
            }
            if let recording = monitor.telemetryRecording {
                Text(recording.isRecording ? "Recording since \(recording.startedAt.formatted())." : (recording.stopReason ?? "Recording stopped."))
                    .font(.caption).foregroundStyle(recording.isRecording ? .green : .secondary)
            }
            Text("Recordings stay in memory until exported. Missing readings remain empty; PHC offset and vibration await driver support.")
                .font(.caption).foregroundStyle(.secondary)
            if !message.isEmpty { Text(message).font(.caption).textSelection(.enabled) }
        }
    }

    private func statistic(_ title: String, _ value: Double?) -> some View {
        VStack(alignment: .leading) {
            Text(value.map(ns) ?? "Waiting").font(.headline.monospacedDigit())
            Text(title).font(.caption).foregroundStyle(.secondary)
        }.frame(maxWidth: .infinity, alignment: .leading)
    }

    private func ns(_ value: Double) -> String {
        value >= 1000 ? String(format: "%.2f µs", value / 1000) : String(format: "%.0f ns", value)
    }

    private func export(recording: Bool, json: Bool) {
        let captured = recording ? monitor.telemetryRecording?.samples ?? [] : samples
        let session = recording ? monitor.telemetryRecording : nil
        let panel = NSSavePanel()
        panel.title = recording ? "Save Telemetry Recording" : "Save Visible Telemetry"
        panel.nameFieldStringValue = "TimeCard-\(recording ? "Recording" : "History").\(json ? "json" : "csv")"
        panel.allowedContentTypes = json ? [.json] : [.commaSeparatedText]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        Task {
            do {
                try await Task.detached(priority: .userInitiated) {
                    let data = json ? try TimeCardTelemetryExport.json(captured, startedAt: session?.startedAt,
                                                                       stoppedAt: session?.stoppedAt, stopReason: session?.stopReason)
                        : Data(TimeCardTelemetryExport.csv(captured).utf8)
                    try data.write(to: url, options: .atomic)
                }.value
                message = "Saved \(captured.count) samples to \(url.lastPathComponent)."
            } catch { message = "Export failed: \(error.localizedDescription)" }
        }
    }
}
