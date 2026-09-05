/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

struct WindowsProfileReview: Identifiable, Sendable {
    let id = UUID()
    let name: String
    let serviceID: UInt64
    let profile: TimeCardConfigurationProfile?
    let notes: [String]
    let blockers: [String]
}

// These names and defaults mirror ControlCenterProduct.cs, Windows schema 0...2.
// Windows ABI numbers and FPGA identity are never reinterpreted as macOS values.
struct WindowsConfigurationProfile: Sendable {
    private let root: ProfileXMLNode
    var name: String { root.string("Name") }

    static func decode(_ data: Data) throws -> [Self] {
        guard data.count <= TimeCardConfigurationProfile.maximumFileBytes,
              let xml = String(data: data, encoding: .utf8) else {
            throw TimeCardProfileError.invalid("Windows XML must be UTF-8 and at most 64 KiB.")
        }
        guard !xml.uppercased().contains("<!DOCTYPE"), !xml.uppercased().contains("<!ENTITY") else {
            throw TimeCardProfileError.invalid("DTD and entity declarations are not allowed in profiles.")
        }
        let reader = ProfileXMLReader()
        let parser = XMLParser(data: data)
        parser.shouldProcessNamespaces = true
        parser.shouldResolveExternalEntities = false
        parser.externalEntityResolvingPolicy = .never
        parser.delegate = reader
        guard parser.parse(), reader.failure == nil, let root = reader.root else {
            throw TimeCardProfileError.invalid(reader.failure ?? "Malformed Windows XML profile.")
        }
        let nodes: [ProfileXMLNode]
        if root.name == "ConfigurationProfile" { nodes = [root] }
        else if root.name == "ConfigurationProfileList" {
            try root.requireChildren(["Profiles"])
            guard let list = root.child("Profiles") else { throw TimeCardProfileError.invalid("Missing Profiles list.") }
            try list.requireList("ConfigurationProfile", maximum: 32)
            nodes = list.children
        } else { throw TimeCardProfileError.invalid("Expected ConfigurationProfile or ConfigurationProfileList.") }
        guard !nodes.isEmpty else { throw TimeCardProfileError.invalid("The Windows profile list is empty.") }
        return try nodes.map { node in
            try validateShape(node)
            let name = node.string("Name")
            guard !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty, name.utf8.count <= 160,
                  !name.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) else {
                throw TimeCardProfileError.invalid("Windows profile names must contain 1...160 UTF-8 bytes without control characters.")
            }
            guard node.uint("SchemaVersion") <= 2 else { throw TimeCardProfileError.invalid("Unsupported Windows profile schema.") }
            return Self(root: node)
        }
    }

    func review(for state: TimeCardProfileState, date: Date = Date()) -> WindowsProfileReview {
        var settings: [TimeCardProfileSetting] = []
        var notes = ["Windows schema \(root.uint("SchemaVersion")); captured Windows ABI \(root.uint("CapturedAbiVersion")).",
                     "Destination: \(state.target.summary). Staging creates a native copy bound to this hardware identity; Preview and Apply are still required."]
        var blockers: [String] = []
        if root.uint("RequiredAbiVersion") > 15 { blockers.append("The required Windows ABI is newer than the supported import contract (15).") }
        if root.bool("HasFpgaImageIdentity") {
            blockers.append("This profile requires an exact FPGA image identity that the macOS driver cannot verify. Its image constraint will not be discarded.")
        }
        let mask = root.uint("RequiredFpgaCoreMask")
        if mask & ~UInt32(3) != 0 { blockers.append(String(format: "Required FPGA core mask 0x%08x contains unimplemented profile engines.", mask)) }
        for core: UInt32 in 1...2 where mask & (1 << (core - 1)) != 0 {
            if state.values["pps:\(core)"] == nil { blockers.append("Required PPS core \(core) is unavailable on the destination.") }
        }
        if root.bool("HasNmea") || root.bool("HasNmeaAdvanced") { blockers.append("NMEA/ToD output configuration is not yet supported by native profiles.") }
        if !(root.child("TimecodeEngines")?.children.isEmpty ?? true) { blockers.append("IRIG-B/DCF timecode configuration is not yet supported by native profiles.") }
        if root.child("TodParser") != nil { blockers.append("ToD parser configuration is not yet supported by native profiles.") }
        if !(root.child("SignalGenerators")?.children.isEmpty ?? true) { blockers.append("Signal-generator configuration is not yet supported by native profiles.") }
        if root.bool("HasClockSource") {
            settings.append(.init(kind: .clockSource, channel: 0, values: [root.uint("ClockSource")]))
        }
        for route in root.child("Sma")?.children ?? [] {
            let directions: [String: UInt32] = ["Input": 0, "Output": 1, "Disabled": 2]
            settings.append(.init(kind: .smaRoute, channel: route.uint("Connector"),
                values: [directions[route.string("Direction", default: "Input")]!, route.uint("Function")]))
        }
        for source in root.child("PpsEngines")?.children ?? [] {
            let core = source.uint("Core")
            guard var desired = state.values["pps:\(core)"], let current = desired.ppsState else {
                blockers.append("PPS core \(core) has no native capture/restore contract."); continue
            }
            guard source.uint("CoreVersion") == current.version else {
                blockers.append("PPS core \(core) version differs from the captured Windows profile."); continue
            }
            desired.values[2] = source.bool("Enabled") ? 1 : 0
            if current.canWrite(4) { desired.values[3] = source.bool("ActiveHigh") ? 1 : 0 }
            else if source.bool("ActiveHigh") { blockers.append("PPS core \(core) does not expose writable polarity.") }
            if source.bool("HasPulseWidth") {
                if current.canWrite(8) { desired.values[4] = source.uint("PulseWidthMilliseconds") }
                else { blockers.append("PPS core \(core) pulse width is measured, not writable.") }
            } else if current.canWrite(8) {
                notes.append("PPS core \(core): the Windows profile does not set output width; the destination's captured width is preserved.")
            }
            let delay = source.int("CableDelayNanoseconds")
            if current.canWrite(16) { desired.values[5] = UInt32(bitPattern: delay) }
            else if delay != 0 { blockers.append("PPS core \(core) does not expose writable cable delay.") }
            settings.append(desired)
        }
        // Never silently create a partial profile. Any unsupported requirement
        // blocks the whole entry, while other entries in a library remain usable.
        var candidate: TimeCardConfigurationProfile?
        if settings.isEmpty { blockers.append("No supported clock-source, SMA, or PPS settings were found.") }
        else {
            let profile = TimeCardConfigurationProfile(name: name, capturedAt: date, target: state.target, settings: settings)
            do {
                let plan = try TimeCardProfilePlan.create(profile: profile, state: state, now: date)
                blockers += plan.blockers
                if blockers.isEmpty { candidate = profile }
            } catch { blockers.append(error.localizedDescription) }
        }
        return .init(name: name, serviceID: state.serviceID, profile: candidate, notes: notes, blockers: blockers)
    }

    private static func validateShape(_ node: ProfileXMLNode) throws {
        let strings: Set<String> = ["Name", "Description"]
        let booleans: Set<String> = ["HasClockSource", "HasNmea", "NmeaEnabled", "NmeaInverted", "HasNmeaAdvanced", "HasFpgaImageIdentity", "FpgaImageLoaderEncoding", "IsBuiltIn"]
        let signed: Set<String> = ["NmeaCorrectionSeconds", "NmeaLocalOffsetMinutes"]
        let unsigned: Set<String> = ["SchemaVersion", "CapturedAbiVersion", "RequiredAbiVersion", "RequiredFpgaCoreMask", "ClockSource", "NmeaBaud", "NmeaCoreVersion", "NmeaGnss", "NmeaMessageDisableMask", "FpgaImageRawVersion", "FpgaImageTag", "FpgaImageVersion", "FpgaImageLayout", "FpgaImageBoardProfile"]
        let containers: Set<String> = ["Sma", "PpsEngines", "TimecodeEngines", "TodParser", "SignalGenerators"]
        try node.requireChildren(strings.union(booleans).union(signed).union(unsigned).union(containers))
        for child in node.children where !containers.contains(child.name) {
            try child.requireScalar(type: strings.contains(child.name) ? .text : booleans.contains(child.name) ? .bool : signed.contains(child.name) ? .int : .uint)
        }
        let specs: [(String, String, Int, [String: XMLScalar])] = [
            ("Sma", "SmaProfileSetting", 4, ["Connector": .uint, "Direction": .direction, "Function": .uint]),
            ("PpsEngines", "PpsProfileSetting", 2, ["Core": .uint, "CoreVersion": .uint, "Enabled": .bool, "ActiveHigh": .bool, "HasPulseWidth": .bool, "PulseWidthMilliseconds": .uint, "CableDelayNanoseconds": .int]),
            ("TimecodeEngines", "TimecodeProfileSetting", 4, ["Format": .uint, "Role": .uint, "CoreVersion": .uint, "Enabled": .bool, "Mode": .uint, "Code": .uint, "CorrectionSeconds": .int, "HasDelay": .bool, "DelayNanoseconds": .int, "HasControlBits": .bool, "ControlBits": .uint]),
            ("SignalGenerators", "SignalGeneratorProfileSetting", 4, ["Generator": .uint, "CoreVersion": .uint, "Enabled": .bool, "ActiveHigh": .bool, "Inverted": .bool, "PeriodNanoseconds": .uint64, "PulseNanoseconds": .uint64, "PhaseNanoseconds": .uint64, "RepeatCount": .uint, "CableDelayNanoseconds": .uint])
        ]
        for (name, item, maximum, fields) in specs {
            if let list = node.child(name) {
                try list.requireList(item, maximum: maximum)
                for child in list.children { try child.requireFields(fields) }
            }
        }
        if let tod = node.child("TodParser") {
            try tod.requireFields(["CoreVersion": .uint, "Enabled": .bool, "Protocol": .uint, "Gnss": .uint, "Baud": .uint, "Inverted": .bool, "CorrectionSeconds": .int, "MessageDisableMask": .uint])
        }
    }
}

private enum XMLScalar { case text, bool, uint, uint64, int, direction }
private struct ProfileXMLNode: Sendable {
    let name: String
    var text = ""
    var children: [Self] = []
    func child(_ name: String) -> Self? { children.first { $0.name == name } }
    func string(_ name: String, default fallback: String = "") -> String { child(name)?.text ?? fallback }
    func uint(_ name: String) -> UInt32 { UInt32(string(name).trimmingCharacters(in: .whitespacesAndNewlines)) ?? 0 }
    func int(_ name: String) -> Int32 { Int32(string(name).trimmingCharacters(in: .whitespacesAndNewlines)) ?? 0 }
    func bool(_ name: String) -> Bool { ["true", "1"].contains(string(name).trimmingCharacters(in: .whitespacesAndNewlines)) }
    func requireChildren(_ names: Set<String>) throws {
        guard text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              children.allSatisfy({ names.contains($0.name) }), Set(children.map(\.name)).count == children.count else {
            throw TimeCardProfileError.invalid("Unknown, duplicate, or mixed-content fields in \(name).")
        }
    }
    func requireList(_ item: String, maximum: Int) throws {
        guard children.count <= maximum, children.allSatisfy({ $0.name == item }),
              text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw TimeCardProfileError.invalid("Invalid \(name) list or too many entries.")
        }
    }
    func requireFields(_ fields: [String: XMLScalar]) throws {
        try requireChildren(Set(fields.keys))
        for child in children { try child.requireScalar(type: fields[child.name]!) }
    }
    func requireScalar(type: XMLScalar) throws {
        guard children.isEmpty else { throw TimeCardProfileError.invalid("Nested content in \(name).") }
        let value = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let digits = !value.isEmpty && value.utf8.allSatisfy { (48...57).contains($0) }
        let valid: Bool
        switch type {
        case .text: valid = true
        case .bool: valid = ["true", "false", "1", "0"].contains(value)
        case .uint: valid = digits && UInt32(value) != nil
        case .uint64: valid = digits && UInt64(value) != nil
        case .int: valid = (digits || (value.first == "-" && value.dropFirst().utf8.allSatisfy { (48...57).contains($0) })) && Int32(value) != nil
        case .direction: valid = ["Input", "Output", "Disabled"].contains(text)
        }
        guard valid else { throw TimeCardProfileError.invalid("Invalid value for Windows field \(name).") }
    }
}

private final class ProfileXMLReader: NSObject, XMLParserDelegate {
    var root: ProfileXMLNode?
    var failure: String?
    private var stack: [ProfileXMLNode] = []
    private var count = 0
    private func fail(_ parser: XMLParser, _ reason: String) { failure = reason; parser.abortParsing() }
    func parser(_ parser: XMLParser, didStartElement name: String, namespaceURI: String?, qualifiedName: String?, attributes: [String: String]) {
        count += 1
        guard stack.count < 8, count <= 2048, (namespaceURI ?? "").isEmpty,
              attributes.allSatisfy({ ($0.key == "xmlns:xsi" && $0.value == "http://www.w3.org/2001/XMLSchema-instance") || ($0.key == "xmlns:xsd" && $0.value == "http://www.w3.org/2001/XMLSchema") }) else {
            fail(parser, "Unsupported XML namespaces/attributes or profile complexity limit exceeded."); return
        }
        stack.append(.init(name: name))
    }
    func parser(_ parser: XMLParser, foundCharacters string: String) {
        guard !stack.isEmpty else { return }
        stack[stack.count - 1].text += string
        if stack[stack.count - 1].text.utf8.count > 4096 { fail(parser, "An XML field exceeds 4 KiB.") }
    }
    func parser(_ parser: XMLParser, foundCDATA data: Data) {
        guard let value = String(data: data, encoding: .utf8) else { fail(parser, "Invalid UTF-8 CDATA."); return }
        self.parser(parser, foundCharacters: value)
    }
    func parser(_ parser: XMLParser, didEndElement name: String, namespaceURI: String?, qualifiedName: String?) {
        guard let node = stack.popLast(), node.name == name else { fail(parser, "Mismatched XML elements."); return }
        if stack.isEmpty {
            guard root == nil else { fail(parser, "Multiple XML roots."); return }
            root = node
        } else { stack[stack.count - 1].children.append(node) }
    }
    func parser(_ parser: XMLParser, resolveExternalEntityName name: String, systemID: String?) -> Data? {
        fail(parser, "External entities are not allowed."); return nil
    }
}
