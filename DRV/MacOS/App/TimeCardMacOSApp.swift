/* SPDX-License-Identifier: BSD-3-Clause */

import SwiftUI

@main
struct TimeCardMacOSApp: App {
    @StateObject private var driverManager = DriverManager()
    @StateObject private var monitor = TimeCardMonitor()
    @State private var handledLaunchArguments = false

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
                    }
                }
        }
        .defaultSize(width: 1120, height: 760)
    }
}
