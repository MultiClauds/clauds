import AppKit
import Sparkle

/// Self-update from GitHub releases, with Sparkle.
///
/// - The feed is `appcast.xml` in the repository (`SUFeedURL` in Info.plist).
/// - Each update file has an EdDSA signature. The public key is in Info.plist (`SUPublicEDKey`).
///   Sparkle refuses an update that has no valid signature, so a changed download cannot install.
/// - Sparkle asks the user one time if it can check automatically. Nothing installs without a click.
@MainActor
final class Updates: NSObject, ObservableObject {
    static let shared = Updates()

    /// A version that a background check found, and that the user did not look at yet.
    @Published private(set) var pendingVersion: String?
    /// False in a build with no feed (for example a command line hook, or a build with no Info.plist keys).
    @Published private(set) var isAvailable = false
    @Published var checksAutomatically = false {
        didSet {
            guard let updater = controller?.updater, updater.automaticallyChecksForUpdates != checksAutomatically else { return }
            updater.automaticallyChecksForUpdates = checksAutomatically
        }
    }

    private var controller: SPUStandardUpdaterController?

    /// Starts the updater. Call one time, after the app started.
    func start() {
        guard controller == nil,
              Bundle.main.object(forInfoDictionaryKey: "SUFeedURL") != nil,
              Bundle.main.object(forInfoDictionaryKey: "SUPublicEDKey") != nil else { return }
        let controller = SPUStandardUpdaterController(startingUpdater: true, updaterDelegate: nil, userDriverDelegate: self)
        self.controller = controller
        isAvailable = true
        checksAutomatically = controller.updater.automaticallyChecksForUpdates
    }

    /// Reads the setting again (Sparkle's own question can change it).
    func refresh() {
        guard let updater = controller?.updater else { return }
        if checksAutomatically != updater.automaticallyChecksForUpdates {
            checksAutomatically = updater.automaticallyChecksForUpdates
        }
    }

    /// The user asked: look for an update now, and show the result.
    func checkNow() {
        NSApp.activate(ignoringOtherApps: true)
        controller?.checkForUpdates(nil)
    }
}

// Clauds has no Dock icon, so a window of the app can open behind other windows.
// When a check finds an update, the app comes to the front, and Sparkle shows its window with the
// release notes and three buttons: Skip This Version, Remind Me Later, Install Update.
// Until the user looks at that window, the panel also shows a note.
extension Updates: SPUStandardUserDriverDelegate {
    nonisolated var supportsGentleScheduledUpdateReminders: Bool { true }

    nonisolated func standardUserDriverShouldHandleShowingScheduledUpdate(
        _ update: SUAppcastItem, andInImmediateFocus immediateFocus: Bool
    ) -> Bool {
        true   // Sparkle shows its window.
    }

    nonisolated func standardUserDriverWillHandleShowingUpdate(
        _ handleShowingUpdate: Bool, forUpdate update: SUAppcastItem, state: SPUUserUpdateState
    ) {
        let version = update.displayVersionString
        let userAsked = state.userInitiated
        Task { @MainActor in
            NSApp.activate(ignoringOtherApps: true)
            if !userAsked { self.pendingVersion = version }
        }
    }

    nonisolated func standardUserDriverDidReceiveUserAttention(forUpdate update: SUAppcastItem) {
        Task { @MainActor in self.pendingVersion = nil }
    }

    nonisolated func standardUserDriverWillFinishUpdateSession() {
        Task { @MainActor in self.pendingVersion = nil }
    }
}
