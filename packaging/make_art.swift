// Draws the app icon and the disk image background. Own artwork: nothing here comes from the original game.
// usage: swift make_art.swift OUTDIR   -> OUTDIR/icon_1024.png, OUTDIR/dmg_background.png, OUTDIR/dmg_background@2x.png
import AppKit

let outDir = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."

func bitmap(_ w: Int, _ h: Int, scale: CGFloat = 1, _ draw: (CGContext) -> Void) -> NSBitmapImageRep {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(CGFloat(w) * scale), pixelsHigh: Int(CGFloat(h) * scale),
                               bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = NSSize(width: w, height: h)
    let ctx = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = ctx
    draw(ctx.cgContext)
    NSGraphicsContext.restoreGraphicsState()
    return rep
}

func save(_ rep: NSBitmapImageRep, _ name: String) {
    let url = URL(fileURLWithPath: outDir).appendingPathComponent(name)
    try! rep.representation(using: .png, properties: [:])!.write(to: url)
}

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 255) / 255, green: CGFloat((hex >> 8) & 255) / 255, blue: CGFloat(hex & 255) / 255, alpha: a)
}

func gradient(_ colors: [CGColor], _ stops: [CGFloat]) -> CGGradient {
    CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(), colors: colors as CFArray, locations: stops)!
}

// A leaf: a pointed oval from the origin to (len, 0), rotated by angle.
func leaf(_ c: CGContext, at p: CGPoint, len: CGFloat, width: CGFloat, angle: CGFloat, fill: [CGColor]) {
    c.saveGState()
    c.translateBy(x: p.x, y: p.y)
    c.rotate(by: angle)
    let path = CGMutablePath()
    path.move(to: .zero)
    path.addCurve(to: CGPoint(x: len, y: 0), control1: CGPoint(x: len * 0.25, y: width), control2: CGPoint(x: len * 0.8, y: width * 0.75))
    path.addCurve(to: .zero, control1: CGPoint(x: len * 0.8, y: -width * 0.75), control2: CGPoint(x: len * 0.25, y: -width))
    c.addPath(path)
    c.clip()
    c.drawLinearGradient(gradient(fill, [0, 1]), start: CGPoint(x: 0, y: width), end: CGPoint(x: len, y: -width), options: [])
    c.setStrokeColor(rgb(0x0b3d2a, 0.55))
    c.setLineWidth(len * 0.022)
    c.move(to: CGPoint(x: len * 0.04, y: 0))
    c.addLine(to: CGPoint(x: len * 0.93, y: 0))
    c.strokePath()
    c.restoreGState()
}

// The biosphere: a glass dome over a seedling, on a dark ground. Origin bottom-left, size s.
func drawEmblem(_ c: CGContext, center: CGPoint, radius R: CGFloat) {
    let ground = center.y - R * 0.42
    // glow inside the dome
    c.saveGState()
    c.addArc(center: CGPoint(x: center.x, y: ground), radius: R, startAngle: 0, endAngle: .pi, clockwise: false)
    c.closePath()
    c.clip()
    c.drawRadialGradient(gradient([rgb(0xbff7d0, 0.55), rgb(0x3fbf8a, 0.22), rgb(0x0f4a3c, 0.0)], [0, 0.45, 1]),
                         startCenter: CGPoint(x: center.x, y: ground + R * 0.25), startRadius: 0,
                         endCenter: CGPoint(x: center.x, y: ground + R * 0.25), endRadius: R * 1.05, options: [])
    // ribs of the dome
    c.setStrokeColor(rgb(0xd9fff0, 0.26))
    c.setLineWidth(R * 0.018)
    for k in [0.36, 0.72] as [CGFloat] {
        c.addEllipse(in: CGRect(x: center.x - R * k, y: ground - R, width: 2 * R * k, height: 2 * R))
        c.strokePath()
    }
    c.move(to: CGPoint(x: center.x, y: ground)); c.addLine(to: CGPoint(x: center.x, y: ground + R)); c.strokePath()
    for a in [0.36, 0.72] as [CGFloat] {
        let y = ground + R * sin(a * .pi / 2 * 1.0)
        let half = R * cos(asin(min(1, (y - ground) / R)))
        c.move(to: CGPoint(x: center.x - half, y: y)); c.addLine(to: CGPoint(x: center.x + half, y: y)); c.strokePath()
    }
    c.restoreGState()

    // seedling
    let base = CGPoint(x: center.x, y: ground)
    let top = CGPoint(x: center.x + R * 0.02, y: ground + R * 0.50)
    c.setStrokeColor(rgb(0x2f9d57))
    c.setLineWidth(R * 0.06)
    c.setLineCap(.round)
    c.move(to: base)
    c.addQuadCurve(to: top, control: CGPoint(x: center.x - R * 0.07, y: ground + R * 0.26))
    c.strokePath()
    leaf(c, at: top, len: R * 0.50, width: R * 0.20, angle: 0.62, fill: [rgb(0xb6f27a), rgb(0x3fae4d)])
    leaf(c, at: CGPoint(x: top.x, y: top.y - R * 0.03), len: R * 0.44, width: R * 0.18, angle: .pi - 0.50, fill: [rgb(0x8fe07a), rgb(0x2a8f4b)])

    // glass shell
    c.setStrokeColor(rgb(0xeafff6, 0.92))
    c.setLineWidth(R * 0.055)
    c.setLineCap(.round)
    c.addArc(center: CGPoint(x: center.x, y: ground), radius: R, startAngle: 0, endAngle: .pi, clockwise: false)
    c.strokePath()
    // highlight on the glass
    c.setStrokeColor(rgb(0xffffff, 0.55))
    c.setLineWidth(R * 0.03)
    c.addArc(center: CGPoint(x: center.x, y: ground), radius: R * 0.86, startAngle: .pi * 0.62, endAngle: .pi * 0.86, clockwise: false)
    c.strokePath()
    // ground
    c.setStrokeColor(rgb(0xeafff6, 0.92))
    c.setLineWidth(R * 0.055)
    c.move(to: CGPoint(x: center.x - R * 1.22, y: ground)); c.addLine(to: CGPoint(x: center.x + R * 1.22, y: ground)); c.strokePath()
    c.setStrokeColor(rgb(0xeafff6, 0.45))
    c.setLineWidth(R * 0.035)
    c.move(to: CGPoint(x: center.x - R * 0.8, y: ground - R * 0.16)); c.addLine(to: CGPoint(x: center.x + R * 0.8, y: ground - R * 0.16)); c.strokePath()
    c.move(to: CGPoint(x: center.x - R * 0.42, y: ground - R * 0.30)); c.addLine(to: CGPoint(x: center.x + R * 0.42, y: ground - R * 0.30)); c.strokePath()
}

// --- app icon: 1024 canvas, 824 rounded square (the macOS icon grid) ---
let icon = bitmap(1024, 1024) { c in
    let box = CGRect(x: 100, y: 100, width: 824, height: 824)
    let shape = CGPath(roundedRect: box, cornerWidth: 186, cornerHeight: 186, transform: nil)
    c.saveGState()
    c.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: rgb(0x000000, 0.35))
    c.addPath(shape); c.setFillColor(rgb(0x0b2a25)); c.fillPath()
    c.restoreGState()
    c.saveGState()
    c.addPath(shape); c.clip()
    c.drawLinearGradient(gradient([rgb(0x14584a), rgb(0x0a2b27), rgb(0x05110f)], [0, 0.55, 1]),
                         start: CGPoint(x: 512, y: 924), end: CGPoint(x: 512, y: 100), options: [])
    // faint stars
    var seed: UInt64 = 7
    func rnd() -> CGFloat { seed = seed &* 6364136223846793005 &+ 1442695040888963407; return CGFloat((seed >> 33) % 10000) / 10000 }
    for _ in 0..<70 {
        let x = 100 + rnd() * 824, y = 560 + rnd() * 364, r = 1.2 + rnd() * 2.6
        c.setFillColor(rgb(0xffffff, 0.10 + rnd() * 0.35))
        c.fillEllipse(in: CGRect(x: x - r, y: y - r, width: 2 * r, height: 2 * r))
    }
    drawEmblem(c, center: CGPoint(x: 512, y: 520), radius: 270)
    c.restoreGState()
    c.addPath(shape); c.setStrokeColor(rgb(0xffffff, 0.10)); c.setLineWidth(3); c.strokePath()
}
save(icon, "icon_1024.png")

// --- disk image window: 660 x 420 points; app icon at (165, 200), Applications at (495, 200) (Finder coordinates, top-left origin) ---
func text(_ s: String, _ rect: CGRect, size: CGFloat, weight: NSFont.Weight, color: NSColor, align: NSTextAlignment = .center) {
    let p = NSMutableParagraphStyle(); p.alignment = align; p.lineSpacing = 2
    let attr: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: size, weight: weight), .foregroundColor: color, .paragraphStyle: p]
    (s as NSString).draw(in: rect, withAttributes: attr)
}

func background(scale: CGFloat) -> NSBitmapImageRep {
    bitmap(660, 420, scale: scale) { c in
        c.drawLinearGradient(gradient([rgb(0x14584a), rgb(0x0a2b27), rgb(0x05110f)], [0, 0.6, 1]),
                             start: CGPoint(x: 330, y: 420), end: CGPoint(x: 330, y: 0), options: [])
        text("Biosys Remaster", CGRect(x: 0, y: 358, width: 660, height: 36), size: 26, weight: .semibold, color: NSColor(white: 1, alpha: 0.96))
        text("Drag the app onto the Applications folder", CGRect(x: 0, y: 332, width: 660, height: 22), size: 14, weight: .regular, color: NSColor(white: 1, alpha: 0.72))
        // A mid-tone tray behind the icons and their names: Finder writes the names in black (light appearance) or
        // white (dark appearance) and cannot be told otherwise, so the ground under them has to carry both.
        let tray = CGPath(roundedRect: CGRect(x: 60, y: 118, width: 540, height: 192), cornerWidth: 22, cornerHeight: 22, transform: nil)
        c.saveGState()
        c.addPath(tray); c.clip()
        c.drawLinearGradient(gradient([rgb(0x8db3a5), rgb(0x759a8d)], [0, 1]), start: CGPoint(x: 330, y: 310), end: CGPoint(x: 330, y: 118), options: [])
        c.restoreGState()
        c.addPath(tray); c.setStrokeColor(rgb(0xffffff, 0.18)); c.setLineWidth(1); c.strokePath()
        // arrow between the two icons (icons are 128 pt, centred at y = 420 - 200)
        c.setStrokeColor(rgb(0x0b2a25, 0.80)); c.setLineWidth(5); c.setLineCap(.round); c.setLineJoin(.round)
        c.move(to: CGPoint(x: 272, y: 226)); c.addLine(to: CGPoint(x: 386, y: 226)); c.strokePath()
        c.move(to: CGPoint(x: 366, y: 246)); c.addLine(to: CGPoint(x: 388, y: 226)); c.addLine(to: CGPoint(x: 366, y: 206)); c.strokePath()
        text("First start: macOS will say it cannot check this app (it is a free fan project without a paid Apple signature).\nOpen System Settings › Privacy & Security, scroll down and choose “Open Anyway”.",
             CGRect(x: 40, y: 50, width: 580, height: 54), size: 11.5, weight: .regular, color: NSColor(white: 1, alpha: 0.70))
        text("Unofficial homage. The original game is not included: the app asks for your own copy on first start.",
             CGRect(x: 40, y: 24, width: 580, height: 18), size: 11, weight: .regular, color: NSColor(white: 1, alpha: 0.50))
    }
}
save(background(scale: 1), "dmg_background.png")
save(background(scale: 2), "dmg_background@2x.png")
print("art written to \(outDir)")
