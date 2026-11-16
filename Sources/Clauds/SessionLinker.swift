import Foundation

/// Controls where the Claude app's Code session files are, and which account sees them.
///
/// The Claude app keeps one small file per Code session here:
///   <Claude data>/claude-code-sessions/<account id>/<org id>/local_<session id>.json
/// The conversation itself is in ~/.claude/projects, which all accounts already share.
///
/// SHARED layout (`link`):
///   <shared>/orgs/<org id>/                          real folder, holds ALL session files
///   <Claude data>/claude-code-sessions/<account id>  ->  symlink to <shared>/orgs
/// The symlink MUST be at the account level. Claude opens the <org id> folder with
/// "no symlinks allowed" (O_NOFOLLOW), so a symlink at the org level makes every save fail.
///
/// SEPARATE layout (`unlink`):
///   <Claude data>/claude-code-sessions/<account id>/<org id>/   real folder, holds ONLY the
///   sessions that this account owns. Sessions of other accounts wait in <shared>/orgs.
///
/// Data safety rules of this file:
///   1. One live file per session. Session files MOVE between the pool and the owner's folder.
///      They are not copied.
///   2. Nothing is deleted. If two different files have the same name, the newer one wins and
///      the older one moves to <shared>/_conflicts.
///   3. A folder is removed only when it is empty.
///   4. If the owner record cannot be read, every operation stops. It does not guess.
///   5. One operation at a time (lock).
///   6. A symlink that this app did not make is not touched.
enum SessionLinker {
    enum LinkerError: LocalizedError {
        case ownersUnreadable(String)
        case folderNotEmpty(String)

        var errorDescription: String? {
            switch self {
            case .ownersUnreadable(let path):
                return "The session owner record cannot be read: \(path). Nothing was changed. A copy is next to it (owners.json.bak)."
            case .folderNotEmpty(let path):
                return "A session folder still has files, so it was not removed: \(path). Nothing was lost."
            }
        }
    }

    private static let lock = NSLock()

    // MARK: Shared layout

    /// Returns the number of account folders that it linked in this call.
    @discardableResult
    static func link(claudeDir: URL, sharedDir: URL, accountTag: String? = nil) throws -> Int {
        lock.lock(); defer { lock.unlock() }
        let fm = FileManager.default
        let sessionsRoot = claudeDir.appendingPathComponent("claude-code-sessions", isDirectory: true)
        guard fm.fileExists(atPath: sessionsRoot.path) else { return 0 }
        let orgsRoot = sharedDir.appendingPathComponent("orgs", isDirectory: true)
        try makePrivateDir(orgsRoot)

        var owners = try loadOwners(sharedDir)
        defer { saveOwners(owners, sharedDir) }
        var linked = 0
        for accountDir in try children(of: sessionsRoot) where kind(of: accountDir) == .folder {
            let account = accountDir.lastPathComponent
            for item in try children(of: accountDir) {
                let label = "\(account)_\(item.lastPathComponent)"
                switch kind(of: item) {
                case .symlink:
                    // Old layout of this app: <org id> -> <shared>. Its files are loose in <shared>.
                    guard pointsTo(item, sharedDir) else { continue }   // not ours: leave it
                    let orgDir = orgsRoot.appendingPathComponent(item.lastPathComponent, isDirectory: true)
                    try makePrivateDir(orgDir)
                    try migrateLooseFiles(in: sharedDir, to: orgDir)
                    try fm.removeItem(at: item)   // removes the symlink only
                case .folder:
                    let orgDir = orgsRoot.appendingPathComponent(item.lastPathComponent, isDirectory: true)
                    try makePrivateDir(orgDir)
                    // A session with no owner record was made in this account's folder: this account owns it.
                    for file in try children(of: item) {
                        if let id = sessionID(of: file), owners[id] == nil {
                            owners[id] = account
                            if let accountTag { owners[signedInKey(accountTag)] = account }
                        }
                    }
                    try merge(item, into: orgDir, sharedDir: sharedDir, conflictLabel: label)
                    try removeIfEmpty(item)
                case .file:
                    try moveKeepingNewer(item, to: orgsRoot.appendingPathComponent(item.lastPathComponent),
                                         sharedDir: sharedDir, conflictLabel: label)
                }
            }
            try removeIfEmpty(accountDir)
            try fm.createSymbolicLink(at: accountDir, withDestinationURL: orgsRoot)
            linked += 1
        }
        try rebuildArchiveLists(under: orgsRoot)
        return linked
    }

    // MARK: Separate layout

    /// Each account gets its own real folder with ONLY the sessions that it owns (forks included).
    /// Own sessions move out of the pool into the folder. Sessions of other accounts stay in the pool.
    ///
    /// `accountTag` names the Clauds account of this data folder ("switcher:<id>").
    /// It marks sessions that were made while sessions were shared.
    ///
    /// Safe to run again on a folder that is already separate.
    @discardableResult
    static func unlink(claudeDir: URL, sharedDir: URL, accountTag: String? = nil) throws -> Int {
        lock.lock(); defer { lock.unlock() }
        let fm = FileManager.default
        let sessionsRoot = claudeDir.appendingPathComponent("claude-code-sessions", isDirectory: true)
        guard fm.fileExists(atPath: sessionsRoot.path) else { return 0 }
        let orgsRoot = sharedDir.appendingPathComponent("orgs", isDirectory: true)
        try makePrivateDir(orgsRoot)

        var owners = try loadOwners(sharedDir)
        defer { saveOwners(owners, sharedDir) }
        // Sessions made while shared carry the tag of the Clauds account. Change the tag to the
        // Claude account that is signed in in this data folder, when that is known.
        if let accountTag, let signedIn = owners[signedInKey(accountTag)] {
            for (id, owner) in owners where owner == accountTag { owners[id] = signedIn }
        }
        func isMine(_ id: String, _ account: String) -> Bool {
            guard let owner = owners[id] else { return false }
            return owner == account || owner == accountTag
        }

        var unlinked = 0
        for accountDir in try children(of: sessionsRoot) {
            let account = accountDir.lastPathComponent
            switch kind(of: accountDir) {
            case .symlink:
                guard pointsTo(accountDir, orgsRoot) else { continue }   // not ours: leave it
                try fm.removeItem(at: accountDir)   // removes the symlink only
                try makePrivateDir(accountDir)
                for poolOrg in try children(of: orgsRoot) where kind(of: poolOrg) == .folder {
                    let orgDir = accountDir.appendingPathComponent(poolOrg.lastPathComponent, isDirectory: true)
                    try makePrivateDir(orgDir)
                    try takeOwnFiles(from: poolOrg, to: orgDir, account: account, isMine: isMine, sharedDir: sharedDir)
                }
                unlinked += 1
            case .folder:
                for orgDir in try children(of: accountDir) where kind(of: orgDir) == .folder {
                    let poolOrg = orgsRoot.appendingPathComponent(orgDir.lastPathComponent, isDirectory: true)
                    try makePrivateDir(poolOrg)
                    // New sessions get this account as owner. Sessions of other accounts go back to the pool.
                    for item in try children(of: orgDir) {
                        guard let id = sessionID(of: item) else { continue }
                        if owners[id] == nil {
                            owners[id] = account
                            // A new session shows which Claude account is signed in here.
                            if let accountTag { owners[signedInKey(accountTag)] = account }
                        } else if !isMine(id, account) {
                            try moveKeepingNewer(item, to: poolOrg.appendingPathComponent(item.lastPathComponent),
                                                 sharedDir: sharedDir, conflictLabel: "not-owner_\(account)")
                        }
                    }
                    try takeOwnFiles(from: poolOrg, to: orgDir, account: account, isMine: isMine, sharedDir: sharedDir)
                }
            case .file:
                break
            }
        }
        // Each folder's archive list must match the sessions that are in that folder now.
        try rebuildArchiveLists(under: orgsRoot)
        for accountDir in try children(of: sessionsRoot) where kind(of: accountDir) == .folder {
            try rebuildArchiveLists(under: accountDir)
        }
        return unlinked
    }

    /// Own session files MOVE from the pool to the account's folder (one live file per session).
    /// Other items (scheduled tasks and so on) are copied, only if the account does not have them.
    private static func takeOwnFiles(from poolOrg: URL, to orgDir: URL, account: String,
                                     isMine: (String, String) -> Bool, sharedDir: URL) throws {
        let fm = FileManager.default
        for item in try children(of: poolOrg) {
            let dest = orgDir.appendingPathComponent(item.lastPathComponent)
            if let id = sessionID(of: item) {
                guard isMine(id, account) else { continue }
                try moveKeepingNewer(item, to: dest, sharedDir: sharedDir, conflictLabel: "pool_\(account)")
            } else if !fm.fileExists(atPath: dest.path) {
                try fm.copyItem(at: item, to: dest)
            }
        }
    }

    /// Puts one Claude data folder in the wanted state: shared or separate.
    static func apply(claudeDir: URL, sharedDir: URL, share: Bool, accountTag: String? = nil) throws {
        if share {
            try link(claudeDir: claudeDir, sharedDir: sharedDir, accountTag: accountTag)
        } else {
            try unlink(claudeDir: claudeDir, sharedDir: sharedDir, accountTag: accountTag)
        }
    }

    // MARK: Owner record

    /// `<shared>/owners.json`: session id -> owner.
    /// Owner is a Claude account id (the folder name), or "switcher:<id>" for a session made while shared.
    static func ownersFile(_ sharedDir: URL) -> URL { sharedDir.appendingPathComponent("owners.json") }

    /// Record key that holds the Claude account that is signed in in a Clauds account's data folder.
    static func signedInKey(_ accountTag: String) -> String { "@account-of:\(accountTag)" }

    /// No file: empty record. A file that cannot be read: an error, so that nothing guesses owners.
    static func loadOwners(_ sharedDir: URL) throws -> [String: String] {
        let file = ownersFile(sharedDir)
        guard FileManager.default.fileExists(atPath: file.path) else { return [:] }
        guard let data = try? Data(contentsOf: file),
              let owners = try? JSONDecoder().decode([String: String].self, from: data) else {
            throw LinkerError.ownersUnreadable(file.path)
        }
        return owners
    }

    /// Writes the record (atomic). Keeps the last version as owners.json.bak.
    static func saveOwners(_ owners: [String: String], _ sharedDir: URL) {
        let fm = FileManager.default
        let file = ownersFile(sharedDir)
        guard let old = try? loadOwners(sharedDir), old != owners else { return }
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        guard let data = try? encoder.encode(owners) else { return }
        if fm.fileExists(atPath: file.path) {
            let backup = sharedDir.appendingPathComponent("owners.json.bak")
            try? fm.removeItem(at: backup)
            try? fm.copyItem(at: file, to: backup)
        }
        try? data.write(to: file, options: .atomic)
        try? fm.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
    }

    /// "local_<id>.json" -> "local_<id>". Nil for files that are not session files.
    static func sessionID(of url: URL) -> String? {
        let name = url.lastPathComponent
        guard name.hasPrefix("local_"), name.hasSuffix(".json") else { return nil }
        return String(name.dropLast(5))
    }

    /// While sessions are shared, only one account runs at a time. Sessions in the pool that have
    /// no owner record yet were made by that account. This gives them to it.
    static func claimUnknown(sharedDir: URL, tag: String) throws {
        lock.lock(); defer { lock.unlock() }
        let orgsRoot = sharedDir.appendingPathComponent("orgs", isDirectory: true)
        guard FileManager.default.fileExists(atPath: orgsRoot.path) else { return }
        var owners = try loadOwners(sharedDir)
        for poolOrg in try children(of: orgsRoot) where kind(of: poolOrg) == .folder {
            for item in try children(of: poolOrg) {
                if let id = sessionID(of: item), owners[id] == nil { owners[id] = tag }
            }
        }
        saveOwners(owners, sharedDir)
    }

    // MARK: Archive list

    /// Claude stores "archived" in two places: the `isArchived` mark inside each session file, and
    /// one list per folder (`archived-sessions.idx`: the sorted ids of the archived sessions there).
    /// Session files move between folders, so the list can go out of date. This builds each list
    /// again from the marks of the session files that are in the folder.
    private static func rebuildArchiveLists(under parent: URL) throws {
        for orgDir in try children(of: parent) where kind(of: orgDir) == .folder {
            rebuildArchiveList(in: orgDir)
        }
    }

    private static func rebuildArchiveList(in orgDir: URL) {
        let fm = FileManager.default
        let file = orgDir.appendingPathComponent("archived-sessions.idx")
        var root: [String: Any] = ["v": 1]
        var old: [String] = []
        if fm.fileExists(atPath: file.path) {
            // A list that cannot be read is left as it is.
            guard let data = try? Data(contentsOf: file),
                  let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return }
            root = object
            old = object["archived"] as? [String] ?? []
        }
        guard let items = try? children(of: orgDir) else { return }
        var archived: [String] = []
        for item in items {
            guard let id = sessionID(of: item) else { continue }
            if let data = try? Data(contentsOf: item),
               let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                if object["isArchived"] as? Bool == true { archived.append(id) }
            } else if old.contains(id) {
                archived.append(id)   // session file cannot be read: keep its old state
            }
        }
        archived.sort()
        guard archived != old else { return }
        root["archived"] = archived
        guard let data = try? JSONSerialization.data(withJSONObject: root, options: [.sortedKeys]) else { return }
        try? data.write(to: file, options: .atomic)
        try? fm.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
    }

    // MARK: Helpers

    private enum Kind { case folder, symlink, file }

    private static func kind(of url: URL) -> Kind {
        let values = try? url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        if values?.isSymbolicLink == true { return .symlink }
        return values?.isDirectory == true ? .folder : .file
    }

    private static func children(of dir: URL) throws -> [URL] {
        try FileManager.default
            .contentsOfDirectory(at: dir, includingPropertiesForKeys: [.isDirectoryKey, .isSymbolicLinkKey])
            .filter { $0.lastPathComponent != ".DS_Store" }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
    }

    /// True if the symlink's target is exactly `target`.
    private static func pointsTo(_ symlink: URL, _ target: URL) -> Bool {
        guard let dest = try? FileManager.default.destinationOfSymbolicLink(atPath: symlink.path) else { return false }
        return URL(fileURLWithPath: dest).standardizedFileURL.path == target.standardizedFileURL.path
    }

    /// A folder that only the user can open.
    private static func makePrivateDir(_ dir: URL) throws {
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true,
                                                attributes: [.posixPermissions: 0o700])
        try? FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: dir.path)
    }

    /// Removes a folder only if it has no files (a .DS_Store does not count).
    private static func removeIfEmpty(_ dir: URL) throws {
        guard try children(of: dir).isEmpty else { throw LinkerError.folderNotEmpty(dir.path) }
        try FileManager.default.removeItem(at: dir)
    }

    /// Old layout only: session files that sit loose in <shared> go to one org folder.
    private static func migrateLooseFiles(in sharedDir: URL, to orgDir: URL) throws {
        let keep = ["orgs", "_conflicts", "owners.json", "owners.json.bak"]
        for item in try children(of: sharedDir) where !keep.contains(item.lastPathComponent) {
            try moveKeepingNewer(item, to: orgDir.appendingPathComponent(item.lastPathComponent),
                                 sharedDir: sharedDir, conflictLabel: "migrated")
        }
    }

    private static func merge(_ source: URL, into dest: URL, sharedDir: URL, conflictLabel: String) throws {
        for item in try children(of: source) {
            try moveKeepingNewer(item, to: dest.appendingPathComponent(item.lastPathComponent),
                                 sharedDir: sharedDir, conflictLabel: conflictLabel)
        }
    }

    /// Moves `item` to `dest`. If `dest` exists with different content, the newer one wins and the
    /// older one moves to "<shared>/_conflicts/<label>/<time>_<name>". Nothing is deleted,
    /// except a file that is an exact copy of the one that stays.
    private static func moveKeepingNewer(_ item: URL, to dest: URL, sharedDir: URL, conflictLabel: String) throws {
        let fm = FileManager.default
        guard fm.fileExists(atPath: dest.path) else {
            try fm.moveItem(at: item, to: dest)
            return
        }
        if fm.contentsEqual(atPath: item.path, andPath: dest.path) {
            try fm.removeItem(at: item)   // exact copy
            return
        }
        let conflicts = sharedDir.appendingPathComponent("_conflicts/\(conflictLabel)", isDirectory: true)
        try makePrivateDir(conflicts)
        let stamp = Int(Date().timeIntervalSince1970 * 1000)
        let loserSpot = conflicts.appendingPathComponent("\(stamp)_\(item.lastPathComponent)")
        if modified(item) > modified(dest) {
            try fm.moveItem(at: dest, to: loserSpot)
            try fm.moveItem(at: item, to: dest)
        } else {
            try fm.moveItem(at: item, to: loserSpot)
        }
    }

    private static func modified(_ url: URL) -> Date {
        (try? url.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
    }
}
