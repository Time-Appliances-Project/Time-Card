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

@main
struct TimeCardMacOSApp: App {
    @StateObject private var driverManager = DriverManager()
    @StateObject private var monitor = TimeCardMonitor()
    @State private var handledLaunchArguments = false

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
        WindowGroup {
            ContentView()
                .environmentObject(driverManager)
                .environmentObject(monitor)
                .frame(minWidth: 920, minHeight: 640)
                .task {
                    guard !handledLaunchArguments else { return }
                    handledLaunchArguments = true
                    if ProcessInfo.processInfo.arguments
                        .contains("--activate-driver") {
                        driverManager.activate()
                        await reportActivationProgress()
                    }
                }
        }
        .defaultSize(width: 1120, height: 760)
    }

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
