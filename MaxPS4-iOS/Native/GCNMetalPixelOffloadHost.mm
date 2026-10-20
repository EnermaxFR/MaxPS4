// Apple hosts: hostPixelBackend() is the Metal pixel backend, created once.
// The kernel prelude (GCNKernel.h, then GCNKernelImage.h) is read from the
// main bundle's resources, or from MAXPS4_GPU_PRELUDE_DIR when set (command
// line runners). MAXPS4_GPU_OFFLOAD=0 disables the offload. Without a Metal
// device or the prelude, there is no backend and the interpreter shades.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "GCNMetalPixelBackend.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace maxps4::gpu {
static std::string preludeFile(const char* name) {
  NSString* path = nil;
  if (const char* dir = std::getenv("MAXPS4_GPU_PRELUDE_DIR"))
    path = [[NSString stringWithUTF8String:dir] stringByAppendingPathComponent:[NSString stringWithUTF8String:name]];
  else
    path = [[NSBundle mainBundle] pathForResource:[[NSString stringWithUTF8String:name] stringByDeletingPathExtension] ofType:@"h"];
  NSString* text = path ? [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil] : nil;
  return text ? std::string(text.UTF8String) : std::string();
}

PixelBackend* hostPixelBackend() {
  static std::unique_ptr<GCNMetalPixelBackend> backend = [] {
    std::unique_ptr<GCNMetalPixelBackend> made;
    if (const char* e = std::getenv("MAXPS4_GPU_OFFLOAD"); e && std::strcmp(e, "0") == 0) return made;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { NSArray<id<MTLDevice>>* all = MTLCopyAllDevices(); if (all.count) device = all[0]; }
    const std::string kernel = preludeFile("GCNKernel.h"), image = preludeFile("GCNKernelImage.h");
    if (!device || kernel.empty() || image.empty()) {
      std::fprintf(stderr, "GPU.OFFLOAD unavailable: %s\n", !device ? "no Metal device" : "kernel prelude not found");
      return made;
    }
    made = std::make_unique<GCNMetalPixelBackend>(device, kernel + image);
    std::fprintf(stderr, "GPU.OFFLOAD Metal backend on %s\n", device.name.UTF8String);
    return made;
  }();
  return backend.get();
}
} // namespace maxps4::gpu
