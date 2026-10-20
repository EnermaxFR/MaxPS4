#pragma once
// PixelBackend on Metal (Objective-C++): the draw's generated source
// (GCNPixelProgram) is compiled as MSL with fast math disabled, cached by
// source hash, and each batch of fragments runs as one compute dispatch, one
// thread per fragment. Per-draw uniform data (SGPRs, interpolation table,
// image descriptors and texels) is uploaded once in prepare and freed in
// release. The kernel prelude text (GCNKernel.h then GCNKernelImage.h) is
// given by the caller, so an app can ship it as a resource.
#ifdef __OBJC__
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "GCNPixelOffload.hpp"
#include <map>
#include <mutex>
#include <string>

namespace maxps4::gpu {
struct GCNMetalPixelBackend : PixelBackend {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  std::string prelude;
  std::map<uint64_t, id<MTLComputePipelineState>> cache;
  std::mutex lock;
  unsigned compiled = 0, dispatches = 0;

  struct DrawResources { id<MTLComputePipelineState> pipeline; id<MTLBuffer> sgprs, interp, images, texels; uint32_t numInterp; };

  GCNMetalPixelBackend(id<MTLDevice> d, std::string preludeText) : device(d), prelude(std::move(preludeText)) {
    if (device) queue = [device newCommandQueue];
  }
  static std::string kernel() {
    return "kernel void maxps4_shade(constant gcn_u32* s [[buffer(0)]], device const float* interp [[buffer(1)]],\n"
           "    constant uint& numInterp [[buffer(2)]], device const GcnImage* images [[buffer(3)]],\n"
           "    device const unsigned char* texels [[buffer(4)]], device const GcnPixelFragment* f [[buffer(5)]],\n"
           "    device GcnPixelOutput* o [[buffer(6)]], constant uint& n [[buffer(7)]], uint id [[thread_position_in_grid]]) {\n"
           "  if (id >= n) return;\n"
           "  GcnPixelOutput r;\n"
           "  gcn_pixel_shade(s, interp, numInterp, images, texels, f[id], r);\n"
           "  o[id] = r;\n"
           "}\n";
  }
  id<MTLBuffer> upload(const void* data, size_t bytes) {
    if (!bytes) return [device newBufferWithLength:16 options:MTLResourceStorageModeShared];
    return [device newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared];
  }
  bool prepare(PixelDraw& draw, std::string& why) override {
    if (!device || !queue) { why = "Metal: no device"; return false; }
    id<MTLComputePipelineState> pipeline = nil;
    {
      std::lock_guard<std::mutex> guard(lock);
      auto found = cache.find(draw.key);
      if (found != cache.end()) pipeline = found->second;
      else {
        const std::string source = prelude + draw.source + kernel();
        MTLCompileOptions* options = [MTLCompileOptions new];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options.fastMathEnabled = NO;   // IEEE arithmetic (mathMode is missing from older SDKs)
#pragma clang diagnostic pop
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:options error:&error];
        id<MTLFunction> fn = library ? [library newFunctionWithName:@"maxps4_shade"] : nil;
        pipeline = fn ? [device newComputePipelineStateWithFunction:fn error:&error] : nil;
        if (!pipeline) { why = std::string("Metal: ") + (error ? error.localizedDescription.UTF8String : "no kernel"); return false; }
        cache.emplace(draw.key, pipeline);
        ++compiled;
      }
    }
    auto* r = new DrawResources{pipeline, upload(draw.sgprs.data(), draw.sgprs.size() * 4), upload(draw.interp.data(), draw.interp.size() * 4),
                                upload(draw.images.data(), draw.images.size() * sizeof(GcnImage)), upload(draw.texels.data(), draw.texels.size()),
                                uint32_t(draw.numInterp)};
    draw.backendData = r;
    return true;
  }
  bool shade(const PixelDraw& draw, const GcnPixelFragment* fragments, size_t n, GcnPixelOutput* out, std::string& error) override {
    @autoreleasepool {
      const auto* r = static_cast<const DrawResources*>(draw.backendData);
      id<MTLBuffer> in = upload(fragments, n * sizeof(GcnPixelFragment));
      id<MTLBuffer> result = [device newBufferWithLength:n * sizeof(GcnPixelOutput) options:MTLResourceStorageModeShared];
      const uint32_t count = uint32_t(n);
      id<MTLCommandBuffer> cb = [queue commandBuffer];
      id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
      [enc setComputePipelineState:r->pipeline];
      [enc setBuffer:r->sgprs offset:0 atIndex:0]; [enc setBuffer:r->interp offset:0 atIndex:1];
      [enc setBytes:&r->numInterp length:4 atIndex:2]; [enc setBuffer:r->images offset:0 atIndex:3];
      [enc setBuffer:r->texels offset:0 atIndex:4]; [enc setBuffer:in offset:0 atIndex:5];
      [enc setBuffer:result offset:0 atIndex:6]; [enc setBytes:&count length:4 atIndex:7];
      [enc dispatchThreadgroups:MTLSizeMake((n + 63) / 64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];   // kernel bounds-checks n
      [enc endEncoding]; [cb commit]; [cb waitUntilCompleted];
      if (cb.error) { error = std::string("Metal: ") + cb.error.localizedDescription.UTF8String; return false; }
      std::memcpy(out, result.contents, n * sizeof(GcnPixelOutput));
      std::lock_guard<std::mutex> guard(lock);
      ++dispatches;
      return true;
    }
  }
  void release(PixelDraw& draw) override {
    delete static_cast<DrawResources*>(draw.backendData);
    draw.backendData = nullptr;
  }
};
} // namespace maxps4::gpu
#endif
