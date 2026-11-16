import AppKit
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

/// Makes the pictures for the README: `Clauds --render-demo <dir>`.
/// Uses fake accounts. Reads and writes no Claude data.
@MainActor
enum Demo {
    static let work = Account(id: UUID(uuidString: "00000000-0000-4000-8000-000000000001")!,
                              name: "Work", email: "you@company.example")
    static let personal = Account(id: UUID(uuidString: "02000000-0000-4000-8000-000000000002")!,
                                  name: "Personal", email: "you@home.example")
    static let client = Account(id: UUID(uuidString: "03000000-0000-4000-8000-000000000003")!,
                                name: "Client Project", email: "you@client.example")

    private static func usage(_ fiveHour: Int, _ week: Int, _ fable: Int, minutesLeft: Double) -> Usage {
        let weekReset = Date().addingTimeInterval(3.4 * 86400)
        return Usage(limits: [
            .init(label: "5h", fullName: "5-hour limit", percent: fiveHour,
                  resetsAt: Date().addingTimeInterval(minutesLeft * 60)),
            .init(label: "Week", fullName: "Weekly limit, all models", percent: week, resetsAt: weekReset),
            .init(label: "Fable wk", fullName: "Weekly limit, Fable only", percent: fable, resetsAt: weekReset),
        ], sampledAt: Date(), isLive: true)
    }

    private static func store(active: Account, sideBySide: Bool, running: [Account]) -> AccountStore {
        let store = AccountStore(demo: true)
        store.applyDemo(
            accounts: [work, personal, client], activeID: active.id, sideBySide: sideBySide,
            running: Set(running.map(\.id)),
            usage: [work.id: usage(72, 41, 18, minutesLeft: 64),
                    personal.id: usage(12, 6, 3, minutesLeft: 227),
                    client.id: usage(88, 63, 35, minutesLeft: 19)])
        return store
    }

    /// Draws the panel at 2x.
    private static func panel(_ store: AccountStore, dark: Bool) -> NSBitmapImageRep? {
        let appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
        let host = NSHostingView(rootView: MenuContent(store: store)
            .background(Color(nsColor: .windowBackgroundColor)))
        host.appearance = appearance
        let fitting = host.fittingSize
        host.frame = NSRect(x: 0, y: 0, width: fitting.width.rounded(.up), height: fitting.height.rounded(.up))
        let window = NSWindow(contentRect: host.frame, styleMask: [.borderless], backing: .buffered, defer: false)
        window.appearance = appearance
        window.contentView = host
        host.layoutSubtreeIfNeeded()
        let size = host.bounds.size
        guard let rep = NSBitmapImageRep(
            bitmapDataPlanes: nil, pixelsWide: Int(size.width * 2), pixelsHigh: Int(size.height * 2),
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
        rep.size = size
        host.cacheDisplay(in: host.bounds, to: rep)
        return rep
    }

    /// One picture: soft background, the panel with round corners and a shadow, a caption, and a pointer.
    /// `pointer` is in panel points from the top left of the panel.
    /// `clearBackground`: no backdrop, only the panel and its shadow (for the README screenshots).
    private static func scene(_ panel: NSBitmapImageRep, caption: String?, pointer: CGPoint?,
                              dark: Bool, canvas: CGSize, clearBackground: Bool = false) -> NSBitmapImageRep? {
        guard let rep = NSBitmapImageRep(
            bitmapDataPlanes: nil, pixelsWide: Int(canvas.width * 2), pixelsHigh: Int(canvas.height * 2),
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
        // Set the point size first, so that the context draws at 2x.
        rep.size = canvas
        guard let context = NSGraphicsContext(bitmapImageRep: rep) else { return nil }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = context
        defer { NSGraphicsContext.restoreGraphicsState() }

        let full = NSRect(origin: .zero, size: canvas)
        let colors = dark
            ? [NSColor(red: 0.16, green: 0.13, blue: 0.20, alpha: 1), NSColor(red: 0.07, green: 0.09, blue: 0.14, alpha: 1)]
            : [NSColor(red: 0.99, green: 0.93, blue: 0.86, alpha: 1), NSColor(red: 0.86, green: 0.90, blue: 0.98, alpha: 1)]
        if !clearBackground { NSGradient(colors: colors)?.draw(in: full, angle: -60) }

        let captionHeight: CGFloat = caption == nil ? 0 : 44
        if let caption {
            let style = NSMutableParagraphStyle()
            style.alignment = .center
            let text = NSAttributedString(string: caption, attributes: [
                .font: NSFont.systemFont(ofSize: 16, weight: .semibold),
                .foregroundColor: dark ? NSColor.white : NSColor(white: 0.12, alpha: 1),
                .paragraphStyle: style,
            ])
            text.draw(in: NSRect(x: 0, y: canvas.height - 46, width: canvas.width, height: 24))
        }

        let size = panel.size
        let origin = CGPoint(x: (canvas.width - size.width) / 2, y: canvas.height - captionHeight - 30 - size.height)
        let frame = NSRect(origin: origin, size: size)
        // The offscreen view leaves a thin empty band at its top and bottom. Cut it off.
        let shape = NSBezierPath(roundedRect: frame.insetBy(dx: 0, dy: 5), xRadius: 18, yRadius: 18)

        // Two shadows, as a real macOS window has: a wide soft one and a small close one.
        let fill = dark ? NSColor(white: 0.17, alpha: 1) : NSColor(white: 0.93, alpha: 1)
        for (blur, offset, alpha) in [(46.0, -18.0, dark ? 0.60 : 0.30), (10.0, -3.0, dark ? 0.45 : 0.18)] {
            NSGraphicsContext.saveGraphicsState()
            let shadow = NSShadow()
            shadow.shadowColor = NSColor.black.withAlphaComponent(alpha)
            shadow.shadowBlurRadius = blur
            shadow.shadowOffset = NSSize(width: 0, height: offset)
            shadow.set()
            fill.setFill()
            shape.fill()
            NSGraphicsContext.restoreGraphicsState()
        }

        NSGraphicsContext.saveGraphicsState()
        shape.addClip()
        panel.draw(in: frame)
        NSGraphicsContext.restoreGraphicsState()
        if let pointer {
            let image = NSCursor.arrow.image
            let spot = NSCursor.arrow.hotSpot
            let scale: CGFloat = 1.4
            let w = image.size.width * scale, h = image.size.height * scale
            image.draw(in: NSRect(x: origin.x + pointer.x - spot.x * scale,
                                  y: origin.y + size.height - pointer.y - h + spot.y * scale,
                                  width: w, height: h))
        }
        return rep
    }

    private static func write(_ rep: NSBitmapImageRep?, to url: URL) throws {
        guard let data = rep?.representation(using: .png, properties: [:]) else {
            throw SwitchError.message("Could not draw \(url.lastPathComponent)")
        }
        try data.write(to: url)
    }

    static func renderAll(to dir: URL) throws {
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let restartWork = store(active: work, sideBySide: false, running: [])
        let restartPersonal = store(active: personal, sideBySide: false, running: [])
        let sideBySide = store(active: personal, sideBySide: true, running: [personal, work])

        // Screenshots.
        for dark in [true, false] {
            let suffix = dark ? "dark" : "light"
            let shot = CGSize(width: 450, height: 590)
            if let image = panel(restartWork, dark: dark) {
                try write(scene(image, caption: nil, pointer: nil, dark: dark, canvas: shot, clearBackground: true),
                          to: dir.appendingPathComponent("panel-\(suffix).png"))
            }
            if let image = panel(sideBySide, dark: dark) {
                try write(scene(image, caption: nil, pointer: nil, dark: dark, canvas: shot, clearBackground: true),
                          to: dir.appendingPathComponent("side-by-side-\(suffix).png"))
            }
        }

        // Check picture for the loading state: `CS_LOADING_CHECK=1 Clauds --render-demo <dir>`.
        if ProcessInfo.processInfo.environment["CS_LOADING_CHECK"] != nil {
            let loading = AccountStore(demo: true)
            let old = Usage(limits: [.init(label: "5h", fullName: "5-hour limit", percent: 40, resetsAt: nil),
                                     .init(label: "Week", fullName: "Weekly limit, all models", percent: 22, resetsAt: nil)],
                            sampledAt: Date().addingTimeInterval(-1500), isLive: false)
            loading.applyDemo(accounts: [work, personal, client], activeID: work.id, sideBySide: false, running: [],
                              usage: [personal.id: old, client.id: usage(88, 63, 35, minutesLeft: 19)],
                              loading: [work.id, personal.id])
            let failed = AccountStore(demo: true)
            failed.applyDemo(accounts: [work, personal, client], activeID: work.id, sideBySide: false, running: [],
                             usage: [personal.id: old],
                             states: [work.id: .failed("claude.ai did not load"), personal.id: .failed("claude.ai answered 500 (usage)"),
                                      client.id: .needsSignIn])
            if let image = panel(failed, dark: false) {
                try write(scene(image, caption: nil, pointer: nil, dark: false, canvas: CGSize(width: 450, height: 640),
                                clearBackground: true), to: dir.appendingPathComponent("failed-check.png"))
            }
            if let image = panel(loading, dark: true) {
                try write(scene(image, caption: nil, pointer: nil, dark: true, canvas: CGSize(width: 450, height: 590),
                                clearBackground: true), to: dir.appendingPathComponent("loading-check.png"))
            }
            return
        }

        // GIF: a short tour.
        let canvas = CGSize(width: 450, height: 630)
        let steps: [(AccountStore, String, CGPoint?, Double)] = [
            (restartWork, "All your Claude accounts in the menu bar", nil, 2.2),
            (restartWork, "See each limit and when it resets", CGPoint(x: 190, y: 106), 2.4),
            (restartWork, "Click an account to switch", CGPoint(x: 150, y: 192), 1.8),
            (restartPersonal, "Claude opens again, signed in", CGPoint(x: 150, y: 192), 2.2),
            (restartPersonal, "Or turn on Side by Side", CGPoint(x: 312, y: 418), 1.8),
            (sideBySide, "All accounts open at the same time", CGPoint(x: 312, y: 418), 2.8),
        ]
        let url = dir.appendingPathComponent("demo.gif")
        guard let gif = CGImageDestinationCreateWithURL(url as CFURL, UTType.gif.identifier as CFString, steps.count, nil) else {
            throw SwitchError.message("Could not make demo.gif")
        }
        CGImageDestinationSetProperties(gif, [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFLoopCount: 0]] as CFDictionary)
        for step in steps {
            guard let image = panel(step.0, dark: true),
                  let frame = scene(image, caption: step.1, pointer: step.2, dark: true, canvas: canvas),
                  let cg = frame.cgImage else { continue }
            CGImageDestinationAddImage(gif, cg, [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFDelayTime: step.3]] as CFDictionary)
        }
        guard CGImageDestinationFinalize(gif) else { throw SwitchError.message("Could not save demo.gif") }
    }
}
