import Foundation

/// Auto-switch: when the current account is near a plan limit, go to the account with the most room.
/// Only for the restart mode. In Side by Side all accounts are open, so there is nothing to switch.
enum AutoSwitch {
    enum Mode: String, Codable, CaseIterable, Identifiable {
        /// Never switch by itself.
        case off
        /// Show a question first. You say yes or no.
        case ask
        /// Switch without a question. Claude restarts by itself.
        case auto

        var id: String { rawValue }
        var title: String {
            switch self {
            case .off: return "Off"
            case .ask: return "Ask Me First"
            case .auto: return "Switch Automatically"
            }
        }
    }

    /// The current account is "near a limit" at this percent or more.
    static let threshold = 90
    /// The other account must be this many points below the threshold. This prevents a switch back and forth.
    static let margin = 10
    /// Numbers older than this are not used for a decision.
    static let maxAge: TimeInterval = 15 * 60
    /// Time between two questions or two automatic switches.
    static let pause: TimeInterval = 20 * 60

    struct Proposal: Equatable {
        var target: Account
        /// The limit of the current account that is near its end.
        var limit: Usage.Limit
        /// The fullest limit of the target account, in percent.
        var targetPercent: Int
    }

    /// The fullest limit of an account. All limits count: 5-hour, weekly, and weekly for one model.
    static func fullest(_ usage: Usage) -> Usage.Limit? {
        usage.limits.max { $0.currentPercent < $1.currentPercent }
    }

    /// The switch to make now, or nil when no switch is necessary or no account has enough room.
    /// Uses only new numbers. For the other accounts, only live numbers from claude.ai.
    static func proposal(accounts: [Account], activeID: UUID?, usage: [UUID: Usage],
                         now: Date = Date()) -> Proposal? {
        func isNew(_ value: Usage) -> Bool { now.timeIntervalSince(value.sampledAt) < maxAge }
        guard let activeID,
              let mine = usage[activeID], isNew(mine),
              let limit = fullest(mine), limit.currentPercent >= threshold else { return nil }

        let best = accounts
            .filter { $0.id != activeID }
            .compactMap { account -> (Account, Int)? in
                guard let theirs = usage[account.id], theirs.isLive, isNew(theirs),
                      let top = fullest(theirs) else { return nil }
                return (account, top.currentPercent)
            }
            .filter { $0.1 <= threshold - margin }
            .min { $0.1 < $1.1 }
        guard let best else { return nil }
        return Proposal(target: best.0, limit: limit, targetPercent: best.1)
    }

    /// Check hook: Clauds --auto-switch-check. Runs the decision on fake numbers. Prints "ok" or the failures.
    static func selfCheck() -> [String] {
        let a = Account(id: UUID(), name: "A"), b = Account(id: UUID(), name: "B"), c = Account(id: UUID(), name: "C")
        let now = Date()
        func use(_ five: Int, _ week: Int, live: Bool = true, age: TimeInterval = 60) -> Usage {
            Usage(limits: [
                .init(label: "5h", fullName: "5-hour limit", percent: five, resetsAt: now.addingTimeInterval(3600)),
                .init(label: "Week", fullName: "Weekly limit, all models", percent: week, resetsAt: now.addingTimeInterval(86400)),
            ], sampledAt: now.addingTimeInterval(-age), isLive: live)
        }
        func run(_ usage: [UUID: Usage]) -> Proposal? {
            proposal(accounts: [a, b, c], activeID: a.id, usage: usage, now: now)
        }
        var failures: [String] = []
        func expect(_ name: String, _ ok: Bool) { if !ok { failures.append(name) } }

        expect("below the threshold: no switch", run([a.id: use(89, 10), b.id: use(5, 5)]) == nil)
        expect("5-hour limit full: go to the emptiest", run([a.id: use(92, 10), b.id: use(40, 30), c.id: use(10, 20)])?.target == c)
        expect("weekly limit full counts too", run([a.id: use(10, 95), b.id: use(5, 5)])?.target == b)
        expect("the weekly limit of the target counts", run([a.id: use(95, 10), b.id: use(5, 85)]) == nil)
        expect("target too full: no switch", run([a.id: use(95, 10), b.id: use(81, 10)]) == nil)
        expect("target at the margin: switch", run([a.id: use(95, 10), b.id: use(80, 10)])?.target == b)
        expect("old numbers of the target: no switch", run([a.id: use(95, 10), b.id: use(5, 5, age: 3600)]) == nil)
        expect("target numbers not live: no switch", run([a.id: use(95, 10), b.id: use(5, 5, live: false)]) == nil)
        expect("old numbers of the current account: no switch", run([a.id: use(95, 10, age: 3600), b.id: use(5, 5)]) == nil)
        expect("no numbers for the target: no switch", run([a.id: use(95, 10)]) == nil)
        // A limit after its reset time is empty.
        var past = use(99, 10)
        past.limits[0].resetsAt = now.addingTimeInterval(-60)
        expect("limit after its reset: no switch", run([a.id: past, b.id: use(5, 5)]) == nil)
        return failures
    }
}
