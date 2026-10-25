#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../Native/GCNTranslator.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace maxps4::gpu;
struct State { uint64_t base, size; uint32_t vs[16], ps[17], flags, count; uint64_t ranges[48]; };
int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { std::puts("SKIP texture footprint GPU benchmark: no device"); return 0; }
        std::string shader = gcnMetalPrelude() + R"MSL(
inline float4 prior_sample(GCNMemory M, GCNRes8 t, GCNRes4 s, float u, float v) {
    ulong base=(ulong(t.d[0]) | (ulong(t.d[1]&255u)<<32))<<8;
    uint width=(t.d[2]&0x3fffu)+1u, height=((t.d[2]>>14)&0x3fffu)+1u;
    uint pitch=((t.d[4]>>13)&0x3fffu)+1u, bytes=((t.d[1]>>20)&63u)==10u ? 4u : 2u;
    ulong offset=0ul;
    if(gcn_locate(M,base,((ulong(height)-1ul)*pitch+width)*bytes,offset))
        return gcn_sample_at(M,t,s,false,u,v,true,offset);
    return gcn_sample_at(M,t,s,false,u,v,false,0ul);
}
inline float4 texture_sum(uint index, GCNMemory M, constant GCNDrawState& D, uint mode) {
    GCNRes8 t; GCNRes4 s;
    for (uint k=0;k<8;++k) t.d[k]=D.psUser[k];
    for (uint k=0;k<4;++k) s.d[k]=D.psUser[8+k];
    float4 sum=float4(0);
    for (uint k=0;k<32;++k) {
        float u=float((index+k*17u)%257u)/257.0f;
        float v=float((index*3u+k*7u)%257u)/257.0f;
        sum += mode==2u ? gcn_sample(M,t,s,false,u,v) : mode==1u ? prior_sample(M,t,s,u,v) : gcn_sample_at(M,t,s,false,u,v,false,0ul);
    }
    return sum;
}
kernel void fast_texture(device const uchar* mem [[buffer(0)]], constant GCNDrawState& D [[buffer(1)]],
    device float4* result [[buffer(2)]], uint i [[thread_position_in_grid]]) {
    GCNMemory M={mem,D.base,D.size,D.ranges,D.rangeCount}; result[i]=texture_sum(i,M,D,2u);
}
kernel void resolved_texture(device const uchar* mem [[buffer(0)]], constant GCNDrawState& D [[buffer(1)]],
    device float4* result [[buffer(2)]], uint i [[thread_position_in_grid]]) {
    GCNMemory M={mem,D.base,D.size,D.ranges,D.rangeCount}; result[i]=texture_sum(i,M,D,1u);
}
kernel void checked_texture(device const uchar* mem [[buffer(0)]], constant GCNDrawState& D [[buffer(1)]],
    device float4* result [[buffer(2)]], uint i [[thread_position_in_grid]]) {
    GCNMemory M={mem,D.base,D.size,D.ranges,D.rangeCount}; result[i]=texture_sum(i,M,D,0u);
}
)MSL";
        MTLCompileOptions* options = [MTLCompileOptions new];
        if (@available(macOS 15.0, iOS 18.0, *)) options.mathMode = MTLMathModeSafe;
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:shader.c_str()] options:options error:&error];
        if (!library) std::printf("MSL: %s\n", error.localizedDescription.UTF8String);
        assert(library);
        constexpr unsigned count=4096;
        id<MTLComputePipelineState> pipelines[3];
        const char* names[]={"checked_texture","resolved_texture","fast_texture"};
        for(unsigned variant=0;variant<3;++variant) {
            id<MTLFunction> function=[library newFunctionWithName:[NSString stringWithUTF8String:names[variant]]];
            pipelines[variant]=[device newComputePipelineStateWithFunction:function error:&error];
            assert(pipelines[variant]);
        }
        for(const unsigned width : {16u,256u}) for(const unsigned format : {10u,16u}) {
        const unsigned height=width/2, bytes=format==10 ? 4 : 2;
        id<MTLBuffer> memory=[device newBufferWithLength:256+width*height*bytes options:MTLResourceStorageModeShared];
        auto* pixels=static_cast<uint8_t*>(memory.contents)+256;
        for(unsigned k=0;k<width*height;++k) {
            const uint32_t raw=(k*971u)|0xff000000u;
            std::memcpy(pixels+k*bytes,&raw,bytes);
        }
        State state{}; state.count=3;
        state.ranges[0]=0x10000; state.ranges[1]=16;
        state.ranges[3]=0x11000; state.ranges[4]=16;
        state.ranges[6]=0x14000; state.ranges[7]=width*height*bytes; state.ranges[8]=256;
        const uint32_t descriptors[]={0x140,format<<20,(width-1)|((height-1)<<14),0x90800facu,(width-1)<<13,0,0,0,0x12,0,0x500000,0};
        std::memcpy(state.ps,descriptors,sizeof descriptors);
        id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLBuffer> results[3]; double times[3]{};
        for(unsigned variant=0;variant<3;++variant) {
            results[variant]=[device newBufferWithLength:count*4*sizeof(float) options:MTLResourceStorageModeShared];
        }
            // Interleave variants to limit clock/thermal bias; discard two warmups.
            for(unsigned run=0;run<8;++run) for(unsigned turn=0;turn<3;++turn) {
                const unsigned variant=(turn+run)%3;
                id<MTLCommandBuffer> command=[queue commandBuffer];
                id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
                [encoder setComputePipelineState:pipelines[variant]]; [encoder setBuffer:memory offset:0 atIndex:0];
                [encoder setBytes:&state length:sizeof state atIndex:1]; [encoder setBuffer:results[variant] offset:0 atIndex:2];
                [encoder dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
                [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
                assert(command.status==MTLCommandBufferStatusCompleted && !command.error);
                if(run>=2 && command.GPUStartTime>0) times[variant]+=(command.GPUEndTime-command.GPUStartTime)*1000/6;
            }
        const float* checked=static_cast<const float*>(results[0].contents);
        for(unsigned variant=1;variant<3;++variant) {
            const float* result=static_cast<const float*>(results[variant].contents);
            for(unsigned k=0;k<count*4;++k) assert(std::fabs(checked[k]-result[k])<0.00001f);
        }
        std::printf("PASS texture footprint GPU: %ux%u format %u, checked %.3f ms, prior %.3f ms, interior %.3f ms; equal output (not iPhone FPS)\n",width,height,format,times[0],times[1],times[2]);
        }
    }
}
