// Build-time helper: rasterize an SVG to PNG at a requested pixel size.
// Used by seekdb-pkg-build.sh to produce AppIcon.iconset entries from
// tools/macpkg/assets/original.svg.

import AppKit
import Foundation

let args = CommandLine.arguments
guard args.count == 4,
      let size = Int(args[3]), size > 0 else {
    FileHandle.standardError.write(Data("Usage: svg2png <input.svg> <output.png> <pixels>\n".utf8))
    exit(2)
}

let inputPath = args[1]
let outputPath = args[2]

guard let svg = NSImage(contentsOfFile: inputPath) else {
    FileHandle.standardError.write(Data("Failed to load \(inputPath)\n".utf8))
    exit(1)
}

guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil,
                                 pixelsWide: size, pixelsHigh: size,
                                 bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                 isPlanar: false, colorSpaceName: .deviceRGB,
                                 bytesPerRow: 0, bitsPerPixel: 32) else {
    FileHandle.standardError.write(Data("Failed to allocate bitmap rep\n".utf8))
    exit(1)
}
rep.size = NSSize(width: size, height: size)

NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
NSGraphicsContext.current?.imageInterpolation = .high
svg.draw(in: NSRect(x: 0, y: 0, width: size, height: size),
         from: .zero, operation: .sourceOver, fraction: 1.0)
NSGraphicsContext.restoreGraphicsState()

guard let data = rep.representation(using: .png, properties: [:]) else {
    FileHandle.standardError.write(Data("Failed to encode PNG\n".utf8))
    exit(1)
}

do {
    try data.write(to: URL(fileURLWithPath: outputPath))
} catch {
    FileHandle.standardError.write(Data("Failed to write \(outputPath): \(error)\n".utf8))
    exit(1)
}
