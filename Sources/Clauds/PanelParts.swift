import SwiftUI

/// Small grey title above a group of cards.
struct SectionTitle: View {
    let text: String
    init(_ text: String) { self.text = text }

    var body: some View {
        Text(text)
            .font(.system(size: 10.5, weight: .semibold))
            .kerning(0.6)
            .foregroundStyle(.secondary)
            .padding(.leading, 4)
            .padding(.top, 2)
    }
}

/// White rounded card that holds one or more account rows.
struct CardGroup<Content: View>: View {
    @ViewBuilder var content: Content

    var body: some View {
        VStack(spacing: 0) { content }
            .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 12))
            .overlay(RoundedRectangle(cornerRadius: 12).strokeBorder(Color.primary.opacity(0.10)))
            .shadow(color: .black.opacity(0.05), radius: 2, y: 1)
    }
}

/// Colored square with the first letter of the account name.
struct Avatar: View {
    let account: Account

    private static let palette: [Color] = [
        Color(red: 0.78, green: 0.25, blue: 0.10),   // rust
        Color(red: 0.13, green: 0.55, blue: 0.24),   // green
        Color(red: 0.11, green: 0.38, blue: 0.85),   // blue
        Color(red: 0.49, green: 0.16, blue: 0.80),   // purple
        Color(red: 0.80, green: 0.50, blue: 0.05),   // amber
        Color(red: 0.05, green: 0.52, blue: 0.56),   // teal
        Color(red: 0.78, green: 0.16, blue: 0.42),   // pink
    ]

    var body: some View {
        // The color comes from the account id, so it stays the same after a rename.
        let index = Int(account.id.uuid.0) % Self.palette.count
        let letter = account.name.first(where: \.isLetter).map { String($0).uppercased() } ?? "?"
        RoundedRectangle(cornerRadius: 9)
            .fill(Self.palette[index].gradient)
            .frame(width: 36, height: 36)
            .overlay(
                Text(letter)
                    .font(.system(size: 19, weight: .bold, design: .serif))
                    .foregroundStyle(.white))
    }
}

/// One account: avatar, name, email, and one meter per plan limit.
/// A click on the row selects the account.
struct AccountRow<MenuItems: View>: View {
    let account: Account
    let usage: Usage?
    let isOpen: Bool
    let needsSignIn: Bool
    /// A live usage read is in progress for this account.
    var isLoading = false
    /// The last live read failed. A short text that says why, in plain words.
    var failure: String? = nil
    let disabled: Bool
    let onSelect: () -> Void
    @ViewBuilder var menuItems: MenuItems

    @State private var hovering = false

    var body: some View {
        HStack(alignment: .top, spacing: 6) {
            Button(action: onSelect) {
                VStack(alignment: .leading, spacing: 8) {
                    HStack(spacing: 10) {
                        Avatar(account: account)
                            .overlay(alignment: .bottomTrailing) {
                                if isOpen {
                                    Circle().fill(.green)
                                        .frame(width: 9, height: 9)
                                        .overlay(Circle().stroke(Color(nsColor: .controlBackgroundColor), lineWidth: 2))
                                        .offset(x: 3, y: 3)
                                }
                            }
                        VStack(alignment: .leading, spacing: 2) {
                            Text(account.name)
                                .font(.system(size: 13, weight: .semibold))
                                .lineLimit(1)
                            if let email = account.email, !email.isEmpty, email != account.name {
                                Text(email)
                                    .font(.system(size: 11))
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }
                        }
                        Spacer(minLength: 0)
                    }
                    meters
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .disabled(disabled)

            Menu { menuItems } label: {
                Image(systemName: "ellipsis")
                    .frame(width: 18, height: 30)
                    .contentShape(Rectangle())
            }
            .menuStyle(.borderlessButton)
            .menuIndicator(.hidden)
            .fixedSize()
            .foregroundStyle(.secondary)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 10)
        .background(hovering ? Color.primary.opacity(0.05) : .clear, in: RoundedRectangle(cornerRadius: 10))
        .onHover { hovering = $0 }
    }

    @ViewBuilder private var meters: some View {
        VStack(alignment: .leading, spacing: 6) {
            if let usage, !usage.limits.isEmpty {
                HStack(alignment: .top, spacing: 12) {
                    ForEach(usage.limits) { limit in
                        LimitMeter(limit: limit)
                    }
                }
            } else if isLoading {
                // Same height as the meters, so that the panel does not jump when the numbers come in.
                HStack(spacing: 6) {
                    ProgressView().controlSize(.small)
                    Text("Loading usage…")
                        .font(.system(size: 10.5))
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, minHeight: 38, alignment: .leading)
            }
            notice
        }
    }

    /// One line below the meters: loading, a problem, or where old numbers come from.
    @ViewBuilder private var notice: some View {
        let hasNumbers = !(usage?.limits.isEmpty ?? true)
        let old = usage.map { "Numbers from \($0.age)." } ?? ""
        if isLoading {
            if hasNumbers, usage?.isLive == false {
                HStack(spacing: 5) {
                    ProgressView().controlSize(.mini)
                    Text("Old numbers (\(usage?.age ?? "")). Loading new numbers…")
                        .font(.system(size: 10))
                        .foregroundStyle(.secondary)
                }
            }
        } else if let failure {
            Notice(icon: "wifi.exclamationmark", tint: .orange,
                   title: failure,
                   detail: hasNumbers ? "\(old) The app tries again in 2 minutes."
                                      : "The app tries again in 2 minutes.")
        } else if needsSignIn {
            Notice(icon: "person.crop.circle.badge.exclamationmark", tint: .blue,
                   title: "Sign in for live usage",
                   detail: hasNumbers ? "\(old) Open the … menu on this card." : "Open the … menu on this card.")
        } else if hasNumbers, usage?.isLive == false {
            Text("From the Claude app's file, \(usage?.age ?? "")")
                .font(.system(size: 10))
                .foregroundStyle(.secondary)
        } else if !hasNumbers {
            Text("No usage numbers yet")
                .font(.system(size: 10.5))
                .foregroundStyle(.secondary)
        }
    }
}

/// A small, soft colored box with an icon: a problem that is not an alarm.
struct Notice: View {
    let icon: String
    let tint: Color
    let title: String
    let detail: String

    var body: some View {
        HStack(alignment: .center, spacing: 8) {
            Image(systemName: icon)
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(tint)
                .frame(width: 18)
            VStack(alignment: .leading, spacing: 1) {
                Text(title)
                    .font(.system(size: 11, weight: .semibold))
                Text(detail)
                    .font(.system(size: 10))
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 9)
        .padding(.vertical, 6)
        .background(tint.opacity(0.13), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
    }
}

/// One plan limit: name, percent used, bar, and when it resets.
struct LimitMeter: View {
    let limit: Usage.Limit

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(alignment: .firstTextBaseline, spacing: 4) {
                Text(limit.label)
                    .font(.system(size: 10.5, weight: .medium))
                    .foregroundStyle(.secondary)
                Spacer(minLength: 0)
                Text("\(limit.currentPercent)%")
                    .font(.system(size: 11.5, weight: .semibold))
                    .foregroundStyle(Usage.color(for: limit.currentPercent))
            }
            ProgressView(value: Double(min(limit.currentPercent, 100)), total: 100)
                .progressViewStyle(.linear)
                .tint(Usage.color(for: limit.currentPercent))
                .controlSize(.small)
            Text(limit.resetText)
                .font(.system(size: 9.5))
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .help(limit.tip)
    }
}

/// Plan usage of one account.
struct Usage: Equatable {
    /// One limit of the plan.
    struct Limit: Equatable, Identifiable {
        /// "5h", "Week", "Fable wk".
        var label: String
        /// Full name for the mouse-over tip: "5-hour limit", "Weekly limit, all models", "Weekly limit, Fable".
        var fullName: String
        var percent: Int
        var resetsAt: Date?

        var id: String { label }

        /// After the reset time, the window is empty.
        var currentPercent: Int {
            if let resetsAt, Date() >= resetsAt { return 0 }
            return percent
        }

        /// "1h 05m left" (less than a day), "Thu 13:59" (later), or "" when the reset time is not known.
        var resetText: String {
            guard let resetsAt else { return " " }
            let minutes = Int(resetsAt.timeIntervalSinceNow / 60)
            if minutes <= 0 { return "reset" }
            if minutes < 60 { return "\(minutes)m left" }
            if minutes < 24 * 60 { return "\(minutes / 60)h \(String(format: "%02d", minutes % 60))m left" }
            return resetsAt.formatted(.dateTime.weekday(.abbreviated).hour().minute())
        }

        var tip: String {
            var text = "\(fullName): \(currentPercent)% used"
            if let resetsAt {
                text += "\nResets: " + resetsAt.formatted(date: .abbreviated, time: .shortened)
            }
            return text
        }
    }

    /// In display order: 5-hour, weekly (all models), then weekly limits for one model.
    var limits: [Limit]
    var sampledAt: Date
    /// True: live from claude.ai. False: from the file that the Claude app saves on this Mac.
    var isLive = false

    /// "now", "12m ago", "3h ago", "2d ago".
    var age: String {
        let seconds = max(0, Date().timeIntervalSince(sampledAt))
        switch seconds {
        case ..<90: return "now"
        case ..<3600: return "\(Int(seconds / 60))m ago"
        case ..<(48 * 3600): return "\(Int(seconds / 3600))h ago"
        default: return "\(Int(seconds / 86400))d ago"
        }
    }

    static func color(for percent: Int?) -> Color {
        guard let percent else { return .secondary }
        switch percent {
        case ..<60: return Color(red: 0.10, green: 0.58, blue: 0.22)
        case ..<85: return .orange
        default: return .red
        }
    }

    /// Reads the newest sample from `<Claude data>/plan-usage-history.json`.
    /// That file has percent numbers only: 5-hour and weekly (all models). No reset times.
    static func read(dataDir: URL) -> Usage? {
        struct File: Decodable { var samples: [Sample] }
        struct Sample: Decodable { var t: Double; var u: [String: Double] }
        let file = dataDir.appendingPathComponent("plan-usage-history.json")
        guard let data = try? Data(contentsOf: file),
              let history = try? JSONDecoder().decode(File.self, from: data),
              let last = history.samples.max(by: { $0.t < $1.t }) else { return nil }
        let sampledAt = Date(timeIntervalSince1970: last.t / 1000)
        var limits: [Limit] = []
        // The 5-hour number is only true while the sample is younger than 5 hours.
        if Date().timeIntervalSince(sampledAt) < 5 * 3600, let fiveHour = last.u["fh"] {
            limits.append(Limit(label: "5h", fullName: "5-hour limit", percent: Int(fiveHour.rounded()), resetsAt: nil))
        }
        if let week = last.u["sd"] {
            limits.append(Limit(label: "Week", fullName: "Weekly limit, all models", percent: Int(week.rounded()), resetsAt: nil))
        }
        return Usage(limits: limits, sampledAt: sampledAt, isLive: false)
    }
}

/// The look of the menu bar panel: solid background, round corners, and a soft shadow. No edge line.
///
/// The panel draws its own shadow. The shadow that macOS gives a window comes with a thin edge
/// line, so the window shadow is off, and the window is larger than the panel by `margin` on
/// each side to give the shadow room.
struct PanelChrome: ViewModifier {
    static let radius: CGFloat = 18
    /// Clear space around the panel for the shadow.
    static let margin: CGFloat = 44

    func body(content: Content) -> some View {
        let shape = RoundedRectangle(cornerRadius: Self.radius, style: .continuous)
        content
            .background(Color(nsColor: .windowBackgroundColor))
            .clipShape(shape)
            .background(
                shape.fill(Color(nsColor: .windowBackgroundColor))
                    .shadow(color: .black.opacity(0.30), radius: 20, y: 10)
                    .shadow(color: .black.opacity(0.18), radius: 4, y: 2))
            .padding(Self.margin)
    }
}
