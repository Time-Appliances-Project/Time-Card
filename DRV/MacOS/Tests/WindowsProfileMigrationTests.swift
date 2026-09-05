/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

@main enum WindowsProfileMigrationTests {
    static func rejected(_ body: () throws -> Void) {
        do { try body(); preconditionFailure("Expected XML rejection") } catch { }
    }
    static func xml(_ fields: String, name: String = "Windows timing") -> String {
        "<ConfigurationProfile><Name>\(name)</Name>\(fields)</ConfigurationProfile>"
    }
    static func decode(_ text: String) throws -> [WindowsConfigurationProfile] {
        try WindowsConfigurationProfile.decode(Data(text.utf8))
    }
    static func main() throws {
        let target = TimeCardProfileTarget(vendorID: 0x1d9b, deviceID: 0x400, revision: 0,
            boardProfile: 1, layout: 1, clockVersion: 0x01080000)
        let output = TimeCardPPSState(core: 1, version: 0x01060000, validFields: 31, control: 1,
            polarity: 1, pulseWidth: 100, maximumDelay: 0x3fffffff, writableFields: 29)
        let input = TimeCardPPSState(core: 2, version: 0x01020000, validFields: 29, control: 1,
            polarity: 1, pulseWidth: 80, maximumDelay: 65535, writableFields: 21)
        let state = TimeCardProfileState(serviceID: 123, target: target, settings: [
            .init(kind: .clockSource, channel: 0, values: [3]),
            .init(kind: .smaRoute, channel: 1, values: [0, 1]),
            try .pps(output), try .pps(input)
        ], supportedClockSources: [1, 3], fixedDirections: [:], immutableSettings: [], notes: [])
        let clock = "<HasClockSource>true</HasClockSource><ClockSource>3</ClockSource>"
        let basic = try decode(xml(clock)).first!.review(for: state)
        precondition(basic.blockers.isEmpty && basic.profile?.schemaVersion == 1)
        precondition(basic.profile?.settings.count == 1 && basic.serviceID == 123 && basic.profile?.target == target)
        let prefixed = "<?xml version=\"1.0\" encoding=\"utf-8\"?><ConfigurationProfile xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\"><Name>Timing &amp; PPS</Name><SchemaVersion>2</SchemaVersion>\(clock)</ConfigurationProfile>"
        precondition(try! decode(prefixed)[0].name == "Timing & PPS")
        let list = "<ConfigurationProfileList><Profiles>\(xml(clock))\(xml(clock, name: "Second"))</Profiles></ConfigurationProfileList>"
        precondition(try! decode(list).count == 2)
        let pps = "<PpsEngines><PpsProfileSetting><Core>1</Core><CoreVersion>17170432</CoreVersion><Enabled>true</Enabled><ActiveHigh>true</ActiveHigh><HasPulseWidth>true</HasPulseWidth><PulseWidthMilliseconds>250</PulseWidthMilliseconds><CableDelayNanoseconds>-25</CableDelayNanoseconds></PpsProfileSetting></PpsEngines>"
        let migrated = try decode(xml("<SchemaVersion>2</SchemaVersion><RequiredAbiVersion>12</RequiredAbiVersion><RequiredFpgaCoreMask>3</RequiredFpgaCoreMask>" + clock + pps))[0].review(for: state, date: Date(timeIntervalSince1970: 1_700_000_000))
        precondition(migrated.blockers.isEmpty && migrated.profile?.schemaVersion == 2)
        let requested = migrated.profile!.settings.first { $0.kind == .pps }!
        precondition(requested.values == [output.version, 29, 1, 1, 250, UInt32(bitPattern: -25)])
        precondition(try! TimeCardConfigurationProfile.decode(migrated.profile!.encoded()) == migrated.profile)
        let plan = try TimeCardProfilePlan.create(profile: migrated.profile!, state: state)
        precondition(plan.canApply && plan.changes.count == 1)
        let preserve = try decode(xml(pps.replacingOccurrences(of: "<HasPulseWidth>true", with: "<HasPulseWidth>false")))[0].review(for: state)
        precondition(preserve.profile?.settings[0].values[4] == 100 && preserve.notes.contains { $0.contains("width is preserved") })
        let slave = pps.replacingOccurrences(of: "<Core>1", with: "<Core>2")
            .replacingOccurrences(of: "17170432", with: "16908288")
        precondition(try! decode(xml(slave))[0].review(for: state).profile == nil, "Input width must not become a writable setting")
        let slaveOK = slave.replacingOccurrences(of: "<HasPulseWidth>true", with: "<HasPulseWidth>false")
        let slaveProfile = try decode(xml(slaveOK))[0].review(for: state).profile!
        precondition(slaveProfile.settings[0].values[4] == 0 && slaveProfile.settings[0].values[5] == UInt32(bitPattern: -25))
        let sma = "<Sma><SmaProfileSetting><Connector>1</Connector><Direction>Input</Direction><Function>2</Function></SmaProfileSetting></Sma>"
        precondition(try! decode(xml(sma))[0].review(for: state).profile?.settings[0].values == [0, 2])
        for extra in [
            "<HasNmea>true</HasNmea>", "<HasNmeaAdvanced>true</HasNmeaAdvanced>",
            "<HasFpgaImageIdentity>true</HasFpgaImageIdentity>", "<RequiredAbiVersion>16</RequiredAbiVersion>",
            "<RequiredFpgaCoreMask>256</RequiredFpgaCoreMask>",
            "<TimecodeEngines><TimecodeProfileSetting><Format>1</Format></TimecodeProfileSetting></TimecodeEngines>",
            "<TodParser><Enabled>false</Enabled></TodParser>",
            "<SignalGenerators><SignalGeneratorProfileSetting><Generator>1</Generator><Inverted>true</Inverted></SignalGeneratorProfileSetting></SignalGenerators>"
        ] {
            let review = try decode(xml(clock + extra))[0].review(for: state)
            precondition(review.profile == nil && !review.blockers.isEmpty, "Unsupported controls must block the whole import")
        }
        for bad in [
            "", "<ConfigurationProfile>", "<Wrong/>", "<ConfigurationProfileList><Profiles/></ConfigurationProfileList>",
            xml(clock + "<SchemaVersion>3</SchemaVersion>"), xml(clock + "<Unexpected>1</Unexpected>"),
            xml(clock + "<Name>Duplicate</Name>"), xml(clock + "<HasClockSource>false</HasClockSource>"),
            xml("<ClockSource>-1</ClockSource>"), xml("<ClockSource>4294967296</ClockSource>"),
            xml("<HasClockSource>yes</HasClockSource>"), xml("<ClockSource><Nested>3</Nested></ClockSource>"),
            xml("<Sma><Other/></Sma>"), xml("<Sma><SmaProfileSetting><Direction>input</Direction></SmaProfileSetting></Sma>"),
            xml("<TodParser><Unknown>1</Unknown></TodParser>"), xml("<Description>" + String(repeating: "x", count: 4097) + "</Description>"),
            xml(clock, name: ""), xml(clock, name: "Bad&#10;name"),
            xml(clock).replacingOccurrences(of: "<ClockSource>", with: "<ClockSource ignored=\"true\">"),
            xml(clock).replacingOccurrences(of: "<ConfigurationProfile>", with: "<ConfigurationProfile xmlns=\"urn:unknown\">"),
            "<!DOCTYPE ConfigurationProfile [<!ENTITY example SYSTEM \"file:///etc/passwd\">]>" + xml(clock, name: "&example;"),
            "<!DOCTYPE ConfigurationProfile [<!ENTITY a \"huge\">]>" + xml(clock, name: "&a;"),
            xml(clock) + xml(clock), xml("mixed" + clock),
            "<ConfigurationProfileList><Profiles>" + String(repeating: xml(clock), count: 33) + "</Profiles></ConfigurationProfileList>"
        ] { rejected { _ = try decode(bad) } }
        rejected { _ = try WindowsConfigurationProfile.decode(Data(repeating: 32, count: 65537)) }
        rejected { _ = try WindowsConfigurationProfile.decode(xml(clock).data(using: .utf16)!) }
        let mismatch = try decode(xml(pps.replacingOccurrences(of: "17170432", with: "17104896")))[0].review(for: state)
        precondition(mismatch.profile == nil && !mismatch.blockers.isEmpty)
        let invalidWidth = try decode(xml(pps.replacingOccurrences(of: ">250<", with: ">1000<")))[0].review(for: state)
        precondition(invalidWidth.profile == nil)
        let duplicateSMA = sma.replacingOccurrences(of: "</Sma>", with: sma.replacingOccurrences(of: "<Sma>", with: "").replacingOccurrences(of: "</Sma>", with: "") + "</Sma>")
        precondition(try! decode(xml(duplicateSMA))[0].review(for: state).profile == nil)
        let unsupportedClock = try decode(xml(clock.replacingOccurrences(of: ">3<", with: ">4<")))[0].review(for: state)
        precondition(unsupportedClock.profile == nil)
        precondition(try! decode(xml(""))[0].review(for: state).profile == nil)
        print("Windows profile tests passed: single/library XML, schema 0/2, clock/SMA/PPS migration, identity/core gates, no partial imports, DTD/entity denial, malformed/duplicate/unknown fields and size limits.")
    }
}
