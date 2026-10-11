import Metal
import Foundation

// Native smoke test only: no guest commands, shaders or PS4 memory are consumed.
enum MaxPS4MetalPipeline {
    static let shader = """
    #include <metal_stdlib>
    using namespace metal;
    struct Output { float4 position [[position]]; float3 color; };
    vertex Output triangleVertex(uint id [[vertex_id]], constant float3 &u [[buffer(0)]]) {
        const float2 positions[] = {float2(0, 0.65), float2(-0.65, -0.55), float2(0.65, -0.55)};
        const float3 colors[] = {float3(1,0.2,0.3), float3(0.2,1,0.4), float3(0.2,0.4,1)};
        float c = cos(u.x), s = sin(u.x);
        float2 p = positions[id];
        Output o;
        o.position = float4((c*p.x-s*p.y)*u.y, (s*p.x+c*p.y)*u.z, 0, 1);
        o.color = colors[id];
        return o;
    }
    fragment float4 triangleFragment(Output in [[stage_in]]) { return float4(in.color, 1); }

    struct TexOutput { float4 position [[position]]; float2 uv; };
    vertex TexOutput quadVertex(uint id [[vertex_id]], constant float3 &u [[buffer(0)]]) {
        const float2 p[] = { {-0.7,-0.7}, {0.7,-0.7}, {-0.7,0.7}, {0.7,-0.7}, {0.7,0.7}, {-0.7,0.7} };
        const float2 uv[] = { {0,1}, {1,1}, {0,0}, {1,1}, {1,0}, {0,0} };
        TexOutput o;
        o.position = float4(p[id].x*u.y, p[id].y*u.z, 0, 1);
        o.uv = uv[id];
        return o;
    }
    fragment float4 quadFragment(TexOutput in [[stage_in]], texture2d<float> tex [[texture(0)]]) {
        constexpr sampler s(filter::nearest, address::clamp_to_edge);
        return tex.sample(s, in.uv);
    }
    """

    static func make(device: MTLDevice) throws -> MTLRenderPipelineState {
        let library = try device.makeLibrary(source: shader, options: nil)
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library.makeFunction(name: "triangleVertex")
        descriptor.fragmentFunction = library.makeFunction(name: "triangleFragment")
        descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
        return try device.makeRenderPipelineState(descriptor: descriptor)
    }


    static func makeTextured(device: MTLDevice) throws -> MTLRenderPipelineState {
        let library = try device.makeLibrary(source: shader, options: nil)
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library.makeFunction(name: "quadVertex")
        descriptor.fragmentFunction = library.makeFunction(name: "quadFragment")
        descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
        return try device.makeRenderPipelineState(descriptor: descriptor)
    }

    static func makeCheckerboard(device: MTLDevice) throws -> MTLTexture {
        let size = 64
        let rowBytes = size * 4
        var pixels = [UInt8](repeating: 0, count: size * rowBytes)
        for y in 0..<size {
            for x in 0..<size {
                let light = ((x / 8) + (y / 8)) % 2 == 0
                let i = y * rowBytes + x * 4
                pixels[i] = light ? 91 : 18
                pixels[i + 1] = light ? 170 : 32
                pixels[i + 2] = light ? 255 : 92
                pixels[i + 3] = 255
            }
        }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: size, height: size, mipmapped: false)
        descriptor.usage = [.shaderRead]
        guard let texture = device.makeTexture(descriptor: descriptor) else {
            throw NSError(domain: "MaxPS4.Metal", code: 2, userInfo: [NSLocalizedDescriptionKey: "Création de la texture impossible"])
        }
        pixels.withUnsafeBytes { bytes in
            texture.replace(region: MTLRegionMake2D(0, 0, size, size), mipmapLevel: 0, withBytes: bytes.baseAddress!, bytesPerRow: rowBytes)
        }
        return texture
    }

    static func uniforms(elapsed: TimeInterval, width: Double, height: Double) -> SIMD3<Float> {
        let w = max(width, 1), h = max(height, 1)
        return SIMD3(Float(elapsed.truncatingRemainder(dividingBy: 2 * .pi)),
                     Float(min(h / w, 1)), Float(min(w / h, 1)))
    }
}
