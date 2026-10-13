import Foundation
import CoreGraphics

// Native display conversion only. No scene geometry or generated pixels here.
enum MaxPS4GuestFrameImage {
    static func make(bytes: Data, width: UInt32, height: UInt32) -> CGImage? {
        guard width > 0, width <= 320, height > 0, height <= 240,
              bytes.count == Int(width * height * 4),
              let provider = CGDataProvider(data: bytes as CFData) else { return nil }
        return CGImage(width: Int(width), height: Int(height), bitsPerComponent: 8,
            bitsPerPixel: 32, bytesPerRow: Int(width)*4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}
