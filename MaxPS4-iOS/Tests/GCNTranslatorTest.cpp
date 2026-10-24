// Stage 1 of the translator test: translates the shared test programs and
// writes the generated functions to argv[1] for the C++ harness (stage 2,
// GCNTranslatorHarness.cpp), which runs them against the interpreter.
#include "../Native/GCNTranslator.hpp"
#include "GCNTestPrograms.h"
#include <cassert>
#include <cstdio>
#include <fstream>
using namespace maxps4::gpu;
using namespace gcn_test;
int main(int argc, char** argv) {
    assert(argc == 2);
    Arena arena;
    loadEmbeddedVS(arena); loadQuadPS(arena); loadFetchVS(arena);
    GCNTranslateOptions vs; vs.stage = GCNStage::vertex; vs.name = "vs_embedded";
    const auto embedded = translateGCN(arena.memory, embeddedVS, vs);
    assert(embedded && embedded.position && embedded.params == 1 && !embedded.quad);
    GCNTranslateOptions ps; ps.name = "ps_quad";
    const auto quad = translateGCN(arena.memory, quadPS, ps);
    if (!quad) std::printf("%s\n", quad.error.c_str());
    assert(quad && quad.colors == 3 && quad.attributes == 1 && quad.flat == 0 && quad.quad);
    GCNTranslateOptions fetch; fetch.stage = GCNStage::vertex; fetch.name = "vs_fetch";
    fetch.userCount = 4; fetch.userData[0] = std::uint32_t(fetchShader); fetch.userData[2] = std::uint32_t(vertexTable);
    const auto fetched = translateGCN(arena.memory, fetchVS, fetch);
    if (!fetched) std::printf("%s\n", fetched.error.c_str());
    assert(fetched && fetched.position && fetched.params == 1);
    // Refusals: no fetch-shader address, interpolation in a vertex program,
    // unmapped code.
    GCNTranslateOptions noUser = fetch; noUser.userCount = 0;
    assert(translateGCN(arena.memory, fetchVS, noUser).error == "s_swappc_b64 target not in user SGPRs");
    GCNTranslateOptions psAsVertex = ps; psAsVertex.stage = GCNStage::vertex;
    assert(translateGCN(arena.memory, quadPS, psAsVertex).error.find("interpolation outside a pixel program") == 0);
    assert(translateGCN(arena.memory, 0x40000, ps).error == "unreadable code");
    // The Metal prelude carries the shared runtime text.
    const std::string prelude = gcnMetalPrelude();
    assert(prelude.find("#include <metal_stdlib>") == 0 && prelude.find("GCN_FN float4 gcn_sample(") != std::string::npos);
    std::ofstream out(argv[1]);
    out << embedded.source << quad.source << fetched.source;
    std::puts("PASS GCN translator stage 1: embedded VS, quad PS and fetch-shader VS translated; refusals reported");
}
