// One PM4 stream drawn by the software renderer (CPU, GCN interpreter) and
// by PM4MetalRenderer (GPU, translated programs); the colour targets must
// match. Scene A: two rect lists through the embedded VS, the second one
// scissored, so the Metal target must keep the first draw. Scene B: an
// indexed draw through a fetch-shader VS and a textured pixel program.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../Native/PM4MetalRenderer.h"
#include "../Native/PM4GameMetalBridge.h"
#include "../Native/PM4SoftwareRenderer.hpp"
#include "GCNTestPrograms.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
using namespace maxps4::gpu;
using namespace gcn_test;
namespace {
constexpr std::uint64_t gradientPS = 0x10100, greenPS = 0x10200, targetA = 0x11000;
constexpr std::uint64_t texturedPS = 0x11400, image = 0x14000, indexBuffer = 0x15000, targetB = 0x18000;
constexpr unsigned width = 16, height = 8;
struct Stream {
    std::vector<std::uint32_t> words;
    void set(std::uint8_t opcode, std::uint32_t offset, std::initializer_list<std::uint32_t> values) {
        words.push_back(0xc0000000U | (std::uint32_t(values.size()) << 16) | (std::uint32_t(opcode) << 8));
        words.push_back(offset);
        words.insert(words.end(), values);
    }
    void target(std::uint64_t base) {
        set(0x69, 0x318, {std::uint32_t(base >> 8), width / 8 - 1, width * height / 64 - 1, 0, (0xaU << 2) | (1U << 11), 0});
        set(0x69, 0x8e, {0xf});
        set(0x69, 0x10f, {bits(8), bits(8), bits(-4), bits(4), bits(0.5f), bits(0.5f)});
        set(0x69, 0x206, {0x43f});
        set(0x69, 0x0c, {0, width | (height << 16)});
        set(0x69, 0x191, {0});
    }
};
bool compare(const char* scene, const std::uint8_t* metal, const std::uint8_t* software) {
    int worst = 0;
    for (unsigned n = 0; n < width * height * 4; ++n) worst = std::max(worst, std::abs(int(metal[n]) - int(software[n])));
    if (worst > 2) {
        std::printf("%s differs (Metal/software, ARGB):\n", scene);
        for (unsigned y = 0; y < height; ++y, std::puts("")) for (unsigned x = 0; x < width; ++x) {
            const unsigned at = 4 * (y * width + x);
            std::printf("%02x%02x%02x%02x/%02x%02x%02x%02x ", metal[at + 3], metal[at + 2], metal[at + 1], metal[at],
                        software[at + 3], software[at + 2], software[at + 1], software[at]);
        }
    }
    std::printf("%s: worst channel difference %d\n", scene, worst);
    return worst <= 2;
}
}
int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { std::puts("SKIP PM4 Metal renderer: no Metal device"); return 0; }
        Arena arena;
        loadEmbeddedVS(arena); loadFetchVS(arena);
        const float triangle[3][4] = {{-1, -1, 0, 1}, {3, -1, 0, 1}, {-1, 3, 0, 1}};
        for (unsigned n = 0; n < 3; ++n) for (unsigned c = 0; c < 4; ++c) arena.put(0x10e00 + 16 * n + 4 * c, bits(triangle[n][c]));
        arena.code(gradientPS, {
            0xc80c0000, 0xc80d0001, // v_interp_p1/p2_f32 v3, attr0.x (= 1.0)
            0x100804ff, 0x3d800000, // v_mul_f32 v4, 0.0625, v2 (POS_X / 16)
            0x7e0a0280,             // v_mov_b32 v5, 0
            0xf800180f, 0x03050504, // exp mrt0, v4, v5, v5, v3 done vm
            0xbf810000});
        arena.code(greenPS, {
            0x7e080280,             // v_mov_b32 v4, 0
            0x7e0a02f2,             // v_mov_b32 v5, 1.0
            0xf800180f, 0x05040504, // exp mrt0, v4, v5, v4, v5 done vm
            0xbf810000});
        arena.code(texturedPS, {
            0xbe94047e, 0xbefe0a7e, 0xc8280000, 0xc8290001, 0x100804ff, 0x3d800000, 0x100a06ff, 0x3e000000,
            0xf0800f00, 0x00400604, 0xd8d48055, 0x0b000004, 0xd8d48000, 0x0c000004, 0x0816190b,
            0x100e16ff, 0x41000000, 0x061014ff, 0x41200000, 0x101010ff, 0x3ccccccd, 0x7c0208f4, 0xbf860001,
            0x7e120280, 0xbefe0414, 0xf800180f, 0x09080706, 0xbf810000}); // as GCNMetalTest's pixel program
        for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
            arena.put(image + 4 * (y * width + x), (x * 16) | ((y * 32) << 8) | (128u << 16) | (255u << 24));
        arena.put(indexBuffer, 0x00010000); arena.put(indexBuffer + 4, 0x00000002); // 16-bit 0, 1, 2
        Stream s;
        // Scene A.
        s.set(0x76, 0x48, {std::uint32_t(embeddedVS >> 8), 0, 0, 0});
        s.set(0x76, 0x08, {std::uint32_t(gradientPS >> 8), 0, 0, 0});
        s.target(targetA);
        s.set(0x69, 0x1b3, {0x102, 0x102}); // PERSP_CENTER, POS_X
        s.set(0x79, 0x242, {0x11});
        s.words.insert(s.words.end(), {0xc0012d00U, 3, 2});
        s.set(0x76, 0x08, {std::uint32_t(greenPS >> 8), 0, 0, 0});
        s.set(0x69, 0x0c, {0, 4 | (height << 16)});
        s.words.insert(s.words.end(), {0xc0012d00U, 3, 2});
        // Scene B.
        s.set(0x76, 0x48, {std::uint32_t(fetchVS >> 8), 0, 0, 4u << 1});
        s.set(0x76, 0x4c, {std::uint32_t(fetchShader), 0, std::uint32_t(vertexTable), 0});
        s.set(0x76, 0x08, {std::uint32_t(texturedPS >> 8), 0, 0, 12u << 1});
        s.set(0x76, 0x0c, {std::uint32_t(image >> 8), 10u << 20, (width - 1) | ((height - 1) << 14), 0x90800facu,
                           (width - 1) << 13, 0, 0, 0, 0x12, 0, 0x500000, 0});
        s.target(targetB);
        s.set(0x69, 0x1b3, {0x302, 0x302}); // PERSP_CENTER, POS_X, POS_Y
        s.set(0x79, 0x242, {4});
        s.words.insert(s.words.end(), {0xc0002a00U, 0});                                    // INDEX_TYPE 16-bit
        s.words.insert(s.words.end(), {0xc0042700U, 3, std::uint32_t(indexBuffer), 0, 3, 0}); // DRAW_INDEX_2
        PM4DrawStats metalStats;
        PM4DrawCollector collector(arena.memory, metalStats);
        // No guest window: each draw gets copies of the ranges its programs read,
        // taken from guest memory as it is before either renderer draws.
        PM4MetalRenderer metal(device, arena.memory, nil, 0);
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        collector.execute(s.words.data(), s.words.size(), [&](const PM4Draw& d) {
            if (metal.encode(d, commands)) ++metalStats.rendered;
            else metalStats.fail(metal.error);
        });
        [commands commit]; [commands waitUntilCompleted];
        if (!metalStats.error.empty()) std::printf("metal: %s\n", metalStats.error.c_str());
        assert(commands.status == MTLCommandBufferStatusCompleted && !commands.error);
        assert(metalStats.draws == 3 && metalStats.rendered == 3 && metalStats.failed == 0 && metal.pipelinesBuilt == 3);
        // Scene B read the vertex table and vertices (one merged range) and the texture.
        assert(metal.lastRanges == 2);
        PM4SoftwareRenderer software(arena.memory);
        software.execute(s.words.data(), s.words.size());
        if (!software.stats.error.empty()) std::printf("software: %s\n", software.stats.error.c_str());
        assert(software.stats.rendered == 3 && software.stats.failed == 0);
        bool ok = true;
        for (const auto& [name, address] : {std::pair<const char*, std::uint64_t>{"scene A", targetA}, {"scene B", targetB}}) {
            id<MTLTexture> texture = metal.target(address);
            assert(texture && texture.pixelFormat == MTLPixelFormatBGRA8Unorm);
            std::uint8_t pixels[width * height * 4] = {};
            [texture getBytes:pixels bytesPerRow:width * 4 fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
            ok = compare(name, pixels, arena.bytes.data() + (address - Arena::start)) && ok;
        }
        assert(ok);
        // The app bridge receives separate submissions; register state must
        // persist. It returns immutable cropped display copies, not live targets.
        Arena* reader = &arena;
        MaxPS4GameMetalBridge* bridge = [[MaxPS4GameMetalBridge alloc] initWithDevice:device
            reader:^BOOL(uint64_t address, void* destination, size_t count) {
                return reader->memory.read(address,destination,count);
            }];
        assert(bridge && !bridge.failure);
        for (const auto& packet : decodePM4Type3(s.words.data(),s.words.size()).packets)
            assert([bridge submit:s.words.data()+packet.headerWord count:packet.payloadCount+1]==0);
        auto readback = [&](id<MTLTexture> source, std::uint8_t* pixels) {
            assert(source);
            if (source.storageMode == MTLStorageModeShared) {
                [source getBytes:pixels bytesPerRow:width*4 fromRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0];
                return;
            }
            auto* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:source.pixelFormat
                width:width height:height mipmapped:NO];
            desc.storageMode = MTLStorageModeShared;
            id<MTLTexture> readable = [device newTextureWithDescriptor:desc];
            id<MTLCommandBuffer> command = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(width,height,1) toTexture:readable destinationSlice:0 destinationLevel:0
                destinationOrigin:MTLOriginMake(0,0,0)];
            [blit endEncoding]; [command commit]; [command waitUntilCompleted];
            assert(command.status==MTLCommandBufferStatusCompleted && !command.error);
            [readable getBytes:pixels bytesPerRow:width*4 fromRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0];
        };
        id<MTLTexture> saved = [bridge copyTargetAtAddress:targetB width:width height:height];
        assert(saved && ![bridge copyTargetAtAddress:0 width:width height:height]);
        assert(![bridge copyTargetAtAddress:targetA width:width+1 height:height]);
        std::uint8_t before[width*height*4]{}, after[sizeof before]{};
        readback([bridge copyTargetAtAddress:targetA width:width height:height],after);
        const bool bridgeA=compare("app bridge A",after,arena.bytes.data()+(targetA-Arena::start));
        readback(saved,before);
        const bool bridgeB=compare("app bridge B",before,arena.bytes.data()+(targetB-Arena::start));
        assert(bridgeA && bridgeB);
        Stream next;
        next.set(0x76,0x08,{std::uint32_t(greenPS>>8),0,0,0});
        next.words.insert(next.words.end(),{0xc0012d00U,3,2});
        assert([bridge submit:next.words.data() count:next.words.size()]==0);
        readback(saved,after); assert(std::memcmp(before,after,sizeof before)==0);
        id<MTLTexture> changed = [bridge copyTargetAtAddress:targetB width:width height:height];
        readback(changed,after); assert(std::memcmp(before,after,sizeof before)!=0);
        const uint32_t bad[]={0};
        assert([bridge submit:bad count:1]!=0 && bridge.failure);
        assert([bridge submit:s.words.data() count:s.words.size()]!=0);
        std::puts("PASS app Metal bridge: persistent submissions, GPU completion, display copies, failure latch");
        // Scene A: green where the second draw's scissor allowed, gradient elsewhere.
        const std::uint8_t* a = arena.bytes.data() + (targetA - Arena::start);
        assert(a[1] == 255 && a[2] == 0 && a[4 * 8 + 2] == 135 && a[4 * 8 + 1] == 0);
        std::puts("PASS PM4 stream -> Metal renderer (translated GCN programs) matches the software renderer: rect lists, scissor, persistence, indexed fetch-shader draw, texture sampling");
    }
}
