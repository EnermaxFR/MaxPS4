// Stage 2 of the translator test: compiles the functions stage 1 generated
// (GCN_GENERATED, a quoted path) as C++ and checks every lane against the
// GCN interpreter on the same programs and guest memory.
#include "../Native/GCNShaderRuntime.hpp"
#include "GCNTestPrograms.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <thread>

namespace maxps4::gpu::gcnrt {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include GCN_GENERATED
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}
using namespace maxps4::gpu;
using namespace gcn_test;
namespace rt = maxps4::gpu::gcnrt;
namespace {
bool same(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
rt::GCNMemory window(const Arena& arena) { return {arena.bytes.data(), Arena::start, arena.bytes.size()}; }
}
int main() {
    Arena arena;
    loadEmbeddedVS(arena); loadQuadPS(arena); loadFetchVS(arena);
    const rt::GCNMemory M = window(arena);
    rt::GCNQuad unusedQuad;
    // Vertex programs: one lane per vertex, against one interpreter wave.
    {
        GCNWave wave(arena.memory, 3);
        for (unsigned lane = 0; lane < 3; ++lane) wave.v(0, lane) = lane;
        assert(wave.run(embeddedVS));
        for (unsigned lane = 0; lane < 3; ++lane) {
            rt::GCNLane L;
            L.vinit[0] = lane;
            rt::vs_embedded(L, M, unusedQuad);
            for (unsigned c = 0; c < 4; ++c) {
                assert(same(L.pos[c], gcn_detail::f32(wave.exports[0].value[c][lane])));
                assert(same(L.param[0][c], gcn_detail::f32(wave.exports[1].value[c][lane])));
            }
        }
    }
    {
        GCNWave wave(arena.memory, 4);
        wave.s[0] = std::uint32_t(fetchShader); wave.s[2] = std::uint32_t(vertexTable);
        for (unsigned lane = 0; lane < 4; ++lane) wave.v(0, lane) = lane;
        assert(wave.run(fetchVS));
        for (unsigned lane = 0; lane < 4; ++lane) {
            rt::GCNLane L;
            L.user[0] = std::uint32_t(fetchShader); L.user[2] = std::uint32_t(vertexTable);
            L.vinit[0] = lane;
            rt::vs_fetch(L, M, unusedQuad);
            for (unsigned c = 0; c < 4; ++c) {
                assert(same(L.pos[c], gcn_detail::f32(wave.exports[0].value[c][lane])));
                assert(same(L.param[0][c], gcn_detail::f32(wave.exports[1].value[c][lane])));
            }
        }
        // Spot check against the known data, not only the interpreter.
        rt::GCNLane L; L.user[0] = std::uint32_t(fetchShader); L.user[2] = std::uint32_t(vertexTable); L.vinit[0] = 2;
        rt::vs_fetch(L, M, unusedQuad);
        assert(L.pos[0] == 5 && L.pos[1] == 6 && L.param[0][0] == 5 * 9 + 6);
    }
    // The pixel quad: four lanes on four threads in lockstep, lane 3 a helper.
    {
        GCNWave wave(arena.memory, 4);
        for (unsigned n = 0; n < 16; ++n) wave.s[n] = quadSgprs[n];
        wave.setExec(0x7);
        for (unsigned lane = 0; lane < 4; ++lane) { wave.v(0, lane) = bits(quadI[lane]); wave.v(1, lane) = 0; }
        wave.attribute = [](unsigned, unsigned chan) { return quadPlanes[chan & 1]; };
        assert(wave.run(quadPS));
        rt::GCNQuad quad;
        rt::GCNLane lanes[4];
        std::thread threads[4];
        for (unsigned lane = 0; lane < 4; ++lane) {
            rt::GCNLane& L = lanes[lane];
            for (unsigned n = 0; n < 16; ++n) L.user[n] = quadSgprs[n];
            L.vinit[0] = bits(quadI[lane]); L.vinit[1] = 0;
            L.exec = lane != 3; L.quadLane = lane;
            for (unsigned c = 0; c < 2; ++c) L.plane[0][c] = {quadPlanes[c].p0, quadPlanes[c].p10, quadPlanes[c].p20};
            threads[lane] = std::thread([&, lane] { rt::ps_quad(lanes[lane], M, quad); });
        }
        for (auto& t : threads) t.join();
        const GCNExport& ddx = wave.exports[0];
        const GCNExport& color = wave.exports[1];
        for (unsigned lane = 0; lane < 4; ++lane) {
            const rt::GCNLane& L = lanes[lane];
            const bool live = (color.mask >> lane) & 1;
            assert(L.killed == !live && ((L.colorWritten & 1) != 0) == live && ((L.colorWritten & 2) != 0) == live);
            if (!live) continue;
            assert(same(L.color[1][0], gcn_detail::f32(ddx.value[0][lane])));
            const float expected[4] = {gcn_detail::f16(color.value[0][lane] & 0xffff), gcn_detail::f16(color.value[0][lane] >> 16),
                                       gcn_detail::f16(color.value[1][lane] & 0xffff), gcn_detail::f16(color.value[1][lane] >> 16)};
            for (unsigned c = 0; c < 4; ++c) assert(same(L.color[0][c], expected[c]));
        }
        assert(lanes[0].color[0][0] == 1.0f && lanes[0].color[0][1] == 0.5f && lanes[1].color[0][1] == 1.0f);
    }
    std::puts("PASS GCN translator stage 2: translated C++ matches the interpreter lane for lane (VS, fetch VS, pixel quad)");
}
