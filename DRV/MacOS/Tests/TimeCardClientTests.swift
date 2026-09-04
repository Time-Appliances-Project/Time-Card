/* SPDX-License-Identifier: BSD-3-Clause */

import Foundation

@main
enum TimeCardClientTests {
    static func main() {
        precondition(
            TimeCardClient.localABILayoutIsValid,
            "Swift structures must preserve the DriverKit ABI v7 layout"
        )

        let snapshot = TimeCardDeviceSnapshot(
            service: TimeCardServiceDescriptor(id: 0x1234),
            abiVersion: 7,
            driverVersion: 2,
            vendorID: 0x1d9b,
            deviceID: 0x0400,
            pciRevision: 0,
            boardProfile: 1,
            layout: 1,
            advertisedMSIXVectors: 0,
            barSize: 0x0200_0000,
            clockOffset: 0x0100_0000,
            todOffset: 0x0105_0000,
            capabilities: 0xf,
            validFields: 0x1f,
            clockVersion: 0x0102_0000,
            clockStatus: 1,
            clockSelect: 0xabcd_00fe,
            todVersion: 0x0102_0000,
            todStatus: 0,
            cardSeconds: 1_800_000_000,
            cardNanoseconds: 123_456_789,
            systemTimeBeforeNanoseconds: 10_000,
            systemTimeAfterNanoseconds: 10_900
        )

        precondition(snapshot.boardName == "Meta/Facebook Time Card")
        precondition(snapshot.pciIdentity == "1d9b:0400")
        precondition(snapshot.layoutName == "Classic")
        precondition(snapshot.driverVersionText == "0.2")
        precondition(snapshot.sampleWindowNanoseconds == 900)
        precondition(snapshot.systemMidpointNanoseconds == 10_450)
        precondition(snapshot.clockInSync == true)
        precondition(snapshot.configuredClockSource == 0xfe)
        precondition(snapshot.capabilityNames == [
            "Clock read", "Clock set", "Cross timestamp", "ToD",
        ])

        let art = TimeCardDeviceSnapshot(
            service: TimeCardServiceDescriptor(id: 0x5678),
            abiVersion: 7,
            driverVersion: 2,
            vendorID: 0x1ad7,
            deviceID: 0xa000,
            pciRevision: 0,
            boardProfile: 3,
            layout: 3,
            advertisedMSIXVectors: 0,
            barSize: 0x0200_0000,
            clockOffset: 0x0100_0000,
            todOffset: 0,
            capabilities: 0x7,
            validFields: 0x1,
            clockVersion: 0x0100_0000,
            clockStatus: 0,
            clockSelect: 0,
            todVersion: 0,
            todStatus: 0,
            cardSeconds: 0,
            cardNanoseconds: 0,
            systemTimeBeforeNanoseconds: 20_000,
            systemTimeAfterNanoseconds: 20_100
        )
        precondition(art.boardName == "Orolia/Safran ART")
        precondition(art.layoutName == "ART")
        precondition(art.clockInSync == nil)
        precondition(!art.capabilityNames.contains("ToD"))

        let temperatureFlags: UInt32 = (1 << 0) | (1 << 1) |
            (1 << 3) | (1 << 7)
        let humidityFlags: UInt32 = temperatureFlags | (1 << 2) |
            (1 << 6) | (1 << 9)
        let pressureFlags: UInt32 = temperatureFlags | (1 << 2) |
            (1 << 9) | (1 << 10) | (1 << 11)
        let imuFlags: UInt32 = (1 << 0) | (1 << 12)
        let humidity = TimeCardSensorReading(
            kind: .sht3x,
            flags: humidityFlags,
            muxChannelMask: 2,
            address: 0x44,
            temperatureMilliCelsius: 34_200,
            humidityMilliPercent: 42_800,
            pressureRaw: 0,
            raw0: 29_657,
            raw1: 28_049,
            raw2: 0
        )
        let pressure = TimeCardSensorReading(
            kind: .icp10100,
            flags: pressureFlags,
            muxChannelMask: 4,
            address: 0x63,
            temperatureMilliCelsius: 32_000,
            humidityMilliPercent: 0,
            pressureRaw: 11_477_003,
            raw0: 11_477_003,
            raw1: 28_836,
            raw2: 0x08
        )
        let imu = TimeCardSensorReading(
            kind: .bno08x,
            flags: imuFlags,
            muxChannelMask: 8,
            address: 0x4a,
            temperatureMilliCelsius: 0,
            humidityMilliPercent: 0,
            pressureRaw: 0,
            raw0: 0,
            raw1: 0xc0,
            raw2: 0xd0
        )
        let sensors = TimeCardSensorSnapshot(
            flags: 3,
            boardProfile: 2,
            capabilities: (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6),
            muxChannelMask: 0,
            restoredMuxChannelMask: 0,
            controllerStatus: 0xc0,
            interruptStatus: 0xd0,
            icp10100Otp: [10_000, 20_000, 30_000, 4_000],
            readings: [humidity, pressure, imu]
        )
        precondition(sensors.isPresent)
        precondition(sensors.isValid)
        precondition(sensors.muxWasRestored)
        precondition(sensors.validReadings.count == 2)
        precondition(sensors.capabilityNames == [
            "BNO08x", "LM75B", "SHT3x", "ICP-10100",
        ])
        precondition(abs((sensors.pressurePascals ?? 0) - 101_324.9985) < 0.01)
        precondition(abs((sensors.dewPointCelsius ?? 0) - 19.758) < 0.01)
        precondition(imu.isIMU)
        precondition(imu.isPresent)
        precondition(!imu.isValid)

        print("Time Card Control Center model tests passed")
    }
}
