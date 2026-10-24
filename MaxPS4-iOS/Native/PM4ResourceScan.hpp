#pragma once
// The guest memory a draw's programs read on the GPU, found on the CPU: one
// lane of the vertex program (first index) and one of the pixel program (a
// pixel at the scissor centre) run in the GCN interpreter, which reports the
// whole extent of every scalar load, V# and T# they address. The GPU then
// receives copies of these ranges only (PM4MetalRenderer), never the whole
// guest address space. A resource used only on a path that representative
// lane does not take is missed and reads as zero on the GPU, never outside.
#include "PM4DrawCollector.hpp"
#include <algorithm>

namespace maxps4::gpu {
struct PM4Range { std::uint64_t base = 0, size = 0; };

inline std::vector<PM4Range> scanDrawResources(const GCNMemory& memory, const PM4Draw& d) {
    constexpr std::uint64_t largest = 256ull << 20;
    std::vector<PM4Range> found;
    auto note = [&](std::uint64_t base, std::uint64_t bytes) {
        if (bytes && bytes <= largest && base + bytes > base) found.push_back({base, bytes});
    };
    GCNCodeCache cache;
    {
        GCNWave wave(memory, 1, &cache);
        wave.resource = note;
        for (unsigned k = 0; k < d.vsUserCount; ++k) wave.s[k] = d.vsUser[k];
        wave.v(0, 0) = d.indices.empty() ? 0 : d.indices[0];
        wave.run(d.vs); // a failure keeps what was found before it
    }
    {
        GCNWave wave(memory, 1, &cache);
        wave.resource = note;
        for (unsigned k = 0; k < d.psUserCount; ++k) wave.s[k] = d.psUser[k];
        const float x = float(d.scissor[0] + d.scissor[2]) / 2, y = float(d.scissor[1] + d.scissor[3]) / 2;
        static const unsigned sizes[16] = {2, 2, 2, 3, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        unsigned reg = 0;
        for (unsigned bit = 0; bit < 16; ++bit) {
            if (!((d.psInputAddr >> bit) & 1)) continue;
            float value[3] = {1.0f / 3, 1.0f / 3, 1.0f / 3};
            if (bit == 8) value[0] = x + 0.5f;
            else if (bit == 9) value[0] = y + 0.5f;
            for (unsigned k = 0; k < sizes[bit]; ++k) wave.v(reg + k, 0) = gcn_detail::u32(value[k]);
            reg += sizes[bit];
        }
        wave.attribute = [](unsigned, unsigned) { return GCNAttributePlane{}; };
        wave.run(d.ps);
    }
    // Sorted, with overlapping or nearly adjacent (4 KiB) ranges merged.
    std::sort(found.begin(), found.end(), [](const PM4Range& a, const PM4Range& b) { return a.base < b.base; });
    std::vector<PM4Range> merged;
    for (const auto& r : found) {
        if (!merged.empty() && r.base <= merged.back().base + merged.back().size + 4096) {
            merged.back().size = std::max(merged.back().size, r.base + r.size - merged.back().base);
        } else merged.push_back(r);
    }
    return merged;
}
} // namespace maxps4::gpu
