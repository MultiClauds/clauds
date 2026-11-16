import AppKit
import WebKit

/// One claude.ai window per account. Each window has its own cookie store,
/// so every account stays signed in at the same time, next to the desktop app.
@MainActor
final class ChatWindows: NSObject {
    static let shared = ChatWindows()

    private var windows: [UUID: NSWindow] = [:]
    private var popups: [NSWindow] = []
    private var signInWatchers: [UUID: Task<Void, Never>] = [:]

    /// Safari's user agent. Some sign-in pages (for example Google) reject unknown embedded browsers.
    private let userAgent =
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15"

    func open(_ account: Account, path: String = "/recents") {
        NSApp.activate(ignoringOtherApps: true)
        if let window = windows[account.id] {
            window.title = "\(account.name) — Claude"
            if let webView = window.contentView as? WKWebView, path != "/recents" {
                webView.load(URLRequest(url: URL(string: "https://claude.ai\(path)")!))
            }
            window.makeKeyAndOrderFront(nil)
            return
        }

        let config = WKWebViewConfiguration()
        config.websiteDataStore = WKWebsiteDataStore(forIdentifier: account.id)
        config.preferences.javaScriptCanOpenWindowsAutomatically = true

        let webView = WKWebView(frame: .zero, configuration: config)
        webView.customUserAgent = userAgent
        webView.uiDelegate = self
        webView.allowsBackForwardNavigationGestures = true
        webView.load(URLRequest(url: URL(string: "https://claude.ai\(path)")!))

        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1100, height: 780),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered, defer: false)
        window.title = "\(account.name) — Claude"
        window.contentView = webView
        window.isReleasedWhenClosed = false
        window.setFrameAutosaveName("ChatWindow-\(account.id.uuidString)")
        if window.frame.origin == .zero { window.center() }
        window.delegate = self
        windows[account.id] = window
        window.makeKeyAndOrderFront(nil)
    }

    /// Opens the account's claude.ai window for sign-in. Looks each 4 seconds if the login is good.
    /// When it is good, closes the window and calls `onSignedIn`. Stops when you close the window
    /// or after 15 minutes.
    func openForSignIn(_ account: Account, onSignedIn: @escaping () -> Void) {
        open(account, path: "/login")
        signInWatchers[account.id]?.cancel()
        signInWatchers[account.id] = Task { [weak self] in
            for _ in 0..<225 {
                try? await Task.sleep(for: .seconds(4))
                guard !Task.isCancelled, let self, let window = self.windows[account.id], window.isVisible else { return }
                let (_, state) = await LiveUsage.shared.fetch(for: account)
                guard !Task.isCancelled else { return }
                if state == .ok {
                    self.signInWatchers[account.id] = nil
                    window.close()
                    // The next open starts with a fresh page, not the sign-in page.
                    self.windows[account.id] = nil
                    onSignedIn()
                    return
                }
            }
        }
    }

    func close(_ account: Account) {
        signInWatchers[account.id]?.cancel()
        signInWatchers[account.id] = nil
        windows[account.id]?.close()
        windows[account.id] = nil
    }

    /// Deletes the saved web login for an account that was removed.
    func forget(_ account: Account) {
        close(account)
        WKWebsiteDataStore.remove(forIdentifier: account.id) { _ in }
    }
}

extension ChatWindows: NSWindowDelegate {
    func windowWillClose(_ notification: Notification) {
        guard let window = notification.object as? NSWindow else { return }
        popups.removeAll { $0 === window }
        // Keep account windows in memory so they reopen fast with the same page.
    }
}

extension ChatWindows: WKUIDelegate {
    // Sign-in pop-ups (Google, SSO) need a real window that shares the same cookie store.
    func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration,
                 for navigationAction: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
        let popupView = WKWebView(frame: .zero, configuration: configuration)
        popupView.customUserAgent = userAgent
        popupView.uiDelegate = self
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 520, height: 680),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered, defer: false)
        window.title = "Sign in"
        window.contentView = popupView
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        window.makeKeyAndOrderFront(nil)
        popups.append(window)
        return popupView
    }

    func webViewDidClose(_ webView: WKWebView) {
        if let window = popups.first(where: { $0.contentView === webView }) {
            window.close()
        }
    }

    func webView(_ webView: WKWebView, runOpenPanelWith parameters: WKOpenPanelParameters,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping ([URL]?) -> Void) {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = parameters.allowsMultipleSelection
        panel.canChooseDirectories = parameters.allowsDirectories
        panel.begin { completionHandler($0 == .OK ? panel.urls : nil) }
    }

    func webView(_ webView: WKWebView, runJavaScriptAlertPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping () -> Void) {
        let alert = NSAlert()
        alert.messageText = message
        alert.runModal()
        completionHandler()
    }

    func webView(_ webView: WKWebView, runJavaScriptConfirmPanelWithMessage message: String,
                 initiatedByFrame frame: WKFrameInfo, completionHandler: @escaping (Bool) -> Void) {
        let alert = NSAlert()
        alert.messageText = message
        alert.addButton(withTitle: "OK")
        alert.addButton(withTitle: "Cancel")
        completionHandler(alert.runModal() == .alertFirstButtonReturn)
    }
}
