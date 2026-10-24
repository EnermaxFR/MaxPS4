#pragma once
// Executes PM4Draws (PM4DrawCollector.hpp) on a Metal device with the
// game's own programs translated by GCNTranslator.hpp. Objective-C++ only.
// Guest memory reaches the GPU as one MTLBuffer window; code is read through
// a GCNMemory when a pipeline is built. Each colour target address owns a
// Metal texture (BGRA8 or RGBA8 per COMP_SWAP), created cleared and kept
// across draws. Pipelines are cached by program addresses, the fetch-shader
// SGPR pair (s[0:1]), PS inputs, format and blend. Limits as the software
// renderer: colour target 0 only, no depth, instance 0.
#ifdef __OBJC__
#import <Metal/Metal.h>
#include "PM4DrawCollector.hpp"
#include <map>
#include <string>
#include <tuple>

namespace maxps4::gpu {
class PM4MetalRenderer {
public:
    PM4MetalRenderer(id<MTLDevice> device, const GCNMemory& code, id<MTLBuffer> guest, std::uint64_t guestBase)
        : device_(device), code_(code), guest_(guest), guestBase_(guestBase) {}
    // Encodes one draw as a render pass into `commands`. False with `error`
    // set (and nothing encoded) when the draw cannot be expressed.
    bool encode(const PM4Draw& draw, id<MTLCommandBuffer> commands);
    // The texture holding colour target `address`, or nil if never drawn.
    id<MTLTexture> target(std::uint64_t address) const;
    std::string error;
    unsigned pipelinesBuilt = 0;

private:
    using PipelineKey = std::tuple<std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t, std::uint32_t, std::string, unsigned, std::uint32_t, unsigned>;
    struct Target { id<MTLTexture> texture; bool fresh; };
    id<MTLDevice> device_;
    const GCNMemory& code_;
    id<MTLBuffer> guest_;
    std::uint64_t guestBase_;
    std::map<PipelineKey, id<MTLRenderPipelineState>> pipelines_;
    std::map<std::uint64_t, Target> targets_;
    id<MTLRenderPipelineState> pipeline(const PM4Draw& draw, MTLPixelFormat format);
    bool fail(const std::string& message) { error = message; return false; }
};
} // namespace maxps4::gpu
#endif
