/* SPDX-License-Identifier: BSD-3-Clause */
import AppKit
import SwiftUI
import UniformTypeIdentifiers

struct TimeSyncView: View {
    @EnvironmentObject private var monitor: TimeCardMonitor
    @EnvironmentObject private var timeSync: TimeSyncMonitor
    @EnvironmentObject private var deviceLab: DeviceLabMonitor
    @State private var port: TimeCardUARTPort = .gnss
    @State private var baud: UInt32 = 115_200
    @State private var sendPolls = true
    @State private var exportMessage = ""
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                ControlCenterPanel(title: "Time synchronization", subtitle: "Receiver time qualification and system-clock safety") {
                    Label("Observe only, system-clock steering is not enabled", systemImage: "lock.shield")
                        .font(.headline).foregroundStyle(.orange)
                    Text("Frequency lock, a plausible date, and valid UTC metadata are different things. This workspace checks receiver UTC, GPS, and leap-second evidence. It never treats UART arrival time as a precision timestamp or copies the Mac's clock back into a supposedly independent reference.")
                        .foregroundStyle(.secondary)
                    HStack(spacing: 28) {
                        metric("PPS LOCK", monitor.snapshot?.clockInSync == true ? "In sync" : "Not confirmed")
                        metric("PHC UTC METADATA", monitor.snapshot?.utcOffsetValid == true ? "Valid bit set" : "Not valid")
                        metric("RECEIVER TIME", timeSync.assessment.qualified ? "Consistent" : "Unqualified")
                        metric("SYNC CLOCK WRITES", "0")
                    }
                }
                ControlCenterPanel(title: "Live receiver session", subtitle: "Continuous, bounded-memory GNSS acquisition; reception freshness includes sleep") {
                    HStack {
                        Picker("Port", selection: $port) { Text("GNSS").tag(TimeCardUARTPort.gnss); Text("GNSS 2").tag(TimeCardUARTPort.gnss2) }
                            .frame(width: 180)
                        Picker("Host UART baud", selection: $baud) {
                            ForEach([9_600, 38_400, 57_600, 115_200, 230_400] as [UInt32], id: \.self) { Text(String($0)).tag($0) }
                        }.frame(width: 240)
                        Toggle("Read-only UBX polls", isOn: $sendPolls)
                    }.disabled(timeSync.running)
                    HStack {
                        Button("Start Session") { timeSync.start(port: port, baud: baud, poll: sendPolls) }
                            .buttonStyle(.borderedProminent)
                            .disabled(timeSync.running || timeSync.stopping || monitor.state != .connected || monitor.timeReferenceSessionInProgress || monitor.nativeSerialSessionInProgress)
                        Button(timeSync.stopping ? "Stopping…" : "Stop") { timeSync.stop() }.disabled(!timeSync.running || timeSync.stopping)
                        Spacer()
                        Text("RX \(timeSync.receivedBytes) B · TX polls \(timeSync.transmittedBytes) B · bad checksums \(timeSync.badFrames)")
                            .font(.caption.monospaced()).foregroundStyle(.secondary)
                    }
                    if monitor.nativeSerialSessionInProgress { Text("Disconnect the native serial session in UART and NMEA before starting receiver-time qualification.").foregroundStyle(.orange) }
                    Text(timeSync.message).textSelection(.enabled)
                    Text("Start configures only the selected host UART baud. Polls request MON-VER, NAV-TIMEGPS, NAV-TIMEUTC and NAV-TIMELS; they do not persist receiver settings. Other hardware operations in this app are blocked while the session owns the UART. Close external card-control clients. Sessions stop on card change or after 24 hours; only the latest 200 messages are retained.")
                        .font(.caption).foregroundStyle(.secondary)
                }
                ControlCenterPanel(title: "UTC and TAI evidence", subtitle: "Exact integer epochs, dynamic leap offset, no hard-coded current leap seconds") {
                    row("Receiver UTC solution", timeSync.assessment.utc?.utcText ?? "Unavailable")
                    row("TAI-scale epoch", timeSync.assessment.tai?.text ?? "Unavailable")
                    row("GPS minus UTC", timeSync.assessment.gpsMinusUTC.map { "\($0) s" } ?? "Unavailable")
                    row("TAI minus UTC", timeSync.assessment.taiMinusUTC.map { "\($0) s" } ?? "Unavailable")
                    row("Receiver-reported accuracy", timeSync.assessment.accuracyNanoseconds.map { "\($0) ns, excludes UART latency" } ?? "Unavailable")
                    if timeSync.assessment.qualified {
                        Text("These are receiver navigation-epoch values, not the current time extrapolated from packet arrival. Precision PHC association still requires a validated timing path.")
                            .foregroundStyle(.orange)
                    }
                    ForEach(timeSync.assessment.reasons, id: \.self) { reason in Label(reason, systemImage: "exclamationmark.circle").font(.caption).foregroundStyle(.orange) }
                }
                ControlCenterPanel(title: "System synchronization prerequisites", subtitle: "Every stage must qualify before a privileged clock writer can be enabled") {
                    row("Receiver reference", timeSync.assessment.qualified ? "UTC/GPS/leap evidence agrees" : "Blocked: receiver reference unqualified")
                    row("PHC epoch and time scale", "Blocked: driver ABI v12 has no validated UTC/TAI provenance or GNSS epoch-association record")
                    row("SA53 PPS reference", deviceLab.atomic?.values["PpsInDetected"] == "1" ? "Detected in the last SA53 snapshot; not a fresh timing association" : "Not freshly confirmed; inspect Atomic Clock")
                    row("macOS clock ownership", timeSync.operatingSystemTimeService)
                    row("Privileged clock writer", "Not installed; no adjtime or settimeofday calls are made")
                    row("Tested controller policy", "Not connected to clocks: five stable samples, 100 ms maximum offset, 250 µs/s slew cap")
                    Button("Check macOS Time Service") { timeSync.refreshTimeServiceStatus() }
                    Text("The tested controller rejects stale samples, large phase discontinuities, duplicate samples, card changes and competing clock owners. Real system-clock discipline remains blocked until the PHC epoch is independently qualified and exclusive ownership is explicitly established. No automatic clock steps or network-time disabling are performed.")
                        .font(.caption).foregroundStyle(.secondary)
                }
                ControlCenterPanel(title: "Receiver evidence log", subtitle: "Host receive times label observations, not GNSS epoch accuracy") {
                    HStack { Text("\(timeSync.events.count) retained messages; \(timeSync.discardedBytes) non-UBX/unsynchronized bytes").font(.caption); Spacer(); Button("Export Evidence…") { export() } }
                    if timeSync.events.isEmpty { Text("No complete UBX frames received.").foregroundStyle(.secondary) }
                    ForEach(timeSync.events.suffix(30).reversed()) { event in
                        HStack(alignment: .top) {
                            Image(systemName: event.checksumValid ? "checkmark.circle" : "xmark.circle").foregroundStyle(event.checksumValid ? .green : .orange)
                            Text(event.receivedAt, style: .time).font(.caption.monospaced()).frame(width: 90, alignment: .leading)
                            Text(event.message).font(.caption.monospaced()).textSelection(.enabled)
                        }
                    }
                    if !exportMessage.isEmpty { Text(exportMessage).font(.caption).foregroundStyle(.secondary) }
                }
            }.padding(24)
        }
        .task { timeSync.refreshTimeServiceStatus() }
    }
    private func metric(_ title: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 6) { Text(title).font(.caption).foregroundStyle(.secondary); Text(value).font(.title3.monospacedDigit()) }
    }
    private func row(_ title: String, _ value: String) -> some View {
        HStack(alignment: .top) { Text(title).foregroundStyle(.secondary).frame(width: 210, alignment: .leading); Text(value).textSelection(.enabled); Spacer() }
    }
    private func export() {
        let panel = NSSavePanel(); panel.allowedContentTypes = [.json]; panel.nameFieldStringValue = "timecard-time-evidence.json"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do { try timeSync.export(snapshot: monitor.snapshot).write(to: url, options: .atomic); exportMessage = "Saved " + url.lastPathComponent }
        catch { exportMessage = error.localizedDescription }
    }
}
