/* SPDX-License-Identifier: BSD-3-Clause */
import Foundation

struct ProfileLibraryEntry: Identifiable, Equatable {
    let id: UUID
    let savedAt: Date
    let profile: TimeCardConfigurationProfile
}

// Library entries are immutable snapshots. A new save never overwrites an
// existing profile, even when names match. Staging never applies to hardware.
struct ProfileLibraryStore {
    static let maximumEntries = 200
    let directory: URL
    private let files = FileManager.default
    static func userLibrary() throws -> Self {
        let base = try FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask,
                                               appropriateFor: nil, create: true)
        return Self(directory: base.appendingPathComponent("org.opentimeserver.timecard.macos/Profiles", isDirectory: true))
    }
    private func prepare() throws {
        try files.createDirectory(at: directory, withIntermediateDirectories: true)
        let values = try directory.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard values.isDirectory == true, values.isSymbolicLink != true else {
            throw TimeCardProfileError.invalid("The profile library must be a real directory, not a symbolic link.")
        }
    }
    private func url(_ id: UUID) -> URL { directory.appendingPathComponent(id.uuidString.lowercased() + ".json") }
    func list() throws -> (entries: [ProfileLibraryEntry], warnings: [String]) {
        try prepare()
        let candidates = try files.contentsOfDirectory(at: directory, includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey, .creationDateKey], options: [.skipsHiddenFiles])
            .filter { $0.pathExtension == "json" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
        guard candidates.count <= Self.maximumEntries else { throw TimeCardProfileError.invalid("Library exceeds \(Self.maximumEntries) entries. Move excess files out before continuing.") }
        var entries: [ProfileLibraryEntry] = [], warnings: [String] = []
        for candidate in candidates {
            do {
                guard let id = UUID(uuidString: candidate.deletingPathExtension().lastPathComponent), candidate.lastPathComponent == url(id).lastPathComponent else {
                    throw TimeCardProfileError.invalid("Unrecognized library filename.")
                }
                entries.append(try read(id))
            } catch { warnings.append(candidate.lastPathComponent + ": " + error.localizedDescription) }
        }
        return (entries.sorted { $0.savedAt > $1.savedAt }, warnings)
    }
    func read(_ id: UUID) throws -> ProfileLibraryEntry {
        let path = url(id)
        let values = try path.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey, .creationDateKey])
        guard values.isRegularFile == true, values.isSymbolicLink != true,
              (values.fileSize ?? Int.max) <= TimeCardConfigurationProfile.maximumFileBytes else {
            throw TimeCardProfileError.invalid("Library entry is not a bounded regular file.")
        }
        let handle = try FileHandle(forReadingFrom: path)
        defer { try? handle.close() }
        let data = try handle.read(upToCount: TimeCardConfigurationProfile.maximumFileBytes + 1) ?? Data()
        return .init(id: id, savedAt: values.creationDate ?? .distantPast, profile: try TimeCardConfigurationProfile.decode(data))
    }
    @discardableResult func save(_ profile: TimeCardConfigurationProfile) throws -> ProfileLibraryEntry {
        let data = try profile.encoded()
        let listing = try list()
        guard listing.entries.count + listing.warnings.count < Self.maximumEntries else { throw TimeCardProfileError.invalid("Profile library is full.") }
        let id = UUID()
        let staging = directory.appendingPathComponent("." + UUID().uuidString + ".pending")
        defer { try? files.removeItem(at: staging) }
        try data.write(to: staging, options: [.atomic])
        try files.moveItem(at: staging, to: url(id))
        return try read(id)
    }
    func moveToTrash(_ entry: ProfileLibraryEntry) throws {
        // Re-read before removing: do not trash a file changed since selection.
        guard try read(entry.id).profile == entry.profile else { throw TimeCardProfileError.invalid("This entry changed on disk. Reload the library before removing it.") }
        try files.trashItem(at: url(entry.id), resultingItemURL: nil)
    }
}

@MainActor final class ProfileLibrary: ObservableObject {
    @Published private(set) var entries: [ProfileLibraryEntry] = []
    @Published private(set) var warnings: [String] = []
    @Published private(set) var message = ""
    func reload() {
        do {
            let result = try ProfileLibraryStore.userLibrary().list()
            entries = result.entries; warnings = result.warnings
            message = "\(entries.count) saved configuration(s). Loading only stages a profile for review."
        } catch { entries = []; warnings = []; message = error.localizedDescription }
    }
    func save(_ profile: TimeCardConfigurationProfile) {
        do { try ProfileLibraryStore.userLibrary().save(profile); reload(); message = "Saved a new snapshot of \(profile.name). Existing entries were preserved." }
        catch { message = error.localizedDescription }
    }
    func read(_ entry: ProfileLibraryEntry) -> TimeCardConfigurationProfile? {
        do { return try ProfileLibraryStore.userLibrary().read(entry.id).profile }
        catch { message = error.localizedDescription; return nil }
    }
    func remove(_ entry: ProfileLibraryEntry) {
        do { try ProfileLibraryStore.userLibrary().moveToTrash(entry); reload(); message = "Moved \(entry.profile.name) to Trash. It can be restored from Finder." }
        catch { message = error.localizedDescription }
    }
}
