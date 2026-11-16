import AppKit

/// The app's own icon art, drawn in code.
/// Glyph: two arrows that go round in a circle (switch) with a spark in the middle (Claude).
enum IconArt {
    /// Draws the glyph. The design grid is 18 x 18 units.
    static func drawGlyph(in rect: NSRect, color: NSColor) {
        let s = rect.width / 18
        let center = NSPoint(x: rect.midX, y: rect.midY)
        func point(_ degrees: CGFloat, _ radius: CGFloat) -> NSPoint {
            let a = degrees * .pi / 180
            return NSPoint(x: center.x + cos(a) * radius, y: center.y + sin(a) * radius)
        }
        color.set()

        // Two clockwise arrows.
        let ringRadius = 6.2 * s
        for start in [CGFloat(168), CGFloat(348)] {
            let end = start - 112
            let arc = NSBezierPath()
            arc.appendArc(withCenter: center, radius: ringRadius, startAngle: start, endAngle: end, clockwise: true)
            arc.lineWidth = 1.5 * s
            arc.lineCapStyle = .round
            arc.stroke()

            let a = end * .pi / 180
            let base = point(end, ringRadius)
            let tangent = NSPoint(x: sin(a), y: -cos(a))   // clockwise direction
            let radial = NSPoint(x: cos(a), y: sin(a))
            let head = NSBezierPath()
            head.move(to: NSPoint(x: base.x + tangent.x * 3.1 * s, y: base.y + tangent.y * 3.1 * s))
            head.line(to: NSPoint(x: base.x + radial.x * 2.3 * s, y: base.y + radial.y * 2.3 * s))
            head.line(to: NSPoint(x: base.x - radial.x * 2.3 * s, y: base.y - radial.y * 2.3 * s))
            head.close()
            head.fill()
        }

        // Spark: 8 rays.
        for i in 0..<8 {
            let angle = CGFloat(i) * 45
            let ray = NSBezierPath()
            ray.move(to: point(angle, 0.4 * s))
            ray.line(to: point(angle, 3.5 * s))
            ray.lineWidth = 1.15 * s
            ray.lineCapStyle = .round
            ray.stroke()
        }
    }

    /// Menu bar icon. A template image: macOS colors it for light and dark menu bars.
    static let menuBarImage: NSImage = {
        let image = NSImage(size: NSSize(width: 18, height: 18), flipped: false) { rect in
            drawGlyph(in: rect, color: .black)
            return true
        }
        image.isTemplate = true
        return image
    }()

    /// App icon: warm rounded square with the white glyph.
    static func drawAppIcon(in rect: NSRect) {
        let side = rect.width
        let plate = rect.insetBy(dx: side * 0.098, dy: side * 0.098)
        let shape = NSBezierPath(roundedRect: plate, xRadius: plate.width * 0.225, yRadius: plate.width * 0.225)

        NSGraphicsContext.saveGraphicsState()
        let shadow = NSShadow()
        shadow.shadowColor = NSColor.black.withAlphaComponent(0.28)
        shadow.shadowBlurRadius = side * 0.03
        shadow.shadowOffset = NSSize(width: 0, height: -side * 0.012)
        shadow.set()
        NSColor(srgbRed: 0.80, green: 0.42, blue: 0.28, alpha: 1).setFill()
        shape.fill()
        NSGraphicsContext.restoreGraphicsState()

        let gradient = NSGradient(
            starting: NSColor(srgbRed: 0.93, green: 0.58, blue: 0.40, alpha: 1),
            ending: NSColor(srgbRed: 0.74, green: 0.34, blue: 0.21, alpha: 1))
        gradient?.draw(in: shape, angle: -90)

        let glyphSide = plate.width * 0.72
        let glyphRect = NSRect(x: plate.midX - glyphSide / 2, y: plate.midY - glyphSide / 2,
                               width: glyphSide, height: glyphSide)
        drawGlyph(in: glyphRect, color: .white)
    }

    static func png(pixels: Int, draw: (NSRect) -> Void) -> Data? {
        guard let rep = NSBitmapImageRep(
            bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels, bitsPerSample: 8,
            samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
            bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
        draw(NSRect(x: 0, y: 0, width: pixels, height: pixels))
        NSGraphicsContext.restoreGraphicsState()
        return rep.representation(using: .png, properties: [:])
    }

    /// Build step: writes AppIcon.iconset PNG files and a menu bar preview into `dir`.
    static func renderAll(to dir: URL) throws {
        let iconset = dir.appendingPathComponent("AppIcon.iconset", isDirectory: true)
        try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
        for points in [16, 32, 128, 256, 512] {
            for scale in [1, 2] {
                let name = scale == 1 ? "icon_\(points)x\(points).png" : "icon_\(points)x\(points)@2x.png"
                try png(pixels: points * scale, draw: drawAppIcon)?.write(to: iconset.appendingPathComponent(name))
            }
        }
        // The white glyph alone, on a clear 1024 canvas: the top layer of the layered icon (AppIcon.icon).
        try png(pixels: 1024) { rect in
            let side = rect.width * 0.58
            drawGlyph(in: NSRect(x: rect.midX - side / 2, y: rect.midY - side / 2, width: side, height: side),
                      color: .white)
        }?.write(to: dir.appendingPathComponent("glyph.png"))
        // Previews, only to look at.
        try png(pixels: 36) { drawGlyph(in: $0, color: .black) }?
            .write(to: dir.appendingPathComponent("menubar-preview@2x.png"))
        try png(pixels: 288) { rect in
            NSColor.white.setFill(); rect.fill()
            drawGlyph(in: rect, color: .black)
        }?.write(to: dir.appendingPathComponent("menubar-preview-big.png"))
    }
}
