/* SPDX-License-Identifier: BSD-3-Clause */

import SwiftUI

@main
struct TimeCardMacOSApp: App {
    @StateObject private var driverManager = DriverManager()
    @StateObject private var monitor = TimeCardMonitor()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(driverManager)
                .environmentObject(monitor)
                .frame(minWidth: 920, minHeight: 640)
        }
        .defaultSize(width: 1120, height: 760)
    }
}
