import Foundation

/// The first name of this app was "Claude Switcher". Its data folder was
///   ~/Library/Application Support/ClaudeSwitcher
/// The folder is now
///   ~/Library/Application Support/Clauds
///
/// The old folder moves to the new name one time. The move is a rename: it is immediate and copies no data.
/// Shared Code sessions use symlinks that contain the full path of the old folder, so this also
/// points those symlinks at the new folder.
///
/// Safety rules:
///   1. The move runs only while Claude is closed. An open Claude copy uses the session files.
///   2. If Claude is open, the app uses the old folder for this run and tries again at the next start.
///   3. If the new folder exists, nothing moves.
///   4. Only symlinks that point at the old pool are changed.
enum Migration {
    static let oldFolderName = "ClaudeSwitcher"
    static let folderName = "Clauds"

    /// Returns the data folder to use for this run.
    static func supportFolder(in appSupport: URL, claudeDir: URL, claudeIsOpen: Bool) -> URL {
        let fm = FileManager.default
        let new = appSupport.appendingPathComponent(folderName, isDirectory: true)
        let old = appSupport.appendingPathComponent(oldFolderName, isDirectory: true)
        guard !fm.fileExists(atPath: new.path), isRealFolder(old) else { return new }
        guard !claudeIsOpen else { return old }
        do { try fm.moveItem(at: old, to: new) } catch { return old }

        let oldPool = old.appendingPathComponent("SharedSessions/orgs", isDirectory: true)
        let newPool = new.appendingPathComponent("SharedSessions/orgs", isDirectory: true)
        let profiles = (try? fm.contentsOfDirectory(
            at: new.appendingPathComponent("Profiles", isDirectory: true), includingPropertiesForKeys: nil)) ?? []
        for dataDir in [claudeDir] + profiles {
            let sessionsRoot = dataDir.appendingPathComponent("claude-code-sessions", isDirectory: true)
            for item in (try? fm.contentsOfDirectory(at: sessionsRoot, includingPropertiesForKeys: nil)) ?? [] {
                repoint(item, from: oldPool, to: newPool)
            }
        }
        return new
    }

    /// Changes one symlink from the old pool to the new pool. Leaves every other item as it is.
    private static func repoint(_ link: URL, from oldPool: URL, to newPool: URL) {
        let fm = FileManager.default
        guard let dest = try? fm.destinationOfSymbolicLink(atPath: link.path),
              URL(fileURLWithPath: dest).standardizedFileURL.path == oldPool.standardizedFileURL.path else { return }
        do {
            try fm.removeItem(at: link)   // removes the symlink only
            try fm.createSymbolicLink(at: link, withDestinationURL: newPool)
        } catch {
            // Put the old symlink back. It does not work, but the session files are safe in the pool.
            try? fm.createSymbolicLink(atPath: link.path, withDestinationPath: dest)
        }
    }

    private static func isRealFolder(_ url: URL) -> Bool {
        let values = try? url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        return values?.isDirectory == true && values?.isSymbolicLink != true
    }
}
