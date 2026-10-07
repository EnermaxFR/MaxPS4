import AppKit

let args = CommandLine.arguments
guard args.count >= 2 else {
    fputs("usage: render_maxps4_icon.swift <output.png>\n", stderr)
    exit(2)
}

let output = args[1]
let size = NSSize(width: 1024, height: 1024)
let image = NSImage(size: size)

image.lockFocus()
guard let ctx = NSGraphicsContext.current?.cgContext else { exit(3) }

let rect = NSRect(origin: .zero, size: size)
ctx.setFillColor(NSColor(calibratedRed: 0.01, green: 0.025, blue: 0.07, alpha: 1).cgColor)
ctx.fill(rect)

// Deep navy -> indigo radial/linear glow.
let gradient = NSGradient(colors: [
    NSColor(calibratedRed: 0.015, green: 0.05, blue: 0.16, alpha: 1),
    NSColor(calibratedRed: 0.02, green: 0.02, blue: 0.09, alpha: 1),
    NSColor(calibratedRed: 0.09, green: 0.02, blue: 0.18, alpha: 1)
])!
gradient.draw(in: rect, angle: -35)

// Rounded neon frame.
let frameRect = rect.insetBy(dx: 28, dy: 28)
let frame = NSBezierPath(roundedRect: frameRect, xRadius: 150, yRadius: 150)
ctx.saveGState()
ctx.setShadow(offset: .zero, blur: 28, color: NSColor.systemBlue.withAlphaComponent(0.8).cgColor)
NSColor(calibratedRed: 0.05, green: 0.75, blue: 1.0, alpha: 0.95).setStroke()
frame.lineWidth = 8
frame.stroke()
ctx.restoreGState()

// Orbit rings.
for (i, inset) in [170.0, 245.0, 315.0].enumerated() {
    let r = NSRect(x: inset, y: 250 + Double(i) * 34, width: 1024 - inset * 2, height: 430 - Double(i) * 30)
    let p = NSBezierPath(ovalIn: r)
    ctx.saveGState()
    ctx.setShadow(offset: .zero, blur: 22, color: NSColor(calibratedRed: 0.1, green: 0.45, blue: 1, alpha: 0.65).cgColor)
    (i == 1 ? NSColor.systemPurple : NSColor.systemCyan).withAlphaComponent(0.65).setStroke()
    p.lineWidth = i == 1 ? 5 : 7
    p.stroke()
    ctx.restoreGState()
}

// Stylized original M emblem, deliberately not based on Sony/PlayStation marks.
let m = NSBezierPath()
m.move(to: NSPoint(x: 240, y: 390))
m.line(to: NSPoint(x: 300, y: 780))
m.line(to: NSPoint(x: 512, y: 590))
m.line(to: NSPoint(x: 724, y: 790))
m.line(to: NSPoint(x: 790, y: 390))
m.line(to: NSPoint(x: 650, y: 490))
m.line(to: NSPoint(x: 622, y: 650))
m.line(to: NSPoint(x: 512, y: 540))
m.line(to: NSPoint(x: 402, y: 650))
m.line(to: NSPoint(x: 374, y: 490))
m.close()

let mGradient = NSGradient(colors: [
    NSColor(calibratedRed: 0.05, green: 0.95, blue: 1.0, alpha: 1),
    NSColor(calibratedRed: 0.12, green: 0.35, blue: 1.0, alpha: 1),
    NSColor(calibratedRed: 0.75, green: 0.18, blue: 1.0, alpha: 1)
])!
ctx.saveGState()
ctx.setShadow(offset: .zero, blur: 36, color: NSColor(calibratedRed: 0.1, green: 0.55, blue: 1, alpha: 0.9).cgColor)
mGradient.draw(in: m, angle: -20)
NSColor.white.withAlphaComponent(0.9).setStroke()
m.lineWidth = 5
m.stroke()
ctx.restoreGState()

// Brand text.
let para = NSMutableParagraphStyle()
para.alignment = .center
let maxFont = NSFont.systemFont(ofSize: 118, weight: .heavy)
let full = NSMutableAttributedString(string: "Max", attributes: [
    .font: maxFont,
    .foregroundColor: NSColor(calibratedWhite: 0.92, alpha: 1),
    .paragraphStyle: para
])
full.append(NSAttributedString(string: "PS4", attributes: [
    .font: maxFont,
    .foregroundColor: NSColor(calibratedRed: 0.1, green: 0.65, blue: 1.0, alpha: 1),
    .paragraphStyle: para
]))
let textRect = NSRect(x: 90, y: 120, width: 844, height: 150)
ctx.saveGState()
ctx.setShadow(offset: NSSize(width: 0, height: -4), blur: 18, color: NSColor.systemBlue.withAlphaComponent(0.75).cgColor)
full.draw(in: textRect)
ctx.restoreGState()

let underline = NSBezierPath()
underline.move(to: NSPoint(x: 180, y: 110))
underline.line(to: NSPoint(x: 844, y: 110))
NSColor(calibratedRed: 0.08, green: 0.8, blue: 1.0, alpha: 0.9).setStroke()
underline.lineWidth = 8
underline.stroke()

image.unlockFocus()

guard let tiff = image.tiffRepresentation,
      let bitmap = NSBitmapImageRep(data: tiff),
      let png = bitmap.representation(using: .png, properties: [:]) else {
    fputs("failed to encode icon\n", stderr)
    exit(4)
}
try png.write(to: URL(fileURLWithPath: output), options: .atomic)
print("Rendered MaxPS4 icon: \(output)")
