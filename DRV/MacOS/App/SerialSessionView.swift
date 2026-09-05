/* SPDX-License-Identifier: BSD-3-Clause */
import SwiftUI
import AppKit
import UniformTypeIdentifiers

private struct SerialConnectionReview: Identifiable {
    let id = UUID()
    let path: String
    let settings: SerialLineSettings
}

struct SerialSessionView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @EnvironmentObject private var session: SerialSessionController
    @State private var path = ""
    @State private var settings = SerialLineSettings()
    @State private var connectionReview: SerialConnectionReview?
    @State private var sendReview: SerialSendReview?
    @State private var input = ""
    @State private var format: SerialSendFormat = .text
    @State private var ending: SerialLineEnding = .none
    @State private var direction = "All"
    @State private var showHex = true
    @State private var pausedEvents: [SerialTimelineEvent]?
    @State private var localMessage = ""

    var body: some View {
        VStack(spacing: 18) {
            ControlCenterPanel(title: "Persistent serial session", subtitle: "Native macOS serial ports · independent of the card's FPGA UARTs") {
                VStack(alignment: .leading, spacing: 14) {
                    HStack {
                        Picker("Port", selection: $path) {
                            Text("Select a port").tag("")
                            ForEach(monitor.serialPorts) { Text("\($0.displayName) (\($0.calloutDevice))").tag($0.calloutDevice) }
                        }.frame(maxWidth: 520)
                        Spacer()
                        Label(session.state.rawValue, systemImage: session.state == .connected ? "cable.connector" : "circle")
                            .foregroundStyle(session.state == .connected ? .green : .secondary)
                    }
                    .disabled(session.active)
                    lineSettings.disabled(session.active)
                    HStack(spacing: 12) {
                        Button(session.record == nil ? "Connect…" : "New Session…") {
                            connectionReview = .init(path: path, settings: settings)
                        }.buttonStyle(.borderedProminent)
                            .disabled(session.active || !monitor.serialPorts.contains(where: { $0.calloutDevice == path }) || monitor.serialCaptureInProgress || monitor.timeReferenceSessionInProgress)
                        Button("Disconnect") { session.disconnect() }.disabled(!session.active)
                        Spacer()
                        Text("RX \(session.record?.receivedBytes ?? 0) B · TX accepted \(session.record?.transmittedBytes ?? 0) B")
                            .font(.caption.monospaced()).foregroundStyle(.secondary)
                    }
                    Text(session.message).font(.callout).textSelection(.enabled)
                    if let record = session.record { Text("Session port: \(record.port) · \(record.settings.summary)").font(.caption.monospaced()).foregroundStyle(.secondary) }
                    Text("Close other serial clients before connecting. Opening a port changes host line settings and may toggle device control lines. Settings stay fixed until disconnect. Sessions continue across pages, but stop on sleep, quit, error or after 24 hours; there is no automatic reconnect. This is not a trusted-time source.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }

            ControlCenterPanel(title: "Send to serial device", subtitle: "Explicit review required · at most 4 KiB per send") {
                VStack(alignment: .leading, spacing: 12) {
                    HStack {
                        Picker("Format", selection: $format) { ForEach(SerialSendFormat.allCases, id: \.self) { Text($0.rawValue).tag($0) } }.frame(width: 250)
                        Picker("Line ending", selection: $ending) { ForEach(SerialLineEnding.allCases, id: \.self) { Text($0.rawValue).tag($0) } }.frame(width: 210).disabled(format == .hex)
                        Spacer()
                        Button("Review Send…") {
                            do { sendReview = try session.reviewSend(input, format: format, ending: ending); localMessage = "" }
                            catch { localMessage = error.localizedDescription }
                        }.disabled(session.state != .connected || session.sending || input.isEmpty)
                    }
                    TextEditor(text: $input).font(.system(.body, design: .monospaced))
                        .frame(height: 75).scrollContentBackground(.hidden)
                        .padding(8).background(.quaternary.opacity(0.3), in: RoundedRectangle(cornerRadius: 8))
                        .accessibilityLabel("Serial send payload")
                        .onChange(of: input) { _, value in if value.count > 12_288 { input = String(value.prefix(12_288)) } }
                    Text("Hex sends exactly the listed bytes, with no added line ending. Commands may reset a device or change persistent configuration. TX totals mean the OS accepted bytes, not that the device received or acknowledged them. No automatic retries.")
                        .font(.caption).foregroundStyle(.secondary)
                    if !localMessage.isEmpty { Text(localMessage).font(.caption).foregroundStyle(.orange).textSelection(.enabled) }
                }
            }

            ControlCenterPanel(title: "Serial RX / TX timeline", subtitle: "Host observation times · bounded session history") {
                VStack(alignment: .leading, spacing: 12) {
                    HStack {
                        Picker("Direction", selection: $direction) { ForEach(["All", "RX", "TX accepted", "Status"], id: \.self) { Text($0).tag($0) } }.frame(width: 210)
                        Toggle("Hex", isOn: $showHex).toggleStyle(.switch).controlSize(.small)
                        Button(pausedEvents == nil ? "Pause Display" : "Resume Display") { pausedEvents = pausedEvents == nil ? session.record?.events ?? [] : nil }
                        Spacer()
                        Menu("Export Session") {
                            Button("JSON evidence…") { export("json") }
                            Button("CSV timeline…") { export("csv") }
                            Button("Retained RX binary…") { export("bin") }
                        }.disabled(session.record == nil)
                    }
                    Text("\(session.record?.events.count ?? 0) retained events · \(session.record?.evictedEvents ?? 0) older events / \(session.record?.evictedBytes ?? 0) bytes evicted. Limit: 500 events and 1 MiB. Exports use all retained events, not the display filter.\(pausedEvents == nil ? "" : " Display paused; acquisition continues.")")
                        .font(.caption).foregroundStyle(.secondary)
                    if visibleEvents.isEmpty { Text("No matching traffic yet.").foregroundStyle(.secondary).frame(maxWidth: .infinity, minHeight: 90) }
                    else {
                        ScrollView {
                            LazyVStack(alignment: .leading, spacing: 8) {
                                ForEach(visibleEvents.suffix(100).reversed()) { event in
                                    HStack(alignment: .top, spacing: 12) {
                                        Text(String(format: "+%.3f s", event.elapsedSeconds)).frame(width: 90, alignment: .trailing).foregroundStyle(.secondary)
                                        Text(event.direction.rawValue).frame(width: 90, alignment: .leading).foregroundStyle(event.direction == .rx ? .teal : .orange)
                                        VStack(alignment: .leading, spacing: 4) {
                                            if !event.note.isEmpty { Text(event.note) }
                                            if !event.bytes.isEmpty {
                                                Text(showHex ? SerialPayload.hex(event.bytes.prefix(256)) : SerialPayload.visibleText(Array(event.bytes.prefix(256))))
                                                Text("\(event.bytes.count) bytes\(event.bytes.count > 256 ? "; display preview limited to 256 bytes, export retains the full event" : "")").foregroundStyle(.secondary)
                                            }
                                        }.frame(maxWidth: .infinity, alignment: .leading).textSelection(.enabled)
                                    }.font(.system(.caption, design: .monospaced))
                                    Divider()
                                }
                            }
                        }.frame(height: 230)
                    }
                }
            }
        }
        .sheet(item: $connectionReview) { review in
            VStack(alignment: .leading, spacing: 18) {
                Text("Open serial session?").font(.title2.bold())
                Text(review.path).font(.body.monospaced()).textSelection(.enabled)
                Text(review.settings.summary)
                Text("This changes host serial settings and requests exclusive access. Opening or closing may change DTR/RTS and reset some devices. Close other serial clients first. No command bytes are sent by Connect. A new session replaces the previous in-memory history, so export it first if needed.")
                HStack { Spacer(); Button("Cancel") { connectionReview = nil }; Button("Connect") { connect(review); connectionReview = nil }.buttonStyle(.borderedProminent) }
            }.padding(24).frame(width: 520)
        }
        .sheet(item: $sendReview) { review in
            VStack(alignment: .leading, spacing: 16) {
                Text("Send \(review.bytes.count) bytes?").font(.title2.bold())
                Text(review.port).font(.body.monospaced()).textSelection(.enabled)
                Text(review.settings.summary).foregroundStyle(.secondary)
                ScrollView { Text(SerialPayload.hex(review.bytes)).font(.body.monospaced()).textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading) }.frame(height: 160)
                Text("These exact bytes will be transmitted. They may change device configuration or reset hardware. Review expires after 30 seconds and cannot be reused after reconnecting.").foregroundStyle(.orange)
                HStack { Spacer(); Button("Cancel") { sendReview = nil }; Button("Send Bytes", role: .destructive) { session.send(review); sendReview = nil } }
            }.padding(24).frame(width: 560)
        }
        .onChange(of: session.record?.id) { _, _ in pausedEvents = nil; sendReview = nil }
        .onAppear {
            if let record = session.record { path = record.port; settings = record.settings }
        }
    }
    private var lineSettings: some View {
        VStack(alignment: .leading) {
          HStack {
            Picker("Baud", selection: $settings.baud) { ForEach(SerialLineSettings.baudRates, id: \.self) { Text(String($0)).tag($0) } }.frame(width: 155)
            Picker("Data", selection: $settings.dataBits) { ForEach(5...8, id: \.self) { Text(String($0)).tag($0) } }.frame(width: 95)
            Picker("Parity", selection: $settings.parity) { ForEach(SerialLineSettings.Parity.allCases, id: \.self) { Text($0.rawValue).tag($0) } }.frame(width: 135)
            Spacer()
          }
          HStack {
            Picker("Stop", selection: $settings.stopBits) { Text("1").tag(1); Text("2").tag(2) }.frame(width: 95)
            Picker("Flow", selection: $settings.flow) { ForEach(SerialLineSettings.Flow.allCases, id: \.self) { Text($0.rawValue).tag($0) } }.frame(width: 280)
            Spacer()
          }
        }
    }
    private var visibleEvents: [SerialTimelineEvent] { (pausedEvents ?? session.record?.events ?? []).filter { direction == "All" || $0.direction.rawValue == direction } }
    private func connect(_ review: SerialConnectionReview) {
        guard monitor.beginNativeSerialSession() else { localMessage = "Finish the serial preview or receiver-time session first."; return }
        session.connect(path: review.path, settings: review.settings, knownPorts: monitor.serialPorts.map(\.calloutDevice)) { [weak monitor] in monitor?.endNativeSerialSession() }
    }
    private func export(_ format: String) {
        guard let record = session.record else { return }
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "serial-session-\(record.id.uuidString.prefix(8)).\(format)"
        panel.allowedContentTypes = [format == "json" ? .json : format == "csv" ? .commaSeparatedText : .data]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let data = try format == "json" ? record.json() : format == "csv" ? Data(record.csv().utf8) : Data(record.retainedRX)
            try data.write(to: url, options: .atomic)
            localMessage = "Exported retained session snapshot. \(record.evictedEvents) older events were already evicted. Binary contains RX only."
        } catch { localMessage = "Export failed: \(error.localizedDescription)" }
    }
}
