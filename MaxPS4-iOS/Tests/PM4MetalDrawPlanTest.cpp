#include "../Native/PM4MetalDrawPlan.hpp"
#include <cassert>
#include <cstdio>
using namespace maxps4::gpu;
int main() {
    uint32_t words[] = {0xc0012d00, 3, 2, 0xc0012d00, 3, 2};
    assert(planPM4Triangles(words, 6).drawCount == 2);
    words[5] = 0; // DMA source is not an auto-index draw.
    assert(planPM4Triangles(words, 6).error == PM4MetalError::unsupported_draw);
    assert(planPM4Triangles(words, 6).drawCount == 0);
    words[5] = 2; words[4] = 6;
    assert(planPM4Triangles(words, 6).error == PM4MetalError::unsupported_draw);
    assert(planPM4Triangles(words, 5).error == PM4MetalError::invalid_stream);
    assert(planPM4Triangles(nullptr, 1).error == PM4MetalError::invalid_stream);
    assert(planPM4Triangles(nullptr, 0).error == PM4MetalError::unsupported_draw);
    const uint32_t state[] = {0xc0016900, 0, 1, 0xc0012d00, 3, 2};
    assert(planPM4Triangles(state, 6).error == PM4MetalError::unsupported_state);
    const uint32_t indexed[] = {0xc0042700,3,0x1000,0,3,0};
    assert(planPM4Triangles(indexed,6).error == PM4MetalError::unsupported_draw);
    const uint32_t instanced[] = {0xc0002f00,2,0xc0012d00,3,2};
    assert(planPM4Triangles(instanced,5).error == PM4MetalError::unsupported_draw);
    std::puts("PASS PM4 Metal preflight: complete validation, no ignored state, auto-index source");
}
