/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation
@main struct ProfileLibraryTests {
    static func rejects(_ operation: () throws -> Void) {
        do { try operation(); fatalError("Expected rejection") } catch {}
    }
    static func main() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("timecard-library-test-" + UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let store = ProfileLibraryStore(directory: root.appendingPathComponent("Profiles"))
        let profile = TimeCardConfigurationProfile(name: "Lab PPS", capturedAt: Date(timeIntervalSince1970: 1000),
            target: .init(vendorID: 0x1d9b, deviceID: 0x0400, revision: 0, boardProfile: 1, layout: 1, clockVersion: 0x01020000),
            settings: [.init(kind: .clockSource, channel: 0, values: [3])])
        let empty = try store.list(); precondition(empty.entries.isEmpty)
        let a = try store.save(profile), b = try store.save(profile)
        precondition(a.id != b.id && a.profile == profile && b.profile == profile)
        let reopened = ProfileLibraryStore(directory: store.directory)
        let listing = try reopened.list(); precondition(listing.entries.count == 2 && listing.warnings.isEmpty)
        let read = try reopened.read(a.id); precondition(read.profile == profile)
        // Corrupt entries never hide valid siblings and are never staged.
        let badID = UUID(), badURL = store.directory.appendingPathComponent(badID.uuidString.lowercased() + ".json")
        try Data("{}".utf8).write(to: badURL)
        rejects { _ = try store.read(badID) }
        let partial = try store.list(); precondition(partial.entries.count == 2 && partial.warnings.count == 1)
        try Data(repeating: 0x20, count: TimeCardConfigurationProfile.maximumFileBytes + 1).write(to: badURL)
        rejects { _ = try store.read(badID) }
        let linkID = UUID(), linkURL = store.directory.appendingPathComponent(linkID.uuidString.lowercased() + ".json")
        try FileManager.default.createSymbolicLink(at: linkURL, withDestinationURL: store.directory.appendingPathComponent(a.id.uuidString.lowercased() + ".json"))
        rejects { _ = try store.read(linkID) }
        var changed = profile; changed.name = "Changed externally"
        try changed.encoded().write(to: store.directory.appendingPathComponent(a.id.uuidString.lowercased() + ".json"))
        rejects { try store.moveToTrash(a) }
        var invalid = profile; invalid.name = ""
        rejects { _ = try store.save(invalid) }
        let linkedDirectory = root.appendingPathComponent("LinkedProfiles")
        try FileManager.default.createSymbolicLink(at: linkedDirectory, withDestinationURL: store.directory)
        rejects { _ = try ProfileLibraryStore(directory: linkedDirectory).list() }
        let full = ProfileLibraryStore(directory: root.appendingPathComponent("Full"))
        _ = try full.list()
        let encoded = try profile.encoded()
        for _ in 0..<ProfileLibraryStore.maximumEntries {
            try encoded.write(to: full.directory.appendingPathComponent(UUID().uuidString.lowercased() + ".json"))
        }
        let atLimit = try full.list(); precondition(atLimit.entries.count == ProfileLibraryStore.maximumEntries)
        rejects { _ = try full.save(profile) }
        try encoded.write(to: full.directory.appendingPathComponent(UUID().uuidString.lowercased() + ".json"))
        rejects { _ = try full.list() }
        print("Profile library tests passed: persistence, immutable snapshots, corruption isolation, capacity/size/symlink guards, and stale removal rejection.")
    }
}
