import Foundation
import Metal

@main struct VideoOutMetalTest {
    static func main() throws {
        let viewport = MaxPS4VideoOutMetal.viewport(width: 2, height: 1, output: CGSize(width: 4, height: 4))
        precondition(viewport.originX == 0 && viewport.originY == 1 && viewport.width == 4 && viewport.height == 2)
        guard let device = MTLCreateSystemDefaultDevice() else {
            print("SKIP real VideoOut Metal readback: no Metal device; presentation gate and iPhone shader/typecheck remain required")
            return
        }
        let pipeline = try MaxPS4VideoOutMetal.make(device: device)
        let queue = device.makeCommandQueue()!
        // Two rows with one padding pixel per row. Distinct corners detect Y-flips
        // and channel swaps; padding must never leak into the displayed image.
        let rgba: [UInt8] = [200,32,64,255, 16,180,48,255, 255,0,255,255,
                            24,80,220,255, 210,160,96,255, 255,0,255,255]
        for format in [UInt32(0x80002200), UInt32(0x80000000)] {
            var source = rgba
            if format == 0x80000000 {
                for i in stride(from: 0, to: source.count, by: 4) { source.swapAt(i, i + 2) }
            }
            let texture = try MaxPS4VideoOutMetal.texture(device: device, bytes: Data(source), width: 2, height: 2, pitch: 3, format: format)
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm_srgb, width: 2, height: 2, mipmapped: false)
            descriptor.storageMode = .shared; descriptor.usage = .renderTarget
            let output = device.makeTexture(descriptor: descriptor)!
            let pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = output; pass.colorAttachments[0].loadAction = .clear
            pass.colorAttachments[0].storeAction = .store
            let command = queue.makeCommandBuffer()!, encoder = command.makeRenderCommandEncoder(descriptor: pass)!
            MaxPS4VideoOutMetal.encode(encoder: encoder, pipeline: pipeline, texture: texture, output: CGSize(width: 2, height: 2))
            encoder.endEncoding(); command.commit(); command.waitUntilCompleted()
            precondition(command.status == .completed && command.error == nil)
            var pixels = [UInt8](repeating: 0, count: 16)
            pixels.withUnsafeMutableBytes {
                output.getBytes($0.baseAddress!, bytesPerRow: 8, from: MTLRegionMake2D(0, 0, 2, 2), mipmapLevel: 0)
            }
            for y in 0..<2 { for x in 0..<2 { for channel in 0..<4 {
                let input = (y * 3 + x) * 4 + (channel == 0 ? 2 : channel == 2 ? 0 : channel)
                let actual = pixels[(y * 2 + x) * 4 + channel]
                precondition(abs(Int(actual) - Int(rgba[input])) <= 1, "RGBA/BGRA, stride or orientation mismatch")
            } } }
        }
        for (width, pitch, format, size) in [(0,3,UInt32(0x80002200),24), (2,1,0x80002200,24), (2,3,0,24), (2,3,0x80002200,23)] {
            do {
                _ = try MaxPS4VideoOutMetal.texture(device: device, bytes: Data(count: size), width: UInt32(width), height: 2, pitch: UInt32(pitch), format: format)
                preconditionFailure("Invalid frame accepted")
            } catch { }
        }
        print("PASS real offscreen Metal: linear RGBA/BGRA, row padding, sRGB, orientation and readback; no onscreen presentation claimed")
    }
}
