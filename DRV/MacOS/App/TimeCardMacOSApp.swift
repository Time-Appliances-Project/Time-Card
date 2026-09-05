/* SPDX-License-Identifier: BSD-3-Clause */

import SwiftUI
import AppKit
import SystemExtensions

private final class ActivationRequestDelegate: NSObject,
    OSSystemExtensionRequestDelegate {
    private let driverIdentifier: String
    private var done = false
    private var exitStatus: Int32 = 1

    init(driverIdentifier: String) {
        self.driverIdentifier = driverIdentifier
    }

    func activate(timeoutSeconds: TimeInterval = 60) -> Int32 {
        let request = OSSystemExtensionRequest.activationRequest(
            forExtensionWithIdentifier: driverIdentifier,
            queue: .main
        )
        request.delegate = self
        fputs("TimeCardMacOS: requesting driver activation\n", stderr)
        OSSystemExtensionManager.shared.submitRequest(request)

        let deadline = Date().addingTimeInterval(timeoutSeconds)
        while !done && Date() < deadline {
            RunLoop.main.run(mode: .default, before: Date().addingTimeInterval(0.1))
        }
        if done {
            return exitStatus
        }
        fputs("TimeCardMacOS: activation request timed out\n", stderr)
        return 1
    }

    func request(
        _ request: OSSystemExtensionRequest,
        actionForReplacingExtension existing: OSSystemExtensionProperties,
        withExtension replacement: OSSystemExtensionProperties
    ) -> OSSystemExtensionRequest.ReplacementAction {
        fputs(
            "TimeCardMacOS: replacing driver " +
                "\(existing.bundleShortVersion)/\(existing.bundleVersion) " +
                "with \(replacement.bundleShortVersion)/" +
                "\(replacement.bundleVersion)\n",
            stderr
        )
        return .replace
    }

    func requestNeedsUserApproval(
        _ request: OSSystemExtensionRequest
    ) {
        fputs("TimeCardMacOS: approval required in System Settings\n", stderr)
    }

    func request(
        _ request: OSSystemExtensionRequest,
        didFinishWithResult result: OSSystemExtensionRequest.Result
    ) {
        fputs("TimeCardMacOS: activation result \(result.rawValue)\n", stderr)
        exitStatus = 0
        done = true
    }

    func request(
        _ request: OSSystemExtensionRequest,
        didFailWithError error: Error
    ) {
        fputs("TimeCardMacOS: activation failed: \(error)\n", stderr)
        exitStatus = 1
        done = true
    }
}

@MainActor private final class TimeCardApplicationDelegate: NSObject, NSApplicationDelegate {
    weak var serialSession: SerialSessionController?
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let serialSession, serialSession.active else { return .terminateNow }
        serialSession.disconnect(reason: "Application is quitting.")
        guard serialSession.active else { return .terminateNow }
        Task {
            while serialSession.active { try? await Task.sleep(for: .milliseconds(10)) }
            sender.reply(toApplicationShouldTerminate: true)
        }
        return .terminateLater
    }
}

@main
struct TimeCardMacOSApp: App {
    @NSApplicationDelegateAdaptor(TimeCardApplicationDelegate.self) private var applicationDelegate
    @StateObject private var driverManager = DriverManager()
    @StateObject private var monitor = TimeCardMonitor()
    @StateObject private var serialSession = SerialSessionController()
    @State private var handledLaunchArguments = false
    @State private var showingSplash = !ProcessInfo.processInfo.arguments.contains("--activate-driver")
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    init() {
        if ProcessInfo.processInfo.arguments.contains("--activate-driver") &&
            ProcessInfo.processInfo.environment["TIMECARD_HEADLESS_ACTIVATE"] == "1" {
            let delegate = ActivationRequestDelegate(
                driverIdentifier: DriverManager.driverIdentifier
            )
            exit(delegate.activate())
        }
    }

    var body: some Scene {
        Window("Time Card Control Center", id: "control-center") {
            ZStack {
                if showingSplash {
                    TimeCardLaunchSplash(monitor: monitor, onContinue: dismissSplash)
                        .transition(.opacity)
                } else {
                    ContentView()
                }
            }
                .environmentObject(driverManager)
                .environmentObject(monitor)
                .environmentObject(serialSession)
                .frame(minWidth: 920, minHeight: 640)
                .onReceive(NSWorkspace.shared.notificationCenter.publisher(for: NSWorkspace.willSleepNotification)) { _ in
                    serialSession.disconnect(reason: "Mac is sleeping. Reconnect explicitly after wake.")
                }
                .task {
                    applicationDelegate.serialSession = serialSession
                    guard !handledLaunchArguments else { return }
                    handledLaunchArguments = true
                    if ProcessInfo.processInfo.arguments
                        .contains("--activate-driver") {
                        driverManager.activate()
                        await reportActivationProgress()
                    } else {
                        // Discovery runs independently. Never wait for hardware
                        // or imply a successful timing lock just to dismiss this.
                        try? await Task.sleep(for: .milliseconds(2400))
                        dismissSplash()
                    }
                }
        }
        .defaultSize(width: 1120, height: 760)
        .commands {
            CommandGroup(replacing: .appInfo) { TimeCardAboutButton() }
        }

        Window("About Time Card Control Center", id: "welcome") {
            TimeCardWelcomeWindow().environmentObject(monitor)
        }
        .windowResizability(.contentSize)
        .defaultPosition(.center)

        MenuBarExtra {
            TimeCardStatusMenu().environmentObject(monitor).environmentObject(serialSession)
        } label: {
            Image(nsImage: Self.menuBarIcon)
                .accessibilityLabel("Time Card")
                .help("Time Card Control Center")
        }
        .menuBarExtraStyle(.menu)
    }

    private func dismissSplash() {
        withAnimation(reduceMotion ? nil : .easeOut(duration: 0.25)) { showingSplash = false }
    }

    // A solid card-and-clock silhouette, not the full-color app artwork.
    // Template rendering lets macOS match the other status icons in every
    // menu-bar appearance, including selection and increased contrast.
    private static let menuBarIcon: NSImage = {
        let icon = (NSImage(named: "TimeCardMenuBar")?.copy() as? NSImage)
            ?? NSImage(systemSymbolName: "pcicard", accessibilityDescription: "Time Card")!
        icon.size = NSSize(width: 26, height: 18)
        icon.isTemplate = true
        return icon
    }()

    @MainActor
    private func reportActivationProgress() async {
        var lastStatus = ""
        for _ in 0..<120 {
            if driverManager.status != lastStatus {
                lastStatus = driverManager.status
                FileHandle.standardError.write(
                    Data("TimeCardMacOS: \(lastStatus)\n".utf8)
                )
            }
            if !driverManager.requestInProgress &&
                !lastStatus.hasPrefix("Requesting ") {
                NSApplication.shared.terminate(nil)
                return
            }
            try? await Task.sleep(for: .milliseconds(500))
        }
        FileHandle.standardError.write(
            Data("TimeCardMacOS: activation request timed out\n".utf8)
        )
        NSApplication.shared.terminate(nil)
    }
}

private struct TimeCardLaunchSplash: View {
    @ObservedObject var monitor: TimeCardMonitor
    let onContinue: () -> Void
    @Environment(\.colorScheme) private var colorScheme

    var body: some View {
        ZStack {
            Color(nsColor: .windowBackgroundColor)
            RadialGradient(colors: [.blue.opacity(colorScheme == .dark ? 0.2 : 0.09), .clear],
                center: .center, startRadius: 20, endRadius: 380)
            VStack(spacing: 0) {
                Spacer()
                Image("TimeCardArtwork")
                    .resizable().interpolation(.high).scaledToFit()
                    .frame(width: 240, height: 210)
                    .shadow(color: .blue.opacity(0.12), radius: 24, y: 8)
                    .accessibilityHidden(true)
                Text("TIME CARD")
                    .font(.system(size: 11, weight: .semibold)).tracking(4)
                    .foregroundStyle(.secondary).padding(.top, 4)
                Text("Control Center")
                    .font(.system(size: 36, weight: .semibold, design: .rounded))
                    .padding(.top, 10)
                Text("Precision timing. Native to your Mac.")
                    .font(.system(size: 14)).foregroundStyle(.secondary).padding(.top, 12)
                Text("Version \(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "") · Build \(Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "")")
                    .font(.caption.monospaced()).foregroundStyle(.tertiary).padding(.top, 8)
                HStack(spacing: 8) {
                    Circle().fill(monitor.state == .connected ? Color.green : .secondary)
                        .frame(width: 6, height: 6)
                    Text(connectionText).font(.system(size: 12))
                }
                .padding(.horizontal, 16).padding(.vertical, 10)
                .background(.quaternary.opacity(0.5), in: Capsule())
                .padding(.top, 28)
                Spacer()
                HStack {
                    Text("Open Compute Project · Time Appliances")
                        .font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button("Continue", action: onContinue).buttonStyle(.plain)
                        .font(.caption).foregroundStyle(.secondary)
                        .keyboardShortcut(.escape, modifiers: [])
                }.padding(24)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Time Card Control Center startup")
    }

    private var connectionText: String {
        switch monitor.state {
        case .connected: "Connected to " + (monitor.snapshot?.boardName ?? "Time Card")
        case .discovering: "Looking for your Time Card…"
        case .noService: "Ready. Connect a Time Card to begin."
        case .accessUnavailable: "Ready. Driver access needs attention."
        case .failed: "Ready. Card communication needs attention."
        }
    }
}

private struct TimeCardWelcomeWindow: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        TimeCardLaunchSplash(monitor: monitor, onContinue: { dismiss() })
            .frame(width: 560, height: 550)
    }
}

private struct TimeCardAboutButton: View {
    @Environment(\.openWindow) private var openWindow
    var body: some View {
        Button("About Time Card Control Center") {
            openWindow(id: "welcome")
            NSApp.activate(ignoringOtherApps: true)
        }
    }
}

private struct TimeCardStatusMenu: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @EnvironmentObject private var serialSession: SerialSessionController
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Text("Time Card Control Center")
        Divider()
        if monitor.state == .connected, let snapshot = monitor.snapshot {
            Text(snapshot.boardName)
            if let updated = monitor.lastUpdated, Date().timeIntervalSince(updated) <= 5 {
                Text(snapshot.clockInSync == true ? "Clock core: in sync" : "Clock core: not synchronized")
                Text(snapshot.utcOffsetValid == true ? "UTC offset valid; epoch not qualified" : "UTC reference: not valid / unavailable")
            } else {
                Text("Telemetry stale. Refresh before relying on status.")
            }
            if let updated = monitor.lastUpdated {
                Text("Updated " + updated.formatted(date: .omitted, time: .standard))
            }
        } else {
            Text(connectionStatus)
        }
        Text("macOS clock discipline is not enabled")
        if serialSession.active {
            Divider()
            Text("Serial: \(serialSession.record?.port ?? "opening")")
            Button("Disconnect Serial Session") { serialSession.disconnect() }
        }
        Divider()
        Button("Open Control Center") {
            openWindow(id: "control-center")
            NSApp.activate(ignoringOtherApps: true)
            for window in NSApp.windows where window.canBecomeMain && window.isMiniaturized {
                window.deminiaturize(nil)
            }
        }
        Button("Refresh Status") { monitor.refresh() }
        TimeCardAboutButton()
        Divider()
        Button("Quit Time Card Control Center") { NSApp.terminate(nil) }
    }

    private var connectionStatus: String {
        switch monitor.state {
        case .discovering: "Looking for a Time Card…"
        case .noService: "No Time Card connected"
        case .accessUnavailable: "Driver access unavailable"
        case .failed: "Card communication failed"
        case .connected: "Waiting for telemetry…"
        }
    }
}
