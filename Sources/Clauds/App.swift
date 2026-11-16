import AppKit
import Combine
import ServiceManagement
import SwiftUI

@main
struct ClaudsApp {
    /// The app has no SwiftUI scene. A scene would give it a window (for example an empty Settings
    /// window). The menu bar icon and the panel come from PanelController.
    static func main() {
        let app = NSApplication.shared
        runHooks()
        let delegate = AppDelegate()
        app.delegate = delegate
        app.setActivationPolicy(.accessory)
        app.mainMenu = AppDelegate.mainMenu()
        app.run()
    }

    /// Command line hooks, then the checks before a normal start.
    private static func runHooks() {
        // Test hook: Clauds --link-sessions <claude data dir> <shared dir>
        // Every file and folder that this app makes is for this macOS user only.
        umask(0o077)
        let args = CommandLine.arguments
        if args.count == 3, args[1] == "--render-panel" {
            MainActor.assumeIsolated { Self.renderPanel(to: URL(fileURLWithPath: args[2])) }
            exit(0)
        }
        // Docs: Clauds --render-demo <dir>. Screenshots and a GIF with fake accounts.
        if args.count == 3, args[1] == "--render-demo" {
            do { try MainActor.assumeIsolated { try Demo.renderAll(to: URL(fileURLWithPath: args[2])) }; exit(0) }
            catch { print("error: \(error)"); exit(1) }
        }
        // Check: Clauds --show-panel. Asks the running app to open its panel.
        if args.count == 2, args[1] == "--show-panel" {
            DistributedNotificationCenter.default().postNotificationName(
                PanelController.showRequest, object: nil, userInfo: nil, deliverImmediately: true)
            exit(0)
        }
        // Check: Clauds --login-item [on|off]. Prints the state of the login item.
        if args.count >= 2, args[1] == "--login-item" {
            if args.count == 3 {
                do {
                    if args[2] == "on" { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() }
                } catch { print("error: \(error)") }
            }
            print("status: \(SMAppService.mainApp.status.rawValue) (0 off, 1 on, 2 needs approval, 3 not found)")
            exit(0)
        }
        // Build step: Clauds --render-icons <dir>
        if args.count == 3, args[1] == "--render-icons" {
            do { try IconArt.renderAll(to: URL(fileURLWithPath: args[2])); exit(0) }
            catch { print("error: \(error)"); exit(1) }
        }
        // Test hook: Clauds --unlink-sessions <claude data dir> <shared dir> [account tag]
        if args.count >= 4, args[1] == "--unlink-sessions" {
            do {
                let n = try SessionLinker.unlink(claudeDir: URL(fileURLWithPath: args[2]),
                                                 sharedDir: URL(fileURLWithPath: args[3]),
                                                 accountTag: args.count > 4 ? args[4] : nil)
                print("unlinked \(n) folder(s)")
                exit(0)
            } catch { print("error: \(error)"); exit(1) }
        }
        if args.count >= 4, args[1] == "--link-sessions" {
            do {
                let n = try SessionLinker.link(claudeDir: URL(fileURLWithPath: args[2]),
                                               sharedDir: URL(fileURLWithPath: args[3]),
                                               accountTag: args.count > 4 ? args[4] : nil)
                print("linked \(n) folder(s)")
                exit(0)
            } catch {
                print("error: \(error)")
                exit(1)
            }
        }
        // Test hook: Clauds --migrate-check <application support dir> <claude data dir> [open]
        // Runs the move of the old data folder on a test tree. "open" acts as if Claude is open.
        if args.count >= 4, args[1] == "--migrate-check" {
            let used = Migration.supportFolder(in: URL(fileURLWithPath: args[2]), claudeDir: URL(fileURLWithPath: args[3]),
                                               claudeIsOpen: args.count > 4 && args[4] == "open")
            print("uses: \(used.lastPathComponent)")
            exit(0)
        }
        // Check: Clauds --auto-switch-check. Tests the auto-switch decision with fake numbers.
        if args.count == 2, args[1] == "--auto-switch-check" {
            let failures = AutoSwitch.selfCheck()
            failures.forEach { print("FAILED: \($0)") }
            print(failures.isEmpty ? "ok" : "\(failures.count) failed")
            exit(failures.isEmpty ? 0 : 1)
        }
        Self.quitIfAlreadyRunning()
    }

    /// Build check: Clauds --render-panel <png>. Draws the panel with the real accounts.
    @MainActor static func renderPanel(to url: URL) {
        let store = AccountStore()
        store.refreshUsage()
        store.usePreviewUsage(LiveUsage.savedAnswers(in: store.supportDir.appendingPathComponent("usage-debug.json")))
        let host = NSHostingView(rootView: MenuContent(store: store)
            .background(Color(nsColor: .windowBackgroundColor)))
        host.frame = NSRect(origin: .zero, size: host.fittingSize)
        let window = NSWindow(contentRect: host.frame, styleMask: [.borderless], backing: .buffered, defer: false)
        window.contentView = host
        host.layoutSubtreeIfNeeded()
        guard let rep = host.bitmapImageRepForCachingDisplay(in: host.bounds) else { return }
        host.cacheDisplay(in: host.bounds, to: rep)
        try? rep.representation(using: .png, properties: [:])?.write(to: url)
    }

    /// Only one Clauds can run. Two at the same time could move the same files.
    static func quitIfAlreadyRunning() {
        guard let id = Bundle.main.bundleIdentifier else { return }
        let me = ProcessInfo.processInfo.processIdentifier
        let others = NSRunningApplication.runningApplications(withBundleIdentifier: id)
            .filter { $0.processIdentifier != me }
        if !others.isEmpty { exit(0) }
    }

}

struct MenuContent: View {
    @ObservedObject var store: AccountStore
    @ObservedObject private var updates = Updates.shared
    @State private var startsAtLogin = LoginItem.isOn

    private let usageTimer = Timer.publish(every: 60, on: .main, in: .common).autoconnect()

    /// Top group: the current account, or in Side by Side all accounts that have a Claude copy open.
    private func isPrimary(_ account: Account) -> Bool {
        store.sideBySide ? store.runningIDs.contains(account.id) : account.id == store.activeID
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 9) {
            if let version = updates.pendingVersion {
                Button { updates.checkNow() } label: {
                    Notice(icon: "arrow.down.circle.fill", tint: .green,
                           title: "Version \(version) is ready",
                           detail: "Click to see what is new and to install it.")
                }
                .buttonStyle(.plain)
            }
            if let note = store.autoSwitchNote {
                Notice(icon: "arrow.triangle.2.circlepath", tint: .blue, title: note.title, detail: note.detail)
            }
            if store.accounts.isEmpty {
                Text("No accounts yet").font(.headline)
                Text("Save the login that the Claude app has now. Then add your other accounts.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                Button("Save Current Claude Login as Account…") { adoptCurrent() }
            } else {
                let primary = store.accounts.filter { isPrimary($0) }
                let others = store.accounts.filter { !isPrimary($0) }
                if !primary.isEmpty {
                    SectionTitle(store.sideBySide ? "OPEN ACCOUNTS" : "CURRENT ACCOUNT")
                    CardGroup { rows(primary) }
                }
                if !others.isEmpty {
                    SectionTitle("SAVED ACCOUNTS")
                    CardGroup { rows(others) }
                }

                VStack(spacing: 6) {
                    Toggle(isOn: Binding(get: { store.sideBySide }, set: { setSideBySide($0) })) {
                        Text("Side by Side (no restart)").frame(maxWidth: .infinity, alignment: .leading)
                    }
                    Toggle(isOn: Binding(get: { store.sessionsAreShared }, set: { setShareSessions($0) })) {
                        Text(store.sideBySide ? "Share Code Sessions (risky in Side by Side)" : "Share Code Sessions")
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
                .toggleStyle(.switch)
                .controlSize(.small)
                .font(.system(size: 12))
                .disabled(store.isSwitching)
                .padding(.horizontal, 4)
                .padding(.top, 4)
            }

            Divider()

            HStack {
                if !store.accounts.isEmpty {
                    Button { addAccount() } label: { Label("Add Account", systemImage: "plus") }
                        .buttonStyle(.borderless)
                }
                Spacer()
                if store.isSwitching { ProgressView().controlSize(.small) }
                Menu {
                    if store.sessionsAreShared {
                        Button("Share Code Sessions Now (Restarts Claude)") { syncSessions() }
                            .disabled(store.isSwitching)
                    }
                    Toggle("Start at Login", isOn: Binding(
                        get: { startsAtLogin },
                        set: { on in
                            if let error = LoginItem.set(on) { Prompt.error(error) }
                            startsAtLogin = LoginItem.isOn
                        }))
                    Picker("Auto-Switch Near a Limit", selection: Binding(
                        get: { store.autoSwitch }, set: { setAutoSwitch($0) })) {
                        ForEach(AutoSwitch.Mode.allCases) { Text($0.title).tag($0) }
                    }
                    .disabled(store.sideBySide || store.accounts.count < 2)
                    if updates.isAvailable {
                        Button("Check for Updates…") { updates.checkNow() }
                        Toggle("Check for Updates Automatically", isOn: $updates.checksAutomatically)
                        Divider()
                    }
                    Button("Show Saved Accounts in Finder") {
                        NSWorkspace.shared.activateFileViewerSelecting([store.profilesDir])
                    }
                    Button("Open Claude") { store.openClaude() }
                    Divider()
                    Button("About Clauds") { About.show() }
                    Button("Quit Clauds") { NSApp.terminate(nil) }
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
                .menuStyle(.borderlessButton)
                .menuIndicator(.hidden)
                .fixedSize()
            }
            .font(.system(size: 12))
            .padding(.horizontal, 4)
        }
        .padding(12)
        .frame(width: 350)
        .onAppear {
            store.refreshUsage()
            startsAtLogin = LoginItem.isOn
            updates.refresh()
            store.refreshRunning()
        }
        .onReceive(usageTimer) { _ in store.refreshUsage() }
        .onDisappear { store.clearAutoSwitchNote() }
    }

    @ViewBuilder
    private func rows(_ list: [Account]) -> some View {
        ForEach(Array(list.enumerated()), id: \.element.id) { index, account in
            if index > 0 { Divider().padding(.leading, 56) }
            AccountRow(
                account: account,
                usage: store.usage[account.id],
                isOpen: store.sideBySide && store.runningIDs.contains(account.id),
                needsSignIn: store.liveState[account.id] == .needsSignIn,
                isLoading: store.loadingIDs.contains(account.id),
                failure: store.liveState[account.id]?.failureText,
                disabled: store.isSwitching,
                onSelect: { store.sideBySide ? show(account) : switchTo(account) }
            ) {
                if let state = store.liveState[account.id], state != .ok {
                    if state != .needsSignIn {
                        Button("Load Usage Again") { store.refreshLiveUsage(force: true) }
                    }
                    Button("Sign In for Live Usage…") { signInForUsage(account) }
                    Divider()
                }
                Button("Open Chats in Window") { ChatWindows.shared.open(account) }
                Button("New Chat in Window") { ChatWindows.shared.open(account, path: "/new") }
                Divider()
                Button("Rename…") { rename(account) }
                Button("Set Email…") { setEmail(account) }
                Button("Move Up") { store.move(account, by: -1) }
                Button("Move Down") { store.move(account, by: 1) }
                Divider()
                Button("Remove…") { remove(account) }
                    .disabled(account.id == store.activeID)
            }
        }
    }

    // MARK: Actions

    private func adoptCurrent() {
        guard let name = Prompt.text(
            title: "Name this account",
            message: "Clauds saves the login that the Claude app has now. Use a name you know, for example your email.",
            defaultValue: "")
        else { return }
        store.adoptCurrentLogin(name: name)
    }

    private func addAccount() {
        guard let name = Prompt.text(
            title: "Add account",
            message: "A sign-in window opens next. Sign in to this account there. The window closes by itself when the login is good. Then Claude opens for this account: sign in there one time too.",
            defaultValue: "")
        else { return }
        let account = store.addAccount(name: name)
        // In Side by Side, the account's own Claude copy opens after the usage sign-in.
        // Without Side by Side, a switch quits Claude, so you start that yourself.
        signInForUsage(account, thenOpenClaude: store.sideBySide)
    }

    /// Sign-in window for live usage. It closes by itself when the login is good.
    private func signInForUsage(_ account: Account, thenOpenClaude: Bool = false) {
        ChatWindows.shared.openForSignIn(account) {
            store.refreshLiveUsage(force: true)
            if thenOpenClaude { show(account) }
        }
    }

    private func rename(_ account: Account) {
        guard let name = Prompt.text(title: "Rename account", message: "", defaultValue: account.name) else { return }
        store.rename(account, to: name)
    }

    private func setEmail(_ account: Account) {
        guard let email = Prompt.text(
            title: "Email for \(account.name)",
            message: "Only shown in the panel, under the account name.",
            defaultValue: account.email ?? "")
        else { return }
        store.setEmail(account, to: email)
    }

    private func remove(_ account: Account) {
        guard Prompt.confirm(
            title: "Remove \(account.name)?",
            message: "Its saved Claude app data goes to the Trash. Its chat window login is deleted. You can sign in again later.",
            button: "Remove")
        else { return }
        do {
            try store.remove(account)
            ChatWindows.shared.forget(account)
            LiveUsage.shared.forget(account)
        } catch {
            Prompt.error(error)
        }
    }

    private func show(_ account: Account) {
        Task {
            do { try await store.showSideBySide(account) } catch { Prompt.error(error) }
        }
    }

    private func setSideBySide(_ on: Bool) {
        if store.isClaudeRunning {
            let message = on
                ? (store.shareInSideBySide
                    ? "All Claude copies quit, and the main one opens again. Running Code sessions stop. Code sessions stay shared, because you turned on \"Share Code Sessions\" in Side by Side before. That is risky: turn it off if you do not want it."
                    : "All Claude copies quit, and the main one opens again. Running Code sessions stop. For safety, each account then gets its own separate copy of the Code sessions.")
                : "All Claude copies quit, and the main one opens again. Running Code sessions stop. Code session sharing turns on again. For each session, the newer version is kept."
            guard Prompt.confirm(
                title: on ? "Turn on Side by Side?" : "Turn off Side by Side?",
                message: message,
                button: "Restart Claude")
            else { return }
        }
        Task {
            do { try await store.setSideBySide(on) } catch { Prompt.error(error) }
        }
    }

    private func setShareSessions(_ on: Bool) {
        if store.sideBySide && on {
            // Always shown, also when Claude is closed: this setting can damage sessions.
            guard Prompt.confirm(
                title: "Share Code sessions in Side by Side?",
                message: "This is not safe. With this on, all open Claude copies write the same session files.\n\nWhat can go wrong:\n• If the same session is open in two accounts, it can lose messages, split in two, or go back to an old state.\n• A session that you archive or rename in one copy can come back or change in a different copy.\n• A new session can stay hidden in the other copies until they restart.\n• The app cannot know which account made a new session. It gives new sessions to the main account.\n\nRule to stay safe: work on one session in one account at a time.\n\nAll Claude copies quit now, and the main one opens again. Running Code sessions stop.",
                button: "Share Anyway",
                warning: true)
            else { return }
            Task {
                do { try await store.setShareSessions(true) } catch { Prompt.error(error) }
            }
            return
        }
        if store.isClaudeRunning {
            guard Prompt.confirm(
                title: on ? "Share Code sessions?" : "Stop sharing Code sessions?",
                message: on
                    ? "Claude quits and opens again. Then every account uses the same Code sessions. For each session, the newer version is kept."
                    : "Claude quits and opens again. Then each account has its own separate copy of the Code sessions.",
                button: "Restart Claude")
            else { return }
        }
        Task {
            do { try await store.setShareSessions(on) } catch { Prompt.error(error) }
        }
    }

    private func setAutoSwitch(_ mode: AutoSwitch.Mode) {
        if mode == .auto {
            guard Prompt.confirm(
                title: "Switch automatically?",
                message: "When the current account reaches \(AutoSwitch.threshold)% of a limit, Clauds goes to the account with the most room. It does not ask. Claude quits and opens again, and running Claude Code sessions in the app stop.\n\nUse \"Ask Me First\" if you want to decide each time.",
                button: "Switch Automatically")
            else { return }
        }
        store.setAutoSwitch(mode)
    }

    private func syncSessions() {
        if store.isClaudeRunning {
            guard Prompt.confirm(
                title: "Share Code sessions now?",
                message: "All Claude copies quit, and the main one opens again. Then every account uses the same Code sessions. Running Code sessions stop.",
                button: "Restart Claude")
            else { return }
        }
        Task {
            do { try await store.syncSessionsNow() } catch { Prompt.error(error) }
        }
    }

    private func switchTo(_ account: Account) {
        guard account.id != store.activeID else {
            store.openClaude()
            return
        }
        if store.isClaudeRunning {
            guard Prompt.confirm(
                title: "Switch to \(account.name)?",
                message: "Claude quits and opens again with this account. Running Claude Code sessions in the app stop.",
                button: "Switch")
            else { return }
        }
        Task {
            do {
                try await store.switchTo(account)
            } catch {
                Prompt.error(error)
            }
        }
    }
}

@MainActor
enum Prompt {
    static func text(title: String, message: String, defaultValue: String) -> String? {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Cancel")
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 280, height: 24))
        field.stringValue = defaultValue
        field.placeholderString = "name@example.com"
        alert.accessoryView = field
        alert.window.initialFirstResponder = field
        guard alert.runModal() == .alertFirstButtonReturn else { return nil }
        let value = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        return value.isEmpty ? nil : value
    }

    /// `warning`: red warning style, and Return does not start the action (Cancel is the default button).
    static func confirm(title: String, message: String, button: String, warning: Bool = false) -> Bool {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        if warning {
            alert.alertStyle = .critical
            alert.messageText = title
            alert.informativeText = message
            alert.addButton(withTitle: "Cancel")
            let risky = alert.addButton(withTitle: button)
            risky.hasDestructiveAction = true
            return alert.runModal() == .alertSecondButtonReturn
        }
        alert.messageText = title
        alert.informativeText = message
        alert.addButton(withTitle: button)
        alert.addButton(withTitle: "Cancel")
        return alert.runModal() == .alertFirstButtonReturn
    }

    static func error(_ error: Error) {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "Could not do that"
        alert.informativeText = error.localizedDescription
        alert.runModal()
    }
}

/// Starts Clauds when you log in to the Mac. Uses the macOS login item list
/// (System Settings > General > Login Items). Only for the copy in /Applications.
enum LoginItem {
    static var isInstalledCopy: Bool { Bundle.main.bundlePath.hasPrefix("/Applications/") }
    static var isOn: Bool { SMAppService.mainApp.status == .enabled }

    /// Returns the error, if there is one.
    @discardableResult
    static func set(_ on: Bool) -> Error? {
        do {
            if on { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() }
            return nil
        } catch {
            return error
        }
    }

    /// Asks one time, at the first start from /Applications: "Start at login?"
    /// After that, the menu switch decides. The app never turns this on without a yes.
    @MainActor static func askOnce() {
        let key = "loginItemSetOnce"
        guard isInstalledCopy, !UserDefaults.standard.bool(forKey: key) else { return }
        UserDefaults.standard.set(true, forKey: key)
        guard !isOn else { return }
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "Start Clauds at login?"
        alert.informativeText = "Clauds can start by itself when you log in to your Mac. You can change this later in the … menu of the panel."
        alert.addButton(withTitle: "Start at Login")
        alert.addButton(withTitle: "Not Now")
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        if let error = set(true) { Prompt.error(error) }
    }
}

/// The About window: version, license, and a link to the project.
enum About {
    static let author = "Razvan Tanase"
    static let profileURL = "https://github.com/codeFi"
    static let projectURL = "https://github.com/codeFi/clauds"

    static var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    }

    @MainActor static func show() {
        let style = NSMutableParagraphStyle()
        style.alignment = .center
        style.paragraphSpacing = 4
        let base: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 11),
            .foregroundColor: NSColor.labelColor,
            .paragraphStyle: style,
        ]
        func link(_ text: String, _ url: String) -> NSAttributedString {
            var attributes = base
            attributes[.link] = URL(string: url)
            return NSAttributedString(string: text, attributes: attributes)
        }
        let credits = NSMutableAttributedString(
            string: "Switch the Claude desktop app between accounts.\nMade by \(author): ", attributes: base)
        credits.append(link("github.com/codeFi", profileURL))
        credits.append(NSAttributedString(string: "\nSource code: ", attributes: base))
        credits.append(link("codeFi/clauds", projectURL))
        credits.append(NSAttributedString(
            string: "\nFree and open source. MIT License.\nNot made by Anthropic. Not an official Claude product.",
            attributes: base))

        NSApp.activate(ignoringOtherApps: true)
        NSApp.orderFrontStandardAboutPanel(options: [
            .applicationName: "Clauds",
            .applicationVersion: version,
            .version: "",
            .credits: credits,
            NSApplication.AboutPanelOptionKey(rawValue: "Copyright"): "© 2026 \(author). MIT License.",
        ])
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var panel: PanelController?

    /// The app shows no menu bar of its own, but the shortcuts come from this menu:
    /// copy and paste in the sign-in and chat windows, and Close Window.
    static func mainMenu() -> NSMenu {
        let main = NSMenu()
        func add(_ title: String, _ items: [(String, Selector, String)]) {
            let menu = NSMenu(title: title)
            for (name, action, key) in items { menu.addItem(withTitle: name, action: action, keyEquivalent: key) }
            let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
            item.submenu = menu
            main.addItem(item)
        }
        add("Clauds", [("Quit Clauds", #selector(NSApplication.terminate(_:)), "q")])
        add("Edit", [
            ("Undo", Selector(("undo:")), "z"),
            ("Redo", Selector(("redo:")), "Z"),
            ("Cut", #selector(NSText.cut(_:)), "x"),
            ("Copy", #selector(NSText.copy(_:)), "c"),
            ("Paste", #selector(NSText.paste(_:)), "v"),
            ("Select All", #selector(NSText.selectAll(_:)), "a"),
        ])
        add("Window", [
            ("Close Window", #selector(NSWindow.performClose(_:)), "w"),
            ("Minimize", #selector(NSWindow.performMiniaturize(_:)), "m"),
        ])
        return main
    }

    // A second start of the app (for example a double-click in Applications) opens no window.
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool { false }

    func applicationDidFinishLaunching(_ notification: Notification) {
        MainActor.assumeIsolated {
            panel = PanelController()
            panel?.start()
            LoginItem.askOnce()
            Updates.shared.start()
        }
    }
}
