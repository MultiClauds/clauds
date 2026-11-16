import AppKit
import WebKit

/// Reads the live plan usage (percent and reset time) of an account from claude.ai.
///
/// How: one hidden web view per account. It uses the same private cookie store as the account's
/// chat window in this app, so it is signed in when that window is signed in. It loads one very
/// small claude.ai page, then asks claude.ai for the usage numbers, as the website itself does.
///
/// - The only host it talks to is claude.ai.
/// - It does not read or store passwords or tokens. The web view keeps the login, as a browser does.
/// - This is not an official interface. If it fails, the panel falls back to the local numbers.
@MainActor
final class LiveUsage: NSObject {
    static let shared = LiveUsage()

    enum State: Equatable {
        case ok
        case needsSignIn
        case failed(String)

        /// Plain words for the panel, when the read failed.
        var failureText: String? {
            guard case .failed(let reason) = self else { return nil }
            if reason.contains("did not load") || reason.localizedCaseInsensitiveContains("timed out")
                || reason.localizedCaseInsensitiveContains("offline") || reason.contains("answered -1") {
                return "No connection to claude.ai"
            }
            if reason.contains("format changed") || reason.contains("unexpected answer") {
                return "claude.ai gave an answer that the app does not know"
            }
            return "Could not load new usage numbers"
        }
    }

    /// A tiny page on claude.ai. The usage request must come from a claude.ai page.
    private let basePage = URL(string: "https://claude.ai/robots.txt")!
    private let userAgent =
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15"

    private var views: [UUID: WKWebView] = [:]
    private var loadWaiters: [ObjectIdentifier: CheckedContinuation<Bool, Never>] = [:]
    /// Last raw answer per account, for problem finding. Private file, usage numbers only.
    var debugFile: URL?

    /// Each request stops after 15 seconds. A slow or dead network cannot keep a read open forever.
    private let script = """
        try {
          let org = null;
          const cookie = document.cookie.split('; ').find(c => c.startsWith('lastActiveOrg='));
          if (cookie) org = cookie.split('=')[1];
          if (!org) {
            const r = await fetch('/api/organizations', { credentials: 'include', signal: AbortSignal.timeout(15000) });
            if (!r.ok) return JSON.stringify({ status: r.status, step: 'organizations', body: (await r.text()).slice(0, 300) });
            const list = await r.json();
            org = (list.find(o => (o.capabilities || []).includes('chat')) || list[0] || {}).uuid;
          }
          if (!org) return JSON.stringify({ status: 0, step: 'no organization' });
          const r = await fetch('/api/organizations/' + org + '/usage', { credentials: 'include', signal: AbortSignal.timeout(15000) });
          if (!r.ok) return JSON.stringify({ status: r.status, step: 'usage', body: (await r.text()).slice(0, 300) });
          return JSON.stringify({ status: 200, usage: await r.json() });
        } catch (e) {
          return JSON.stringify({ status: -1, step: String(e) });
        }
        """

    func fetch(for account: Account) async -> (Usage?, State) {
        let webView: WKWebView
        if let existing = views[account.id] {
            webView = existing
        } else {
            let config = WKWebViewConfiguration()
            config.websiteDataStore = WKWebsiteDataStore(forIdentifier: account.id)
            webView = WKWebView(frame: NSRect(x: 0, y: 0, width: 200, height: 200), configuration: config)
            webView.customUserAgent = userAgent
            webView.navigationDelegate = self
            views[account.id] = webView
        }

        if webView.url?.host != "claude.ai" {
            guard await load(basePage, in: webView) else { return (nil, .failed("claude.ai did not load")) }
        }

        let raw: String
        do {
            let result = try await webView.callAsyncJavaScript(script, arguments: [:], in: nil, contentWorld: .defaultClient)
            raw = result as? String ?? ""
        } catch {
            return (nil, .failed(error.localizedDescription))
        }
        saveDebug(raw, for: account)

        guard let data = raw.data(using: .utf8),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let status = object["status"] as? Int else { return (nil, .failed("unexpected answer")) }
        if status == 401 || status == 403 { return (nil, .needsSignIn) }
        guard status == 200, let usage = object["usage"] as? [String: Any] else {
            return (nil, .failed("claude.ai answered \(status) (\(object["step"] as? String ?? ""))"))
        }
        guard let parsed = Self.parse(usage) else { return (nil, .failed("usage format changed")) }
        return (parsed, .ok)
    }

    /// Forgets the hidden web view of an account (after the account is removed).
    func forget(_ account: Account) {
        views[account.id] = nil
    }

    /// Reads the `limits` list of the answer:
    /// `{"kind": "session" | "weekly_all" | "weekly_scoped", "percent": 18, "resets_at": "...", "scope": {"model": {"display_name": "Fable"}}}`.
    /// Falls back to the older `five_hour` / `seven_day` fields.
    static func parse(_ usage: [String: Any]) -> Usage? {
        var limits: [Usage.Limit] = []
        for entry in usage["limits"] as? [[String: Any]] ?? [] {
            guard let kind = entry["kind"] as? String,
                  let percent = (entry["percent"] as? NSNumber)?.doubleValue else { continue }
            let resetsAt = (entry["resets_at"] as? String).flatMap(date)
            let model = ((entry["scope"] as? [String: Any])?["model"] as? [String: Any])?["display_name"] as? String
            let label: String, fullName: String
            switch kind {
            case "session": (label, fullName) = ("5h", "5-hour limit")
            case "weekly_all": (label, fullName) = ("Week", "Weekly limit, all models")
            case "weekly_scoped":
                guard let model else { continue }
                (label, fullName) = ("\(model) wk", "Weekly limit, \(model) only")
            default: continue
            }
            guard !limits.contains(where: { $0.label == label }) else { continue }
            limits.append(Usage.Limit(label: label, fullName: fullName, percent: Int(percent.rounded()), resetsAt: resetsAt))
        }
        if limits.isEmpty {
            func window(_ key: String, _ label: String, _ fullName: String) {
                guard let entry = usage[key] as? [String: Any] else { return }
                let percent = (entry["utilization"] as? NSNumber)?.doubleValue ?? 0
                limits.append(Usage.Limit(label: label, fullName: fullName, percent: Int(percent.rounded()),
                                          resetsAt: (entry["resets_at"] as? String).flatMap(date)))
            }
            window("five_hour", "5h", "5-hour limit")
            window("seven_day", "Week", "Weekly limit, all models")
        }
        guard !limits.isEmpty else { return nil }
        let order = ["5h": 0, "Week": 1]
        limits.sort { (order[$0.label] ?? 2) < (order[$1.label] ?? 2) }
        return Usage(limits: Array(limits.prefix(3)), sampledAt: Date(), isLive: true)
    }

    /// Build check only: reads the saved raw answers, so that the preview picture shows live numbers.
    static func savedAnswers(in file: URL) -> [UUID: Usage] {
        guard let data = try? Data(contentsOf: file),
              let all = try? JSONSerialization.jsonObject(with: data) as? [String: [String: Any]] else { return [:] }
        var result: [UUID: Usage] = [:]
        for (key, value) in all {
            guard let id = UUID(uuidString: key), let raw = (value["raw"] as? String)?.data(using: .utf8),
                  let object = try? JSONSerialization.jsonObject(with: raw) as? [String: Any],
                  let usage = object["usage"] as? [String: Any], let parsed = parse(usage) else { continue }
            result[id] = parsed
        }
        return result
    }

    private static func date(_ text: String) -> Date? {
        let withFraction = ISO8601DateFormatter()
        withFraction.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        if let date = withFraction.date(from: text) { return date }
        // Drop the fraction (for example ".634895") and try again.
        let plain = text.replacingOccurrences(of: #"\.\d+"#, with: "", options: .regularExpression)
        return ISO8601DateFormatter().date(from: plain)
    }

    private func saveDebug(_ raw: String, for account: Account) {
        guard let debugFile else { return }
        var all = (try? Data(contentsOf: debugFile))
            .flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] } ?? [:]
        all[account.id.uuidString] = ["at": Int(Date().timeIntervalSince1970), "raw": String(raw.prefix(4000))]
        if let data = try? JSONSerialization.data(withJSONObject: all, options: [.prettyPrinted, .sortedKeys]) {
            try? data.write(to: debugFile, options: .atomic)
            try? FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: debugFile.path)
        }
    }

    /// Loads a page. Returns false on failure or after 20 seconds.
    private func load(_ url: URL, in webView: WKWebView) async -> Bool {
        let key = ObjectIdentifier(webView)
        return await withCheckedContinuation { continuation in
            loadWaiters[key] = continuation
            webView.load(URLRequest(url: url))
            Task { [weak self] in
                try? await Task.sleep(for: .seconds(20))
                self?.finishLoad(key, success: false)
            }
        }
    }

    private func finishLoad(_ key: ObjectIdentifier, success: Bool) {
        loadWaiters.removeValue(forKey: key)?.resume(returning: success)
    }
}

extension LiveUsage: WKNavigationDelegate {
    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        finishLoad(ObjectIdentifier(webView), success: true)
    }

    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        finishLoad(ObjectIdentifier(webView), success: false)
    }

    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        finishLoad(ObjectIdentifier(webView), success: false)
    }
}
