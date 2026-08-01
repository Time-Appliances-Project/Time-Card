/* SPDX-License-Identifier: BSD-3-Clause */

import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var driverManager: DriverManager

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Label("OCP Time Card", systemImage: "clock.badge.checkmark")
                .font(.largeTitle)
            Text(driverManager.status)
                .foregroundStyle(.secondary)

            HStack {
                Button("Install or Update Driver") {
                    driverManager.activate()
                }
                .buttonStyle(.borderedProminent)

                Button("Remove Driver") {
                    driverManager.deactivate()
                }
                .buttonStyle(.bordered)
            }
            .disabled(driverManager.requestInProgress)

            Divider()
            Text("The first milestone provides PCI detection, clock status, " +
                 "PHC read and set operations, and bracketed cross timestamps.")
                .font(.callout)
                .foregroundStyle(.secondary)
        }
        .padding(28)
        .frame(minWidth: 560, minHeight: 260)
    }
}
