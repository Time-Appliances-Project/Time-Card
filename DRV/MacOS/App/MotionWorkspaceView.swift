/* SPDX-License-Identifier: BSD-3-Clause */
import Charts
import SwiftUI
import simd

struct MotionWorkspaceView: View {
    @EnvironmentObject private var lab: DeviceLabMonitor
    @State private var exportMessage = ""
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Label("Motion laboratory", systemImage: "gyroscope").font(.title2.bold())
                Spacer()
                Button("Start Motion", systemImage: "play.fill") { lab.startMotion() }
                    .buttonStyle(.borderedProminent).disabled(!lab.supportsMotion || lab.motionRunning || lab.motionBusy)
                Button("Stop", systemImage: "stop.fill") { lab.stopMotion() }
                    .disabled(!lab.motionRunning || lab.motionBusy)
                Menu("Export") {
                    Button("Motion CSV") { exportMessage = DeviceLabExport.csv(MotionStatistics.csv(lab.motionSamples), name: "TimeCard-motion.csv") }
                    Button("Motion JSON") { exportMessage = DeviceLabExport.json(lab.motionSamples, name: "TimeCard-motion.json") }
                }.disabled(lab.motionSamples.isEmpty)
            }
            Text(lab.supportsMotion ? lab.motionMessage : "Fused motion requires active driver 26 (ABI v11) and a supported Meta or Celestica sensor route.")
                .font(.callout).foregroundStyle(.secondary).textSelection(.enabled)
            TimelineView(.periodic(from: .now, by: 0.25)) { context in
                let orientation = lab.motionSamples.last { $0.quaternion != nil }
                let fresh = lab.motionRunning && orientation?.isFresh(at: context.date) == true
                let rotation = fresh ? orientation?.quaternion : nil
                HStack(alignment: .top, spacing: 16) {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Label("3D orientation", systemImage: "cube.transparent").font(.headline)
                            Spacer()
                            Text(fresh ? "LIVE" : "NO FRESH ORIENTATION")
                                .font(.caption.bold()).foregroundStyle(fresh ? .green : .orange)
                        }
                        MotionBoardScene(quaternion: rotation).frame(minHeight: 240, idealHeight: 290)
                        Text("Sensor-frame schematic, not a calibrated chassis mounting model. Drag to orbit the camera; double-click to reset.")
                            .font(.caption).foregroundStyle(.secondary)
                    }.frame(maxWidth: .infinity)
                    VStack(spacing: 10) {
                        let angles = rotation?.eulerDegrees
                        PeripheralMetric(title: "ROLL / PITCH / YAW", value: angles.map { String(format: "%.1f° / %.1f° / %.1f°", $0.x, $0.y, $0.z) } ?? "Unavailable", detail: "Fused quaternion, sensor coordinate frame", color: .cyan)
                        PeripheralMetric(title: "FUSION ACCURACY", value: fresh ? ["Unreliable", "Low", "Medium", "High"][Int(orientation!.fusionAccuracy)] : "Unavailable", detail: "Sensor-reported calibration, not a mounting calibration", color: .purple)
                        let linear = lab.motionSamples.last { $0.linearAcceleration != nil }
                        let liveLinear = lab.motionRunning && linear?.isFresh(at: context.date) == true ? linear?.linearAcceleration : nil
                        PeripheralMetric(title: "LINEAR ACCELERATION", value: liveLinear.map { String(format: "%.3f m/s²", $0.magnitude) } ?? "Unavailable", detail: liveLinear.map { String(format: "X %.3f   Y %.3f   Z %.3f", $0.x, $0.y, $0.z) } ?? "Gravity-compensated sensor report required", color: .orange)
                    }.frame(width: 310)
                }
                MotionVibrationPanel(now: context.date)
            }
            Text("BNO08x requests 4 Hz reports; host polling and bus traffic can reduce the effective rate. This low-rate motion trend is not a calibrated vibration analyzer and cannot resolve high-frequency vibration. Sensor subscriptions are volatile. Time Card clock settings are unchanged.")
                .font(.caption).foregroundStyle(.secondary)
            if !exportMessage.isEmpty { Text(exportMessage).font(.caption).textSelection(.enabled) }
        }.padding(20).background(.background.opacity(0.8), in: RoundedRectangle(cornerRadius: 18))
    }
}

struct MotionVibrationPanel: View {
    @EnvironmentObject private var lab: DeviceLabMonitor
    var now: Date = .now
    private struct Point: Identifiable {
        let id: Int; let date: Date; let value: Double; let segment: Int
    }
    private var points: [Point] {
        var previous: Date?; var segment = 0; var result: [Point] = []
        for (index, sample) in lab.motionSamples.enumerated() {
            guard let value = sample.vibration, (0...60).contains(now.timeIntervalSince(sample.timestamp)) else { continue }
            if let previous, sample.timestamp.timeIntervalSince(previous) > 2 { segment += 1 }
            result.append(Point(id: index, date: sample.timestamp, value: value, segment: segment))
            previous = sample.timestamp
        }
        return result
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Label("Vibration trend", systemImage: "waveform.path").font(.headline)
                Spacer()
                Text("60 s RMS: " + (MotionStatistics.rms(lab.motionSamples, now: now).map { String(format: "%.4f m/s²", $0) } ?? "Unavailable"))
                    .monospacedDigit()
            }
            Chart(points) { point in
                LineMark(x: .value("Time", point.date), y: .value("Linear acceleration", point.value), series: .value("Segment", point.segment))
                    .foregroundStyle(.orange).lineStyle(.init(lineWidth: 2))
                PointMark(x: .value("Time", point.date), y: .value("Linear acceleration", point.value)).foregroundStyle(.orange).symbolSize(10)
            }
            .chartXScale(domain: now.addingTimeInterval(-60)...now)
            .chartYScale(domain: 0...max(0.02, (points.map(\.value).max() ?? 0) * 1.15))
            .chartYAxisLabel("m/s², |linear acceleration|")
            .frame(height: 180)
            .overlay { if points.isEmpty { Text("No valid linear-acceleration samples").foregroundStyle(.secondary) } }
            Text("\(points.count) measured samples in the last 60 seconds. Gaps over two seconds are not joined. Missing readings are never filled with zero.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }
}

private struct MotionBoardScene: View {
    let quaternion: MotionQuaternion?
    @State private var azimuth = -0.55
    @State private var elevation = 0.8
    @GestureState private var drag = CGSize.zero
    private let corners: [SIMD3<Double>] = [
        [-1.6,-0.9,-0.08], [1.6,-0.9,-0.08], [1.6,0.9,-0.08], [-1.6,0.9,-0.08],
        [-1.6,-0.9,0.08], [1.6,-0.9,0.08], [1.6,0.9,0.08], [-1.6,0.9,0.08]
    ]
    var body: some View {
        Canvas { context, size in
            let camera = simd_quatd(angle: elevation + Double(drag.height) * 0.008, axis: [1,0,0]) *
                simd_quatd(angle: azimuth + Double(drag.width) * 0.008, axis: [0,0,1])
            let attitude = quaternion?.normalized ?? simd_quatd(angle: 0, axis: [0,0,1])
            func transform(_ p: SIMD3<Double>) -> SIMD3<Double> { camera.act(attitude.act(p)) }
            func project(_ p: SIMD3<Double>) -> CGPoint {
                let scale = min(size.width / 5.4, size.height / 3.8) * 5 / (5 + p.z)
                return CGPoint(x: size.width / 2 + p.x * scale, y: size.height / 2 - p.y * scale)
            }
            let vertices = corners.map(transform)
            let faces = [[0,1,2,3], [4,7,6,5], [0,4,5,1], [1,5,6,2], [2,6,7,3], [3,7,4,0]]
                .sorted { lhs, rhs in lhs.reduce(0.0) { $0 + vertices[$1].z } > rhs.reduce(0.0) { $0 + vertices[$1].z } }
            for face in faces {
                var path = Path(); path.move(to: project(vertices[face[0]]))
                for index in face.dropFirst() { path.addLine(to: project(vertices[index])) }; path.closeSubpath()
                context.fill(path, with: .color(quaternion == nil ? .gray.opacity(0.15) : .teal.opacity(face.contains(4) ? 0.85 : 0.5)))
                context.stroke(path, with: .color(quaternion == nil ? .gray : .cyan), lineWidth: 1.5)
            }
            for (axis, color, label) in [(SIMD3<Double>(2.0,0,0), Color.red, "X"), (SIMD3<Double>(0,1.5,0), .green, "Y"), (SIMD3<Double>(0,0,1.4), .blue, "Z")] {
                let end = project(transform(axis))
                var path = Path(); path.move(to: project(transform(.zero))); path.addLine(to: end)
                context.stroke(path, with: .color(color), lineWidth: 2)
                context.draw(Text(label).font(.caption.bold()).foregroundColor(color), at: CGPoint(x: end.x + 9, y: end.y - 9))
            }
            context.draw(Text(quaternion == nil ? "ORIENTATION UNAVAILABLE" : "TIME CARD • IMU").font(.caption.bold()).foregroundColor(.secondary), at: CGPoint(x: size.width / 2, y: size.height - 16))
        }
        .background(.black.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
        .contentShape(Rectangle())
        .gesture(DragGesture().updating($drag) { value, state, _ in state = value.translation }
            .onEnded { azimuth += Double($0.translation.width) * 0.008; elevation += Double($0.translation.height) * 0.008 })
        .onTapGesture(count: 2) { azimuth = -0.55; elevation = 0.8 }
        .accessibilityLabel(quaternion == nil ? "No fresh orientation available" : "Live three dimensional IMU orientation")
    }
}
