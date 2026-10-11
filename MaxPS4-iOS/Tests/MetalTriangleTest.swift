import Foundation
import Metal

@main
struct MetalTriangleTest {
    static func main() throws {
        let portrait = MaxPS4MetalPipeline.uniforms(elapsed: 0, width: 100, height: 200)
        let landscape = MaxPS4MetalPipeline.uniforms(elapsed: 1, width: 200, height: 100)
        precondition(portrait == SIMD2<Float>(0, 2))
        precondition(landscape == SIMD2<Float>(1, 0.5))
        precondition(MaxPS4MetalPipeline.uniforms(elapsed: 100, width: 0, height: 0).y.isFinite)
        precondition(MaxPS4MetalPipeline.uniforms(elapsed: 2 * .pi, width: 100, height: 100).x == 0)
        print("PASS animation and resize uniforms")
        guard let device = MTLCreateSystemDefaultDevice() else {
            print("SKIP offscreen GPU test: this CI host has no Metal device; iOS shader compilation is checked separately")
            return
        }
        let pipeline = try MaxPS4MetalPipeline.make(device: device)
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm,
            width: 64, height: 64, mipmapped: false)
        descriptor.usage = [.renderTarget]
        descriptor.storageMode = .shared
        let texture = device.makeTexture(descriptor: descriptor)!
        let queue = device.makeCommandQueue()!
        func render(angle: TimeInterval) throws -> [UInt8] {
            let pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = texture
            pass.colorAttachments[0].loadAction = .clear
            pass.colorAttachments[0].storeAction = .store
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1)
            let command = queue.makeCommandBuffer()!
            let encoder = command.makeRenderCommandEncoder(descriptor: pass)!
            var u = MaxPS4MetalPipeline.uniforms(elapsed: angle, width: 64, height: 64)
            encoder.setRenderPipelineState(pipeline)
            encoder.setVertexBytes(&u, length: MemoryLayout<SIMD2<Float>>.stride, index: 0)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
            encoder.endEncoding()
            command.commit()
            command.waitUntilCompleted()
            if let error = command.error { throw error }
            precondition(command.status == .completed)
            var pixels = [UInt8](repeating: 0, count: 64 * 64 * 4)
            pixels.withUnsafeMutableBytes { bytes in
                texture.getBytes(bytes.baseAddress!, bytesPerRow: 64 * 4,
                    from: MTLRegionMake2D(0, 0, 64, 64), mipmapLevel: 0)
            }
            return pixels
        }
        let first = try render(angle: 0)
        let second = try render(angle: 0.8)
        let center = (32 * 64 + 32) * 4
        precondition(first[center] > 0 && first[center + 1] > 0 && first[center + 2] > 0,
                     "Triangle must cover center with interpolated RGB")
        precondition(first[0] == 0 && first[1] == 0 && first[2] == 0, "Corner must remain clear")
        precondition(first != second, "Animation must change rendered pixels")
        print("PASS real Metal pipeline, completed commands, triangle coverage and animated pixels")
    }
}
