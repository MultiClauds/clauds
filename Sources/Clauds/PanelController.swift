import AppKit
import Combine
import SwiftUI

/// A window with no title bar that can take the keyboard (for switches and menus).
private final class KeyPanel: NSPanel {
    override var canBecomeKey: Bool { true }
}

/// The menu bar icon and the panel below it.
///
/// The app makes its own panel window. This gives full control of the shape: a clear window,
/// a round panel, and the shadow that macOS makes from that round shape.
@MainActor
final class PanelController: NSObject, NSWindowDelegate {
    nonisolated static let showRequest = Notification.Name("io.github.codefi.clauds.show-panel")
    let store = AccountStore()

    private var statusItem: NSStatusItem?
    private var panel: KeyPanel?
    private var watcher: AnyCancellable?
    private var clickMonitor: Any?
    /// Space between the menu bar and the panel.
    private let gap: CGFloat = 6

    func start() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        item.button?.image = IconArt.menuBarImage
        item.button?.target = self
        item.button?.action = #selector(toggle)
        statusItem = item
        // Check hook: `Clauds --show-panel` asks the running app to open its panel.
        DistributedNotificationCenter.default().addObserver(
            forName: PanelController.showRequest, object: nil, queue: .main
        ) { [weak self] _ in
            Task { @MainActor in if self?.panel == nil { self?.show() } }
        }
        watcher = store.$isSwitching.sink { [weak item] switching in
            item?.button?.alphaValue = switching ? 0.5 : 1
        }
    }

    @objc private func toggle() {
        if panel != nil { close() } else { show() }
    }

    private func show() {
        guard let button = statusItem?.button, let anchor = button.window?.frame else { return }
        let content = MenuContent(store: store)
            .modifier(PanelChrome())
            .background(GeometryReader { proxy in
                Color.clear
                    .onAppear { self.resize(to: proxy.size) }
                    .onChange(of: proxy.size) { _, size in self.resize(to: size) }
            })
        let host = NSHostingView(rootView: content)
        // Measure first. With no sizing options, the view reports a size of zero, and the panel
        // would stay invisible until its content changed.
        var size = host.fittingSize
        if size.width < 1 || size.height < 1 {
            host.layoutSubtreeIfNeeded()
            size = host.intrinsicContentSize
        }
        host.sizingOptions = []

        let panel = KeyPanel(contentRect: NSRect(origin: .zero, size: size),
                             styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        // The panel takes the keyboard, but the app does not become the active app. This is how
        // menu bar panels work. It also means that no app switch can close the panel by accident.
        panel.becomesKeyOnlyIfNeeded = false
        panel.hidesOnDeactivate = false
        panel.isOpaque = false
        panel.backgroundColor = .clear
        // The panel draws its own shadow (see PanelChrome).
        panel.hasShadow = false
        panel.level = .statusBar
        panel.collectionBehavior = [.moveToActiveSpace, .fullScreenAuxiliary]
        panel.isReleasedWhenClosed = false
        panel.contentView = host
        panel.delegate = self
        self.panel = panel
        place(panel, size: size, below: anchor)

        panel.makeKeyAndOrderFront(nil)
        button.highlight(true)

        // A click in a different app closes the panel.
        // A click on the menu bar icon also comes here, because the app is not the active app.
        // That click is for `toggle`. If this monitor also closed the panel, the panel would close
        // and open again.
        clickMonitor = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                if let icon = self.statusItem?.button?.window?.frame, icon.contains(NSEvent.mouseLocation) { return }
                self.close()
            }
        }
    }

    private func close() {
        if let clickMonitor { NSEvent.removeMonitor(clickMonitor) }
        clickMonitor = nil
        statusItem?.button?.highlight(false)
        let old = panel
        panel = nil
        old?.delegate = nil
        old?.orderOut(nil)
    }

    /// Puts the panel below the menu bar icon, inside the screen.
    private func place(_ panel: NSPanel, size: CGSize, below anchor: NSRect) {
        let screen = NSScreen.screens.first { $0.frame.intersects(anchor) }?.visibleFrame
            ?? NSScreen.main?.visibleFrame ?? .zero
        // `size` includes the clear shadow margin on each side. Place the visible panel, not the window.
        let margin = PanelChrome.margin
        let visibleWidth = size.width - 2 * margin
        var x = anchor.midX - visibleWidth / 2
        x = min(max(x, screen.minX + 8), screen.maxX - visibleWidth - 8)
        let y = anchor.minY - gap - size.height + margin
        panel.setFrame(NSRect(x: x - margin, y: y, width: size.width, height: size.height), display: true)
    }

    /// The content changed its size (for example a new account). The top edge stays where it is.
    private func resize(to size: CGSize) {
        guard let panel, size.width > 0, size.height > 0, panel.frame.size != size,
              let anchor = statusItem?.button?.window?.frame else { return }
        place(panel, size: size, below: anchor)
    }

    // A dialog, a sign-in window, or a different app took the keyboard: close the panel.
    func windowDidResignKey(_ notification: Notification) {
        close()
    }
}
