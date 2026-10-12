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
    """

    static func make(device: MTLDevice) throws -> MTLRenderPipelineState {
        let library = try device.makeLibrary(source: shader, options: nil)
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library.makeFunction(name: "triangleVertex")
        descriptor.fragmentFunction = library.makeFunction(name: "triangleFragment")
        descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
        return try device.makeRenderPipelineState(descriptor: descriptor)
    }

    static func uniforms(elapsed: TimeInterval, width: Double, height: Double) -> SIMD3<Float> {
        let w = max(width, 1), h = max(height, 1)
        return SIMD3(Float(elapsed.truncatingRemainder(dividingBy: 2 * .pi)),
                     Float(min(h / w, 1)), Float(min(w / h, 1)))
    }
}
