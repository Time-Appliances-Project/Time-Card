/* SPDX-License-Identifier: BSD-3-Clause */
import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct ConfigurationProfilesView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @State private var fileMessage = ""
    @State private var confirmApply = false
    @StateObject private var library = ProfileLibrary()
    @State private var selectedEntry: UUID?
    @State private var pendingRemoval: ProfileLibraryEntry?

    var body: some View {
        ControlCenterPanel(title: "Configuration profiles", subtitle: "Capture, review, apply, and recover supported hardware settings") {
            HStack {
                Button("Capture Current", systemImage: "camera") { monitor.captureProfile() }
                    .disabled(monitor.state != .connected)
                Button("Import JSON…", systemImage: "square.and.arrow.down") { importProfile() }
                Button("Import Windows XML…") { importWindowsProfiles() }
                    .disabled(monitor.state != .connected || monitor.profileOperationInProgress)
                Button("Save JSON…", systemImage: "square.and.arrow.up") {
                    if let profile = monitor.configurationProfile { saveProfile(profile) }
                }.disabled(monitor.configurationProfile == nil)
                Spacer()
                if monitor.profileOperationInProgress { ProgressView().controlSize(.small) }
            }
            Text("Importing and editing never write to the card. Profiles match PCI identity, revision, layout, and clock version, not a unique board serial number. Review the selected card before applying.")
                .font(.caption).foregroundStyle(.secondary)
            libraryPanel
            windowsImportPanel

            if let profile = monitor.configurationProfile {
                TextField("Profile name", text: Binding(get: { profile.name }, set: { name in
                    var changed = profile; changed.name = name; monitor.editProfile(changed)
                })).textFieldStyle(.roundedBorder)
                Text(profile.target.summary).font(.caption.monospaced()).textSelection(.enabled)
                VStack(spacing: 12) {
                    ForEach(profile.settings) { setting in
                        ProfileSettingEditor(setting: setting) { changed in
                            var updated = profile
                            if let index = updated.settings.firstIndex(where: { $0.id == changed.id }) {
                                updated.settings[index] = changed
                                monitor.editProfile(updated)
                            }
                        }
                    }
                }.padding(14).background(.quaternary, in: RoundedRectangle(cornerRadius: 12))
                HStack {
                    Button("Preview Changes") { monitor.previewProfile() }
                        .disabled(monitor.state != .connected)
                    Button("Apply Reviewed Profile…") { confirmApply = true }
                        .buttonStyle(.borderedProminent)
                        .disabled(monitor.profilePlan?.canApply != true || monitor.state != .connected)
                    Spacer()
                }
                if let plan = monitor.profilePlan {
                    VStack(alignment: .leading, spacing: 8) {
                        Label("\(plan.changes.count) changes · \(plan.differences.count - plan.changes.count) unchanged",
                              systemImage: plan.canApply ? "checkmark.shield" : "exclamationmark.shield")
                            .font(.headline).foregroundStyle(plan.canApply ? .green : .orange)
                        ForEach(plan.differences) { diff in
                            HStack(alignment: .top) {
                                Text(diff.requested.title).frame(width: 105, alignment: .leading)
                                Text(diff.previous?.summary ?? "Unavailable").foregroundStyle(.secondary)
                                Image(systemName: "arrow.right").foregroundStyle(.secondary)
                                Text(diff.requested.summary)
                                Spacer()
                                Text(diff.blocker ?? (diff.changed ? "Change" : "Unchanged"))
                                    .foregroundStyle(diff.blocker == nil ? Color.secondary : .orange)
                            }.font(.caption)
                        }
                        ForEach(plan.blockers, id: \.self) { Text($0).font(.caption).foregroundStyle(.orange) }
                    }
                }
            } else {
                Label("Capture the connected card or import a saved profile to begin.", systemImage: "doc.badge.plus")
                    .foregroundStyle(.secondary).padding(.vertical, 12)
            }

            if let state = monitor.profileState {
                ForEach(state.notes, id: \.self) { Text($0).font(.caption).foregroundStyle(.secondary) }
            }
            if !monitor.profileMessage.isEmpty { Text(monitor.profileMessage).font(.callout).textSelection(.enabled) }
            if !fileMessage.isEmpty { Text(fileMessage).font(.caption).textSelection(.enabled) }

            if let report = monitor.profileReport {
                Divider()
                HStack {
                    Label("Last apply: " + report.profileName + " / " + report.outcome.rawValue,
                          systemImage: report.successful ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                        .foregroundStyle(report.successful ? .green : .orange)
                    Spacer()
                    Button("Save Report…") { saveReport(report) }
                    if let recovery = report.recoveryProfile {
                        Button("Save Recovery Profile…") { saveProfile(recovery) }
                        Button("Review Recovery") { monitor.stageProfile(recovery) }
                    }
                }
                ForEach(Array(report.events.enumerated()), id: \.offset) { _, event in
                    Text(event).font(.caption.monospaced()).textSelection(.enabled)
                }
            }
            Text("Close other applications that write to the card. Apply is a sequence, not an atomic hardware transaction. SMA lacks driver-level compare-and-set. On failure, only identifiable changes are rolled back; unknown states require manual recovery. Save profiles and reports before quitting.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .confirmationDialog("Apply this profile to the selected Time Card?", isPresented: $confirmApply, titleVisibility: .visible) {
            Button("Apply and Verify") { monitor.applyProfile() }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text("\(monitor.profilePlan?.changes.count ?? 0) setting(s) will change. Clock-source, SMA, and PPS changes can interrupt synchronization or drive connected equipment. The preview must still match the card and be less than two minutes old. PHC epoch and macOS time are not changed.")
        }
        .onChange(of: monitor.selectedServiceID) { _, _ in confirmApply = false }
        .onChange(of: monitor.configurationProfile) { _, _ in confirmApply = false }
        .onAppear { library.reload() }
        .confirmationDialog("Move this saved profile to Trash?", isPresented: Binding(get: { pendingRemoval != nil }, set: { if !$0 { pendingRemoval = nil } }), titleVisibility: .visible) {
            Button("Move to Trash", role: .destructive) {
                if let entry = pendingRemoval { library.remove(entry); selectedEntry = nil }
                pendingRemoval = nil
            }
            Button("Cancel", role: .cancel) { pendingRemoval = nil }
        } message: { Text(pendingRemoval?.profile.name ?? "") }
    }

    private var libraryPanel: some View {
        DisclosureGroup("Saved profile library (\(library.entries.count))") {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Button("Save New Snapshot", systemImage: "square.stack.3d.up.badge.plus") {
                        if let profile = monitor.configurationProfile { library.save(profile) }
                    }.disabled(monitor.configurationProfile == nil)
                    Button("Reload", systemImage: "arrow.clockwise") { library.reload() }
                    Spacer()
                    if let entry = library.entries.first(where: { $0.id == selectedEntry }) {
                        Button("Stage for Review") {
                            if let profile = library.read(entry) { monitor.stageProfile(profile) }
                        }.buttonStyle(.borderedProminent)
                        Button("Move to Trash…", role: .destructive) { pendingRemoval = entry }
                    }
                }
                if !library.entries.isEmpty {
                    List(library.entries, selection: $selectedEntry) { entry in
                        VStack(alignment: .leading, spacing: 4) {
                            HStack {
                                Text(entry.profile.name).font(.headline)
                                Spacer()
                                Text(entry.savedAt.formatted(date: .abbreviated, time: .shortened)).font(.caption)
                            }
                            Text(entry.profile.target.summary + " · \(entry.profile.settings.count) settings")
                                .font(.caption.monospaced()).foregroundStyle(.secondary)
                        }.tag(entry.id).padding(.vertical, 3)
                    }.frame(height: min(230, CGFloat(library.entries.count) * 58 + 10))
                }
                Text(library.message).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
                ForEach(library.warnings, id: \.self) { Text($0).font(.caption).foregroundStyle(.orange) }
                Text("Stored in your Application Support folder. Every save creates a separate snapshot. Staging never applies settings, and removal is recoverable from Trash.")
                    .font(.caption).foregroundStyle(.secondary)
            }.padding(.vertical, 10)
        }
    }

    private func importProfile() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.json]
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let handle = try FileHandle(forReadingFrom: url)
            defer { try? handle.close() }
            let data = try handle.read(upToCount: TimeCardConfigurationProfile.maximumFileBytes + 1) ?? Data()
            let profile = try TimeCardConfigurationProfile.decode(data)
            monitor.stageProfile(profile)
            fileMessage = "Imported " + url.lastPathComponent + ". No hardware writes were made."
        } catch { fileMessage = "Import failed: " + error.localizedDescription }
    }
    @ViewBuilder private var windowsImportPanel: some View {
        if !monitor.windowsProfileReviews.isEmpty {
            VStack(alignment: .leading, spacing: 12) {
                Label("Windows XML import review", systemImage: "doc.text.magnifyingglass").font(.headline)
                Text("Each entry is imported in full or blocked. Unsupported settings and FPGA image constraints are never silently discarded.")
                    .font(.caption).foregroundStyle(.secondary)
                ForEach(monitor.windowsProfileReviews) { review in
                    VStack(alignment: .leading, spacing: 6) {
                        HStack {
                            Label(review.name, systemImage: review.profile == nil ? "exclamationmark.shield" : "checkmark.shield")
                                .font(.headline)
                            Spacer()
                            Button("Stage Native Copy") { monitor.stageWindowsProfile(review) }
                                .disabled(review.profile == nil || review.serviceID != monitor.selectedServiceID || monitor.profileOperationInProgress)
                        }
                        ForEach(Array(review.notes.enumerated()), id: \.offset) { _, note in
                            Text(note).font(.caption).foregroundStyle(.secondary)
                        }
                        if let profile = review.profile {
                            ForEach(profile.settings) { setting in
                                Text(setting.title + ": " + setting.summary).font(.caption.monospaced())
                            }
                        }
                        ForEach(Array(review.blockers.enumerated()), id: \.offset) { _, blocker in
                            Text(blocker).font(.caption).foregroundStyle(.orange)
                        }
                    }.padding(12).background(.quaternary, in: RoundedRectangle(cornerRadius: 10))
                }
            }.padding(.vertical, 8)
        }
    }

    private func importWindowsProfiles() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.xml]
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let handle = try FileHandle(forReadingFrom: url)
            defer { try? handle.close() }
            let data = try handle.read(upToCount: TimeCardConfigurationProfile.maximumFileBytes + 1) ?? Data()
            monitor.inspectWindowsProfiles(data)
            fileMessage = "Inspecting " + url.lastPathComponent + ". Import never writes to hardware."
        } catch { fileMessage = "Windows import failed: " + error.localizedDescription }
    }
    private func saveProfile(_ profile: TimeCardConfigurationProfile) {
        do { try save(data: profile.encoded(), name: "timecard-profile.json") }
        catch { fileMessage = "Save failed: " + error.localizedDescription }
    }
    private func saveReport(_ report: TimeCardProfileApplyReport) {
        do {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            encoder.dateEncodingStrategy = .iso8601
            try save(data: encoder.encode(report), name: "timecard-profile-result.json")
        } catch { fileMessage = "Save failed: " + error.localizedDescription }
    }
    private func save(data: Data, name: String) throws {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = name
        guard panel.runModal() == .OK, let url = panel.url else { return }
        try data.write(to: url, options: .atomic)
        fileMessage = "Saved " + url.lastPathComponent
    }
}

private struct ProfileSettingEditor: View {
    let setting: TimeCardProfileSetting
    let update: (TimeCardProfileSetting) -> Void
    private func value(_ index: Int) -> Binding<UInt32> {
        Binding(get: { setting.values[index] }, set: { value in
            var changed = setting; changed.values[index] = value
            if setting.kind == .smaRoute && index == 0 { changed.values[1] = 0 }
            update(changed)
        })
    }
    var body: some View {
        HStack {
            Text(setting.title).font(.headline).frame(width: 110, alignment: .leading)
            switch setting.kind {
            case .clockSource:
                Picker("Source", selection: value(0)) {
                    ForEach(TimeCardClockControlState.knownSources, id: \.self) { source in
                        Text(TimeCardClockControlState.name(source)).tag(source)
                    }
                }.labelsHidden()
            case .frequency:
                Stepper(value: Binding(get: { Int(setting.values[0]) }, set: { value(0).wrappedValue = UInt32($0) }), in: 0...255) {
                    Text(setting.summary)
                }.help("0 disables this counter; 1...255 sets integration seconds.")
            case .pps:
                VStack(alignment: .leading, spacing: 6) {
                    Text(setting.summary)
                    HStack {
                        Toggle("Enabled", isOn: Binding(get: { setting.values[2] == 1 }, set: { value(2).wrappedValue = $0 ? 1 : 0 }))
                        if setting.values[1] & 4 != 0 {
                            Toggle("Active high", isOn: Binding(get: { setting.values[3] == 1 }, set: { value(3).wrappedValue = $0 ? 1 : 0 }))
                        }
                        if setting.values[1] & 8 != 0 {
                            TextField("Width (ms)", value: value(4), format: .number.grouping(.never)).frame(width: 90)
                                .help("Output pulse width: 1...999 ms")
                        }
                        if setting.values[1] & 16 != 0 {
                            TextField("Delay (ns)", value: Binding(get: { Int32(bitPattern: setting.values[5]) },
                                set: { value(5).wrappedValue = UInt32(bitPattern: $0) }), format: .number.grouping(.never))
                                .frame(width: 110).help("Signed cable delay in nanoseconds, limited by the PPS core version")
                        }
                    }.textFieldStyle(.roundedBorder)
                    Text(String(format: "Core 0x%08x · writable mask 0x%02x", setting.values[0], setting.values[1]))
                        .font(.caption.monospaced()).foregroundStyle(.secondary)
                }
            case .smaRoute:
                Picker("Direction", selection: value(0)) {
                    ForEach(TimeCardSMADirection.allCases) { Text($0.label).tag($0.rawValue) }
                }.frame(width: 150).labelsHidden()
                if let direction = TimeCardSMADirection(rawValue: setting.values[0]), direction != .disabled {
                    Picker("Function", selection: value(1)) {
                        ForEach(TimeCardSMACatalog.functions(for: direction)) { Text($0.label).tag($0.id) }
                        if !TimeCardSMACatalog.functions(for: direction).contains(where: { $0.id == setting.values[1] }) {
                            Text(String(format: "Current 0x%04x", setting.values[1])).tag(setting.values[1])
                        }
                    }.labelsHidden()
                }
            }
            Spacer()
        }
    }
}
