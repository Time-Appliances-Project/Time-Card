/* SPDX-License-Identifier: BSD-3-Clause */

import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct ReceiverConsoleInput: Equatable {
    let source: String
    let bytes: [UInt8]
}

@MainActor
final class ReceiverConsoleStore: ObservableObject {
    @Published private(set) var document = ReceiverCaptureDocument(source: "No capture", bytes: [])
    @Published private(set) var loading = false
    @Published var message = ""
    @Published var replay: ReceiverConsoleInput?
    private var generation = 0

    func decode(_ input: ReceiverConsoleInput) async {
        generation += 1
        let current = generation
        loading = true
        let decoded = await Task.detached(priority: .userInitiated) {
            ReceiverCaptureDocument(source: input.source, bytes: input.bytes)
        }.value
        guard current == generation, !Task.isCancelled else { return }
        document = decoded
        loading = false
    }

    func openReplay() {
        let panel = NSOpenPanel()
        panel.title = "Replay Receiver Capture"
        panel.message = "Open raw binary or NMEA text, up to 16 MiB. Replay does not transmit bytes."
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        Task {
            do {
                let bytes = try await Task.detached(priority: .userInitiated) {
                    try ReceiverCaptureDocument.readFile(url)
                }.value
                replay = ReceiverConsoleInput(source: "Replay: \(url.lastPathComponent)", bytes: bytes)
                message = "Loaded \(bytes.count) bytes for offline inspection."
            } catch {
                message = "Replay failed: \(error.localizedDescription)"
            }
        }
    }
}

struct ReceiverConsoleView: View {
    let liveInput: ReceiverConsoleInput
    @StateObject private var store = ReceiverConsoleStore()
    @State private var frozen: ReceiverConsoleInput?
    @State private var protocolName = "All"
    @State private var errorsOnly = false
    @State private var search = ""
    @State private var selectedID: String?
    @State private var rawFormat: ReceiverRawFormat = .hex

    private var input: ReceiverConsoleInput { frozen ?? store.replay ?? liveInput }
    private var matches: [ReceiverStreamMessage] {
        store.document.filtered(protocolName: protocolName, errorsOnly: errorsOnly, search: search)
    }
    private var selectedMessage: ReceiverStreamMessage? {
        store.document.messages.first { $0.id == selectedID }
    }

    var body: some View {
        ControlCenterPanel(title: "Receiver console", subtitle: "Live inspection, offline replay, and decoded capture exports") {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    Button("Replay File…", systemImage: "doc.badge.arrow.up") {
                        frozen = nil
                        store.openReplay()
                    }
                    if store.replay != nil {
                        Button("Return to Live") { frozen = nil; store.replay = nil }
                    }
                    Button(frozen == nil ? "Pause Display" : "Resume Display",
                           systemImage: frozen == nil ? "pause" : "play") {
                        if frozen == nil { frozen = input } else { frozen = nil }
                    }
                    .disabled(input.bytes.isEmpty)
                    Spacer()
                    if store.loading { ProgressView().controlSize(.small) }
                    Menu("Export") {
                        Button("Matching Messages as JSON…") { save(.json) }
                        Button("Matching Messages as CSV…") { save(.commaSeparatedText) }
                        Button("Matching Messages as Text…") { save(.plainText) }
                        Divider()
                        Button("Full Capture as Binary…") { save(.data) }
                    }
                    .disabled(store.document.bytes.isEmpty || store.loading)
                }
                .buttonStyle(.bordered)

                Text(store.document.source).font(.caption).foregroundStyle(.secondary)
                HStack(spacing: 12) {
                    counter("Retained RX", "\(store.document.bytes.count) B")
                    counter("Decoded", "\(store.document.messages.count)")
                    counter("Checksum failures", "\(store.document.failedChecksums)")
                    counter("Unframed bytes", "\(store.document.bytes.count - store.document.decodedBytes)")
                }
                if frozen != nil {
                    Label("Display paused. An active hardware capture continues receiving.", systemImage: "pause.circle")
                        .font(.caption).foregroundStyle(.orange)
                }
                HStack {
                    Picker("Protocol", selection: $protocolName) {
                        ForEach(["All", "UBX", "NMEA", "RTCM3"], id: \.self) { Text($0) }
                    }.frame(maxWidth: 200)
                    TextField("Search message, payload, or offset", text: $search)
                        .textFieldStyle(.roundedBorder)
                    Toggle("Errors only", isOn: $errorsOnly)
                }
                messageTable
                Text("\(matches.count) matching messages. Table shows up to 500; exports include all matches.")
                    .font(.caption).foregroundStyle(.secondary)
                if store.document.decodeLimitReached {
                    Text("Decoded-message limit reached (20,000). The full binary capture is preserved for export.")
                        .font(.caption).foregroundStyle(.orange)
                }
                HStack {
                    Text(selectedMessage.map { "\($0.name) at \($0.offsetText)" } ?? "Raw capture preview")
                        .font(.headline)
                    Spacer()
                    Picker("Format", selection: $rawFormat) {
                        ForEach(ReceiverRawFormat.allCases) { Text($0.rawValue).tag($0) }
                    }.frame(width: 160)
                    if selectedID != nil { Button("Full Capture") { selectedID = nil } }
                }
                ScrollView([.horizontal, .vertical]) {
                    Text(rawPreview).font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(height: 150).padding(10)
                .background(Color.secondary.opacity(0.07), in: RoundedRectangle(cornerRadius: 8))
                Text("Raw preview is limited to 4 KiB. Binary export includes every retained byte, regardless of filters.")
                    .font(.caption).foregroundStyle(.secondary)
                if !store.message.isEmpty {
                    Text(store.message).font(.caption).textSelection(.enabled)
                }
                DisclosureGroup("Receiver summary and satellite sky map") {
                    ReceiverStreamSummaryView(nmeaSentences: store.document.nmeaSentences,
                                              ubxFrames: store.document.ubxFrames,
                                              mixedMessages: store.document.messages)
                }
            }
        }
        .task(id: input) { await store.decode(input) }
        .onChange(of: store.document.source) { _, _ in selectedID = nil }
    }

    private var messageTable: some View {
        Table(Array(matches.prefix(500)), selection: $selectedID) {
            TableColumn("Offset") { Text($0.offsetText).monospaced() }.width(85)
            TableColumn("Protocol", value: \.protocolName).width(65)
            TableColumn("Message", value: \.name).width(90)
            TableColumn("Checksum") { message in
                Text(message.checksumState.label).foregroundStyle(message.checksumState.color)
            }.width(110)
            TableColumn("Summary", value: \.summary)
        }
        .frame(height: 260)
        .overlay {
            if matches.isEmpty {
                ContentUnavailableView("No matching messages", systemImage: "waveform",
                                       description: Text("Capture receiver traffic, open a replay file, or adjust the filters."))
            }
        }
    }

    private var rawPreview: String {
        let bytes = selectedMessage.map { store.document.payload(for: $0) } ?? store.document.bytes[...]
        return bytes.isEmpty ? "No bytes loaded." : rawFormat.render(bytes.prefix(4096))
    }

    private func counter(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(value).font(.title3.monospacedDigit().weight(.semibold))
            Text(label).font(.caption).foregroundStyle(.secondary)
        }.frame(maxWidth: .infinity, alignment: .leading)
    }

    private func save(_ type: UTType) {
        let panel = NSSavePanel()
        panel.title = "Export Receiver Capture"
        let suffix = type == .data ? "bin" : type.preferredFilenameExtension ?? "txt"
        panel.nameFieldStringValue = "TimeCard-Receiver.\(suffix)"
        panel.allowedContentTypes = [type]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        let document = store.document
        let selected = matches
        Task {
            do {
                try await Task.detached(priority: .userInitiated) {
                    let data: Data
                    if type == .json { data = try document.exportJSON(selected) }
                    else if type == .commaSeparatedText { data = Data(document.exportCSV(selected).utf8) }
                    else if type == .plainText { data = Data(document.exportText(selected).utf8) }
                    else { data = Data(document.bytes) }
                    try data.write(to: url, options: .atomic)
                }.value
                store.message = "Saved \(url.lastPathComponent)."
            } catch { store.message = "Export failed: \(error.localizedDescription)" }
        }
    }
}
