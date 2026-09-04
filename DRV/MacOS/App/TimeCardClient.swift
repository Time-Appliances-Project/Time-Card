/* SPDX-License-Identifier: BSD-3-Clause */

import CoreFoundation
import Foundation
import IOKit

struct TimeCardServiceDescriptor: Identifiable, Hashable, Sendable {
    let id: UInt64

    var displayName: String {
        String(format: "Time Card %016llX", id)
    }
}

struct TimeCardDeviceSnapshot: Equatable, Sendable {
    let service: TimeCardServiceDescriptor
    let abiVersion: UInt32
    let driverVersion: UInt32
    let vendorID: UInt16
    let deviceID: UInt16
    let pciRevision: UInt32
    let boardProfile: UInt32
    let layout: UInt32
    let advertisedMSIXVectors: UInt32
    let barSize: UInt64
    let clockOffset: UInt64
    let todOffset: UInt64
    let capabilities: UInt32
    let validFields: UInt64
    let clockVersion: UInt32
    let clockStatus: UInt32
    let clockSelect: UInt32
    let todVersion: UInt32
    let todStatus: UInt32
    let cardSeconds: UInt64
    let cardNanoseconds: UInt32
    let systemTimeBeforeNanoseconds: UInt64
    let systemTimeAfterNanoseconds: UInt64

    var boardName: String {
        switch boardProfile {
        case 1: "Meta/Facebook Time Card"
        case 2: "Celestica R4006"
        case 3: "Orolia/Safran ART"
        case 4: "ADVA Time Card"
        case 5: "ADVA Time Card X1"
        default: "Unknown Time Card"
        }
    }

    var pciIdentity: String {
        String(format: "%04x:%04x", vendorID, deviceID)
    }

    var driverVersionText: String {
        "\(driverVersion >> 16).\(driverVersion & 0xffff)"
    }

    var layoutName: String {
        switch layout {
        case 1: "Classic"
        case 2: "Shifted LitePCIe"
        case 3: "ART"
        default: "Unknown"
        }
    }

    var sampleWindowNanoseconds: UInt64 {
        guard systemTimeAfterNanoseconds >= systemTimeBeforeNanoseconds else {
            return 0
        }
        return systemTimeAfterNanoseconds - systemTimeBeforeNanoseconds
    }

    var systemMidpointNanoseconds: UInt64 {
        guard systemTimeAfterNanoseconds >= systemTimeBeforeNanoseconds else {
            return systemTimeBeforeNanoseconds
        }
        return systemTimeBeforeNanoseconds
            + (systemTimeAfterNanoseconds - systemTimeBeforeNanoseconds) / 2
    }

    var clockInSync: Bool? {
        guard hasValidField(1 << 1) else { return nil }
        return (clockStatus & 1) != 0
    }

    var configuredClockSource: UInt32? {
        guard hasValidField(1 << 2) else { return nil }
        return clockSelect & 0xff
    }

    var capabilityNames: [String] {
        var names: [String] = []
        if (capabilities & (1 << 0)) != 0 { names.append("Clock read") }
        if (capabilities & (1 << 1)) != 0 { names.append("Clock set") }
        if (capabilities & (1 << 2)) != 0 { names.append("Cross timestamp") }
        if (capabilities & (1 << 3)) != 0 { names.append("ToD") }
        if (capabilities & (1 << 4)) != 0 { names.append("SMA") }
        return names
    }

    func hasValidField(_ field: UInt64) -> Bool {
        (validFields & field) != 0
    }
}

enum TimeCardSMADirection: UInt32, CaseIterable, Identifiable, Sendable {
    case input = 0
    case output = 1
    case disabled = 2

    var id: UInt32 { rawValue }

    var label: String {
        switch self {
        case .input: "Input"
        case .output: "Output"
        case .disabled: "Disabled"
        }
    }
}

struct TimeCardSMAFunction: Identifiable, Hashable, Sendable {
    let id: UInt32
    let label: String
}

struct TimeCardSMARoute: Identifiable, Equatable, Sendable {
    let connector: UInt32
    let direction: TimeCardSMADirection
    let function: UInt32
    let flags: UInt32
    let inputMap: UInt32
    let outputMap: UInt32

    var id: UInt32 { connector }

    var isPresent: Bool { (flags & (1 << 0)) != 0 }
    var isFixedDirection: Bool { (flags & (1 << 1)) != 0 }
    var isDisabled: Bool { (flags & (1 << 2)) != 0 }
    var isFixedFunction: Bool { (flags & (1 << 3)) != 0 }

    var functionName: String {
        TimeCardSMACatalog.name(for: function, direction: direction)
    }
}

enum TimeCardSMACatalog {
    static let inputFunctions: [TimeCardSMAFunction] = [
        .init(id: 0x0000, label: "10 MHz"),
        .init(id: 0x0001, label: "PPS 1"),
        .init(id: 0x0002, label: "PPS 2"),
        .init(id: 0x0004, label: "Timestamp 1"),
        .init(id: 0x0008, label: "Timestamp 2"),
        .init(id: 0x0010, label: "IRIG-B"),
        .init(id: 0x0020, label: "DCF77"),
        .init(id: 0x0040, label: "Timestamp 3"),
        .init(id: 0x0080, label: "Timestamp 4"),
        .init(id: 0x0100, label: "Frequency 1"),
        .init(id: 0x0200, label: "Frequency 2"),
        .init(id: 0x0400, label: "Frequency 3"),
        .init(id: 0x0800, label: "Frequency 4"),
    ]

    static let outputFunctions: [TimeCardSMAFunction] = [
        .init(id: 0x0000, label: "10 MHz"),
        .init(id: 0x0001, label: "PHC"),
        .init(id: 0x0002, label: "MAC"),
        .init(id: 0x0004, label: "GNSS 1"),
        .init(id: 0x0008, label: "GNSS 2"),
        .init(id: 0x0010, label: "IRIG-B"),
        .init(id: 0x0020, label: "DCF77"),
        .init(id: 0x0040, label: "Generator 1"),
        .init(id: 0x0080, label: "Generator 2"),
        .init(id: 0x0100, label: "Generator 3"),
        .init(id: 0x0200, label: "Generator 4"),
        .init(id: 0x2000, label: "Ground"),
        .init(id: 0x4000, label: "VCC"),
    ]

    static func functions(for direction: TimeCardSMADirection)
        -> [TimeCardSMAFunction] {
        switch direction {
        case .input: inputFunctions
        case .output: outputFunctions
        case .disabled: []
        }
    }

    static func name(for value: UInt32, direction: TimeCardSMADirection)
        -> String {
        functions(for: direction).first(where: { $0.id == value })?.label
            ?? String(format: "0x%04x", value)
    }
}

enum TimeCardClientError: Error, Equatable, LocalizedError, Sendable {
    case matchingDictionary
    case discoveryFailed(Int32)
    case serviceNotFound
    case serviceDisappeared
    case openFailed(Int32)
    case methodFailed(selector: UInt32, result: Int32)
    case unexpectedOutput(selector: UInt32, expected: Int, actual: Int)
    case invalidClientLayout
    case incompatibleABI(UInt32)
    case invalidTimestamp

    var errorDescription: String? {
        switch self {
        case .matchingDictionary:
            "Unable to create an IOKit service match."
        case .discoveryFailed(let result):
            "Time Card discovery failed (\(Self.hex(result)))."
        case .serviceNotFound:
            "No active Time Card DriverKit service was found."
        case .serviceDisappeared:
            "The selected Time Card disconnected before it could be opened."
        case .openFailed(let result):
            "The Time Card user client could not be opened (\(Self.hex(result)))."
        case .methodFailed(let selector, let result):
            "Driver method \(selector) failed (\(Self.hex(result)))."
        case .unexpectedOutput(let selector, let expected, let actual):
            "Driver method \(selector) returned \(actual) bytes; expected \(expected)."
        case .invalidClientLayout:
            "The Control Center ABI structure layout is invalid."
        case .incompatibleABI(let version):
            "Driver ABI \(version) is not supported by this Control Center."
        case .invalidTimestamp:
            "The driver returned an invalid cross timestamp."
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .serviceNotFound:
            "Install or update the driver, approve it in System Settings, and restart if requested."
        case .openFailed(let result)
            where result == kIOReturnNotPrivileged
                || result == kIOReturnNotPermitted:
            "The app provisioning profile must include com.apple.developer.driverkit.userclient-access for the Time Card driver."
        case .openFailed:
            "Verify that the selected driver service is active, then retry."
        case .serviceDisappeared:
            "Reconnect the device or wait for the driver to finish restarting."
        case .incompatibleABI:
            "Install the matching Control Center and driver versions together."
        default:
            nil
        }
    }

    private static func hex(_ value: Int32) -> String {
        String(format: "0x%08x", UInt32(bitPattern: value))
    }
}

enum TimeCardClient {
    private static let serviceClass = "IOUserService"
    private static let userClassValue = "TimeCardDriver"
    private static let supportedABIVersion: UInt32 = 3

    static var localABILayoutIsValid: Bool {
        MemoryLayout<TimeCardTimeRaw>.size == 16
            && MemoryLayout<TimeCardTimeRaw>.offset(of: \.seconds) == 0
            && MemoryLayout<TimeCardTimeRaw>.offset(of: \.nanoseconds) == 8
            && MemoryLayout<TimeCardTimeRaw>.offset(of: \.reserved) == 12
            && MemoryLayout<TimeCardCrossTimestampRaw>.size == 32
            && MemoryLayout<TimeCardCrossTimestampRaw>.offset(of: \.cardTime) == 0
            && MemoryLayout<TimeCardCrossTimestampRaw>.offset(
                of: \.systemTimeBeforeNanoseconds
            ) == 16
            && MemoryLayout<TimeCardCrossTimestampRaw>.offset(
                of: \.systemTimeAfterNanoseconds
            ) == 24
            && MemoryLayout<TimeCardInfoRaw>.size == 112
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.abiVersion) == 0
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.driverVersion) == 4
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.vendorID) == 8
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.deviceID) == 10
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.layout) == 12
            && MemoryLayout<TimeCardInfoRaw>.offset(
                of: \.advertisedMSIXVectors
            ) == 16
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.barSize) == 24
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.clockOffset) == 32
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.todOffset) == 40
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.clockVersion) == 48
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.clockStatus) == 52
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.clockSelect) == 56
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.todVersion) == 60
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.todStatus) == 64
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.utcStatus) == 68
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.leap) == 72
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.gnssStatus) == 76
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.satellites) == 80
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.boardProfile) == 84
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.capabilities) == 88
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.validFields) == 96
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.pciRevision) == 104
            && MemoryLayout<TimeCardInfoRaw>.offset(of: \.reserved) == 108
            && MemoryLayout<TimeCardSMARaw>.size == 32
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.size) == 0
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.connector) == 4
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.direction) == 8
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.function) == 12
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.flags) == 16
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.inputMap) == 20
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.outputMap) == 24
            && MemoryLayout<TimeCardSMARaw>.offset(of: \.reserved) == 28
    }

    static func discoverServices() throws -> [TimeCardServiceDescriptor] {
        let userClassKey = "IOUserClass" as CFString
        guard let matching = IOServiceMatching(serviceClass) else {
            throw TimeCardClientError.matchingDictionary
        }

        var iterator: io_iterator_t = 0
        let result = IOServiceGetMatchingServices(
            kIOMainPortDefault, matching, &iterator
        )
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.discoveryFailed(result)
        }
        defer { IOObjectRelease(iterator) }

        var services: [TimeCardServiceDescriptor] = []
        while true {
            let service = IOIteratorNext(iterator)
            guard service != IO_OBJECT_NULL else { break }
            defer { IOObjectRelease(service) }

            guard let property = IORegistryEntryCreateCFProperty(
                service, userClassKey, kCFAllocatorDefault, 0
            )?.takeRetainedValue() as? String,
                property == userClassValue else {
                continue
            }

            var registryID: UInt64 = 0
            guard IORegistryEntryGetRegistryEntryID(service, &registryID)
                == KERN_SUCCESS else {
                continue
            }
            services.append(TimeCardServiceDescriptor(id: registryID))
        }

        return services.sorted { $0.id < $1.id }
    }

    static func readSnapshot(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> TimeCardDeviceSnapshot {
        guard localABILayoutIsValid else {
            throw TimeCardClientError.invalidClientLayout
        }
        guard let matching = IORegistryEntryIDMatching(descriptor.id) else {
            throw TimeCardClientError.matchingDictionary
        }
        let service = IOServiceGetMatchingService(kIOMainPortDefault, matching)
        guard service != IO_OBJECT_NULL else {
            throw TimeCardClientError.serviceDisappeared
        }
        defer { IOObjectRelease(service) }

        var connection: io_connect_t = 0
        let openResult = IOServiceOpen(
            service, mach_task_self_, 0, &connection
        )
        guard openResult == KERN_SUCCESS else {
            throw TimeCardClientError.openFailed(openResult)
        }
        defer { IOServiceClose(connection) }

        var info = TimeCardInfoRaw()
        try callOutput(
            connection: connection, selector: 0, output: &info
        )
        guard info.abiVersion == supportedABIVersion else {
            throw TimeCardClientError.incompatibleABI(info.abiVersion)
        }

        var timestamp = TimeCardCrossTimestampRaw()
        try callOutput(
            connection: connection, selector: 3, output: &timestamp
        )
        guard timestamp.cardTime.nanoseconds < 1_000_000_000,
              timestamp.systemTimeAfterNanoseconds
                >= timestamp.systemTimeBeforeNanoseconds else {
            throw TimeCardClientError.invalidTimestamp
        }

        return TimeCardDeviceSnapshot(
            service: descriptor,
            abiVersion: info.abiVersion,
            driverVersion: info.driverVersion,
            vendorID: info.vendorID,
            deviceID: info.deviceID,
            pciRevision: info.pciRevision,
            boardProfile: info.boardProfile,
            layout: info.layout,
            advertisedMSIXVectors: info.advertisedMSIXVectors,
            barSize: info.barSize,
            clockOffset: info.clockOffset,
            todOffset: info.todOffset,
            capabilities: info.capabilities,
            validFields: info.validFields,
            clockVersion: info.clockVersion,
            clockStatus: info.clockStatus,
            clockSelect: info.clockSelect,
            todVersion: info.todVersion,
            todStatus: info.todStatus,
            cardSeconds: timestamp.cardTime.seconds,
            cardNanoseconds: timestamp.cardTime.nanoseconds,
            systemTimeBeforeNanoseconds:
                timestamp.systemTimeBeforeNanoseconds,
            systemTimeAfterNanoseconds:
                timestamp.systemTimeAfterNanoseconds
        )
    }

    static func setCardFromSystem(
        for descriptor: TimeCardServiceDescriptor
    ) throws {
        guard localABILayoutIsValid else {
            throw TimeCardClientError.invalidClientLayout
        }
        guard let matching = IORegistryEntryIDMatching(descriptor.id) else {
            throw TimeCardClientError.matchingDictionary
        }
        let service = IOServiceGetMatchingService(kIOMainPortDefault, matching)
        guard service != IO_OBJECT_NULL else {
            throw TimeCardClientError.serviceDisappeared
        }
        defer { IOObjectRelease(service) }

        var connection: io_connect_t = 0
        let openResult = IOServiceOpen(
            service, mach_task_self_, 0, &connection
        )
        guard openResult == KERN_SUCCESS else {
            throw TimeCardClientError.openFailed(openResult)
        }
        defer { IOServiceClose(connection) }

        let now = Date().timeIntervalSince1970
        let seconds = floor(now)
        let nanoseconds = (now - seconds) * 1_000_000_000
        var time = TimeCardTimeRaw(
            seconds: UInt64(seconds),
            nanoseconds: UInt32(nanoseconds.rounded()),
            reserved: 0
        )
        try callInput(connection: connection, selector: 2, input: &time)
    }

    static func querySMARoutes(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> [TimeCardSMARoute] {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        return try (1...4).map { connector in
            var raw = TimeCardSMARaw(
                size: UInt32(MemoryLayout<TimeCardSMARaw>.size),
                connector: UInt32(connector)
            )
            try callInOut(connection: connection, selector: 4, value: &raw)
            return raw.route
        }
    }

    static func setSMARoute(
        for descriptor: TimeCardServiceDescriptor,
        connector: UInt32,
        direction: TimeCardSMADirection,
        function: UInt32
    ) throws -> TimeCardSMARoute {
        let connection = try openConnection(for: descriptor)
        defer { IOServiceClose(connection) }

        var raw = TimeCardSMARaw(
            size: UInt32(MemoryLayout<TimeCardSMARaw>.size),
            connector: connector,
            direction: direction.rawValue,
            function: function
        )
        try callInOut(connection: connection, selector: 5, value: &raw)
        return raw.route
    }

    private static func openConnection(
        for descriptor: TimeCardServiceDescriptor
    ) throws -> io_connect_t {
        guard let matching = IORegistryEntryIDMatching(descriptor.id) else {
            throw TimeCardClientError.matchingDictionary
        }
        let service = IOServiceGetMatchingService(kIOMainPortDefault, matching)
        guard service != IO_OBJECT_NULL else {
            throw TimeCardClientError.serviceDisappeared
        }
        defer { IOObjectRelease(service) }

        var connection: io_connect_t = 0
        let openResult = IOServiceOpen(
            service, mach_task_self_, 0, &connection
        )
        guard openResult == KERN_SUCCESS else {
            throw TimeCardClientError.openFailed(openResult)
        }
        return connection
    }

    private static func callOutput<T>(
        connection: io_connect_t,
        selector: UInt32,
        output: inout T
    ) throws {
        var outputSize = MemoryLayout<T>.size
        let result = withUnsafeMutableBytes(of: &output) { bytes in
            IOConnectCallStructMethod(
                connection,
                selector,
                nil,
                0,
                bytes.baseAddress,
                &outputSize
            )
        }
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.methodFailed(
                selector: selector, result: result
            )
        }
        guard outputSize == MemoryLayout<T>.size else {
            throw TimeCardClientError.unexpectedOutput(
                selector: selector,
                expected: MemoryLayout<T>.size,
                actual: outputSize
            )
        }
    }

    private static func callInput<T>(
        connection: io_connect_t,
        selector: UInt32,
        input: inout T
    ) throws {
        let result = withUnsafeBytes(of: &input) { bytes in
            IOConnectCallStructMethod(
                connection,
                selector,
                bytes.baseAddress,
                bytes.count,
                nil,
                nil
            )
        }
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.methodFailed(
                selector: selector, result: result
            )
        }
    }

    private static func callInOut<T>(
        connection: io_connect_t,
        selector: UInt32,
        value: inout T
    ) throws {
        var input = value
        var outputSize = MemoryLayout<T>.size
        let result = withUnsafeMutableBytes(of: &value) { outputBytes in
            withUnsafeBytes(of: &input) { inputBytes in
                IOConnectCallStructMethod(
                    connection,
                    selector,
                    inputBytes.baseAddress,
                    inputBytes.count,
                    outputBytes.baseAddress,
                    &outputSize
                )
            }
        }
        guard result == KERN_SUCCESS else {
            throw TimeCardClientError.methodFailed(
                selector: selector, result: result
            )
        }
        guard outputSize == MemoryLayout<T>.size else {
            throw TimeCardClientError.unexpectedOutput(
                selector: selector,
                expected: MemoryLayout<T>.size,
                actual: outputSize
            )
        }
    }
}

private struct TimeCardTimeRaw {
    var seconds: UInt64 = 0
    var nanoseconds: UInt32 = 0
    var reserved: UInt32 = 0
}

private struct TimeCardCrossTimestampRaw {
    var cardTime = TimeCardTimeRaw()
    var systemTimeBeforeNanoseconds: UInt64 = 0
    var systemTimeAfterNanoseconds: UInt64 = 0
}

private struct TimeCardInfoRaw {
    var abiVersion: UInt32 = 0
    var driverVersion: UInt32 = 0
    var vendorID: UInt16 = 0
    var deviceID: UInt16 = 0
    var layout: UInt32 = 0
    var advertisedMSIXVectors: UInt32 = 0
    var barSize: UInt64 = 0
    var clockOffset: UInt64 = 0
    var todOffset: UInt64 = 0
    var clockVersion: UInt32 = 0
    var clockStatus: UInt32 = 0
    var clockSelect: UInt32 = 0
    var todVersion: UInt32 = 0
    var todStatus: UInt32 = 0
    var utcStatus: UInt32 = 0
    var leap: UInt32 = 0
    var gnssStatus: UInt32 = 0
    var satellites: UInt32 = 0
    var boardProfile: UInt32 = 0
    var capabilities: UInt32 = 0
    var validFields: UInt64 = 0
    var pciRevision: UInt32 = 0
    var reserved: UInt32 = 0
}

private struct TimeCardSMARaw {
    var size: UInt32 = 0
    var connector: UInt32 = 0
    var direction: UInt32 = 0
    var function: UInt32 = 0
    var flags: UInt32 = 0
    var inputMap: UInt32 = 0
    var outputMap: UInt32 = 0
    var reserved: UInt32 = 0

    var route: TimeCardSMARoute {
        TimeCardSMARoute(
            connector: connector,
            direction: TimeCardSMADirection(rawValue: direction) ?? .disabled,
            function: function,
            flags: flags,
            inputMap: inputMap,
            outputMap: outputMap
        )
    }
}
