import Foundation
import Metal

@main
struct PM4MetalIntegrationTest {
    static func main() throws {
        var drawn = 99
        let words: [UInt32] = [0xc0012d00, 3, 2]
        let noEncoder = words.withUnsafeBufferPointer {
            maxps4_pm4_encode_triangle($0.baseAddress, $0.count, nil, nil, &drawn)
        }
        precondition(noEncoder != 0 && drawn == 0)
        print("PASS Swift calls exported C++ PM4 bridge")
        guard let device = MTLCreateSystemDefaultDevice() else {
            print("SKIP GPU readback: no Metal device; ABI and iOS compilation checked separately")
            return
        }
        let pipeline = try MaxPS4MetalPipeline.make(device: device)
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm,
            width: 64, height: 64, mipmapped: false)
        descriptor.usage = .renderTarget
        descriptor.storageMode = .shared
        let texture = device.makeTexture(descriptor: descriptor)!
        let queue = device.makeCommandQueue()!
        func render(_ stream: [UInt32], expected: Int) throws -> [UInt8] {
            let pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = texture
            pass.colorAttachments[0].loadAction = .clear
            pass.colorAttachments[0].storeAction = .store
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1)
            let command = queue.makeCommandBuffer()!
            let encoder = command.makeRenderCommandEncoder(descriptor: pass)!
            var u = SIMD3<Float>(0, 1, 1)
            encoder.setVertexBytes(&u, length: MemoryLayout<SIMD3<Float>>.stride, index: 0)
            let status = stream.withUnsafeBufferPointer {
                maxps4_pm4_encode_triangle($0.baseAddress, $0.count, encoder, pipeline, &drawn)
            }
            precondition((status == 0) == (expected > 0) && drawn == expected)
            encoder.endEncoding()
            command.commit()
            command.waitUntilCompleted()
            if let error = command.error { throw error }
            precondition(command.status == .completed)
            var pixels = [UInt8](repeating: 0, count: 64 * 64 * 4)
            pixels.withUnsafeMutableBytes {
                texture.getBytes($0.baseAddress!, bytesPerRow: 64 * 4,
                    from: MTLRegionMake2D(0, 0, 64, 64), mipmapLevel: 0)
            }
            return pixels
        }
        let pixels = try render(words, expected: 1)
        let center = (32 * 64 + 32) * 4
        precondition(pixels[center] > 0 && pixels[center + 1] > 0 && pixels[center + 2] > 0)
        precondition(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0)
        let rejected = try render(words + [0xc0012d00, 3, 0], expected: 0)
        precondition(stride(from: 0, to: rejected.count, by: 4).allSatisfy {
            rejected[$0] == 0 && rejected[$0 + 1] == 0 && rejected[$0 + 2] == 0
        })
        print("PASS synthetic PM4 -> C++ -> real Metal completion and pixel readback; not PS4 shaders")
    }
}
