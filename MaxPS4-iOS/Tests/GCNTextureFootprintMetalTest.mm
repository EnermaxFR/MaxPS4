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
inline float4 texture_sum(uint index, GCNMemory M, constant GCNDrawState& D, bool fast) {
    GCNRes8 t; GCNRes4 s;
    for (uint k=0;k<8;++k) t.d[k]=D.psUser[k];
    for (uint k=0;k<4;++k) s.d[k]=D.psUser[8+k];
    float4 sum=float4(0);
    for (uint k=0;k<32;++k) {
        float u=float((index+k*17u)%257u)/257.0f;
        float v=float((index*3u+k*7u)%257u)/257.0f;
        sum += fast ? gcn_sample(M,t,s,false,u,v) : gcn_sample_at(M,t,s,false,u,v,false,0ul);
    }
    return sum;
}
kernel void fast_texture(device const uchar* mem [[buffer(0)]], constant GCNDrawState& D [[buffer(1)]],
    device float4* result [[buffer(2)]], uint i [[thread_position_in_grid]]) {
    GCNMemory M={mem,D.base,D.size,D.ranges,D.rangeCount}; result[i]=texture_sum(i,M,D,true);
}
kernel void checked_texture(device const uchar* mem [[buffer(0)]], constant GCNDrawState& D [[buffer(1)]],
    device float4* result [[buffer(2)]], uint i [[thread_position_in_grid]]) {
    GCNMemory M={mem,D.base,D.size,D.ranges,D.rangeCount}; result[i]=texture_sum(i,M,D,false);
}
)MSL";
        MTLCompileOptions* options = [MTLCompileOptions new];
        if (@available(macOS 15.0, iOS 18.0, *)) options.mathMode = MTLMathModeSafe;
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:shader.c_str()] options:options error:&error];
        if (!library) std::printf("MSL: %s\n", error.localizedDescription.UTF8String);
        assert(library);
        constexpr unsigned count=4096, width=16, height=8;
        id<MTLBuffer> memory=[device newBufferWithLength:256+width*height*4 options:MTLResourceStorageModeShared];
        auto* pixels=static_cast<uint32_t*>(memory.contents)+64;
        for(unsigned k=0;k<width*height;++k) pixels[k]=(k*971u)|0xff000000u;
        State state{}; state.count=3;
        state.ranges[0]=0x10000; state.ranges[1]=16;
        state.ranges[3]=0x11000; state.ranges[4]=16;
        state.ranges[6]=0x14000; state.ranges[7]=width*height*4; state.ranges[8]=256;
        const uint32_t descriptors[]={0x140,10u<<20,(width-1)|((height-1)<<14),0x90800facu,(width-1)<<13,0,0,0,0x12,0,0x500000,0};
        std::memcpy(state.ps,descriptors,sizeof descriptors);
        id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLBuffer> results[2]; double times[2]{};
        for(unsigned variant=0;variant<2;++variant) {
            id<MTLFunction> function=[library newFunctionWithName:variant ? @"fast_texture" : @"checked_texture"];
            id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:function error:&error];
            assert(pipeline);
            results[variant]=[device newBufferWithLength:count*4*sizeof(float) options:MTLResourceStorageModeShared];
            for(unsigned run=0;run<4;++run) {
                id<MTLCommandBuffer> command=[queue commandBuffer];
                id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
                [encoder setComputePipelineState:pipeline]; [encoder setBuffer:memory offset:0 atIndex:0];
                [encoder setBytes:&state length:sizeof state atIndex:1]; [encoder setBuffer:results[variant] offset:0 atIndex:2];
                [encoder dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
                [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
                assert(command.status==MTLCommandBufferStatusCompleted && !command.error);
                if(run>0 && command.GPUStartTime>0) times[variant]+=(command.GPUEndTime-command.GPUStartTime)*1000/3;
            }
        }
        const float* checked=static_cast<const float*>(results[0].contents);
        const float* fast=static_cast<const float*>(results[1].contents);
        for(unsigned k=0;k<count*4;++k) assert(std::fabs(checked[k]-fast[k])<0.00001f);
        std::printf("PASS texture footprint GPU: identical sample sums, checked %.3f ms, resolved %.3f ms (device-specific, not iPhone FPS)\n",times[0],times[1]);
    }
}
