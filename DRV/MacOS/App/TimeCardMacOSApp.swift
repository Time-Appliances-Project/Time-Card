/* SPDX-License-Identifier: BSD-3-Clause */

import SwiftUI

@main
struct TimeCardMacOSApp: App {
    @StateObject private var driverManager = DriverManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(driverManager)
        }
    }
}
