#import "PM4GameMetalBridge.h"
#include "PM4MetalRenderer.h"
#include <cerrno>
#include <memory>
#include <mutex>

using namespace maxps4::gpu;
namespace {
struct GameMetalState {
    std::mutex mutex;
    GCNMemory memory;
    PM4DrawStats stats;
    PM4DrawCollector collector;
    PM4MetalRenderer renderer;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    std::string error;
    GameMetalState(id<MTLDevice> device, MaxPS4GameMemoryReader reader)
        : memory{[reader](uint64_t a, void* d, size_t n) { return bool(reader(a,d,n)); }, {}},
          collector(memory, stats), renderer(device, memory, nil, 0), device(device), queue([device newCommandQueue]) {}
};
}
@implementation MaxPS4GameMetalBridge {
    std::unique_ptr<GameMetalState> state_;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device reader:(MaxPS4GameMemoryReader)reader {
    if ((self = [super init])) {
        state_ = std::make_unique<GameMetalState>(device, [reader copy]);
        if (!state_->queue) return nil;
    }
    return self;
}
- (int32_t)submit:(const uint32_t*)words count:(size_t)count {
    @autoreleasepool {
        auto& s = *state_;
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.error.empty()) return -EIO;
        if (!words || !count || count > 65536) return -EINVAL;
        id<MTLCommandBuffer> command = [s.queue commandBuffer];
        if (!command) { s.error = "Metal command buffer allocation"; return -ENOMEM; }
        const unsigned failed = s.stats.failed;
        s.collector.execute(words, count, [&](const PM4Draw& draw) {
            if (s.stats.failed != failed) return;
            if (s.renderer.encode(draw, command)) ++s.stats.rendered;
            else s.stats.fail(s.renderer.error);
        });
        if (s.stats.failed != failed) { s.error = s.stats.error; return -ENOTSUP; }
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted || command.error) {
            s.error = command.error ? command.error.localizedDescription.UTF8String : "Metal GPU completion failed";
            return -EIO;
        }
        // The native queue retires synchronization packets only after this return.
        return 0;
    }
}
- (id<MTLTexture>)copyTargetAtAddress:(uint64_t)address width:(uint32_t)width height:(uint32_t)height {
    auto& s = *state_;
    std::unique_lock<std::mutex> lock(s.mutex, std::try_to_lock);
    if (!lock || !s.error.empty() || !width || !height || width > 1920 || height > 1080) return nil;
    id<MTLTexture> source = s.renderer.target(address);
    if (!source || source.width < width || source.height < height) return nil;
    auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:source.pixelFormat
        width:width height:height mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [s.device newTextureWithDescriptor:descriptor];
    id<MTLCommandBuffer> command = [s.queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = texture && command ? [command blitCommandEncoder] : nil;
    if (!blit) { s.error = "Metal display copy allocation"; return nil; }
    [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(width,height,1) toTexture:texture destinationSlice:0 destinationLevel:0
        destinationOrigin:MTLOriginMake(0,0,0)];
    [blit endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted || command.error) {
        s.error = "Metal display copy failed"; return nil;
    }
    return texture;
}
- (NSString*)report {
    auto& s = *state_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.error.empty()) return [NSString stringWithUTF8String:s.error.c_str()];
    return [NSString stringWithFormat:@"Metal : %u dessins, %u rendus, %u échecs", s.stats.draws, s.stats.rendered, s.stats.failed];
}
- (NSString*)failure {
    auto& s = *state_;
    std::unique_lock<std::mutex> lock(s.mutex, std::try_to_lock);
    return lock && !s.error.empty() ? [NSString stringWithUTF8String:s.error.c_str()] : nil;
}
@end
