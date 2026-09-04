/* SPDX-License-Identifier: BSD-3-Clause */

import Foundation

@main
enum TimeCardClientTests {
    static func main() {
        precondition(
            TimeCardClient.localABILayoutIsValid,
            "Swift structures must preserve the DriverKit ABI v6 layout"
        )

        let snapshot = TimeCardDeviceSnapshot(
            service: TimeCardServiceDescriptor(id: 0x1234),
            abiVersion: 2,
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
            abiVersion: 2,
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

        print("Time Card Control Center model tests passed")
    }
}
