import Foundation
import Metal

// CPU-owned linear frame upload; no PS4 shader or tiled-surface decoding.
enum MaxPS4VideoOutMetal {
    static let shader = """
    #include <metal_stdlib>
    using namespace metal;
    struct VideoVertex { float4 position [[position]]; float2 uv; };
    vertex VideoVertex videoVertex(uint id [[vertex_id]]) {
        const float2 p[] = {float2(-1,-1), float2(3,-1), float2(-1,3)};
        const float2 uv[] = {float2(0,1), float2(2,1), float2(0,-1)};
        return {float4(p[id], 0, 1), uv[id]};
    }
    fragment float4 videoFragment(VideoVertex v [[stage_in]], texture2d<float> image [[texture(0)]]) {
        constexpr sampler s(filter::nearest, address::clamp_to_edge);
        return image.sample(s, v.uv);
    }
    """

    static func make(device: MTLDevice) throws -> MTLRenderPipelineState {
        let library = try device.makeLibrary(source: shader, options: nil)
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library.makeFunction(name: "videoVertex")
        descriptor.fragmentFunction = library.makeFunction(name: "videoFragment")
        descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm_srgb
        return try device.makeRenderPipelineState(descriptor: descriptor)
    }

    static func texture(device: MTLDevice, bytes: Data, width: UInt32, height: UInt32,
                        pitch: UInt32, format: UInt32) throws -> MTLTexture {
        guard width > 0, width <= 1920, height > 0, height <= 1080,
              pitch >= width, pitch <= 2048,
              bytes.count == Int(pitch) * Int(height) * 4,
              format == 0x80002200 || format == 0x80000000 else {
            throw NSError(domain: "MaxPS4.VideoOut", code: -22)
        }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: format == 0x80002200 ? .rgba8Unorm_srgb : .bgra8Unorm_srgb,
            width: Int(width), height: Int(height), mipmapped: false)
        descriptor.storageMode = .shared
        descriptor.usage = .shaderRead
        guard let texture = device.makeTexture(descriptor: descriptor) else {
            throw NSError(domain: "MaxPS4.VideoOut", code: -12)
        }
        bytes.withUnsafeBytes {
            texture.replace(region: MTLRegionMake2D(0, 0, Int(width), Int(height)), mipmapLevel: 0,
                            withBytes: $0.baseAddress!, bytesPerRow: Int(pitch) * 4)
        }
        return texture
    }

    static func viewport(width: Int, height: Int, output: CGSize) -> MTLViewport {
        let scale = min(output.width / CGFloat(max(width, 1)), output.height / CGFloat(max(height, 1)))
        let w = CGFloat(width) * scale, h = CGFloat(height) * scale
        return MTLViewport(originX: Double((output.width - w) / 2), originY: Double((output.height - h) / 2),
                           width: Double(w), height: Double(h), znear: 0, zfar: 1)
    }

    static func encode(encoder: MTLRenderCommandEncoder, pipeline: MTLRenderPipelineState,
                       texture: MTLTexture, output: CGSize) {
        encoder.setRenderPipelineState(pipeline)
        encoder.setViewport(viewport(width: texture.width, height: texture.height, output: output))
        encoder.setFragmentTexture(texture, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
    }
}
