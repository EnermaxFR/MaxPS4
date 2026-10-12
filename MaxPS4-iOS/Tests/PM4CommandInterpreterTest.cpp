#include "../Native/PM4CommandInterpreter.hpp"
#include <cassert>
using namespace maxps4::gpu;
int main() {
    const std::uint32_t stream[] = {
        (3u<<30)|(2u<<16)|(0x69u<<8), 4u, 0x123u, 0x456u,
        (3u<<30)|(1u<<16)|(0x2du<<8), 3u, 0u
    };
    const auto valid = interpretPM4(stream, 7);
    assert(valid.valid && valid.writes.size() == 2 && valid.draws.size() == 1);
    assert(valid.writes[1].offset == 5 && valid.writes[1].value == 0x456);
    assert(valid.draws[0].indexCount == 3);
    const std::uint32_t badReg[] = {(3u<<30)|(1u<<16)|(0x69u<<8), 0x3ffu, 1u};
    assert(!interpretPM4(badReg, 3).valid);
    const std::uint32_t unknown[] = {(3u<<30)|(0xffu<<8), 0u};
    assert(!interpretPM4(unknown, 2).valid);
    const std::uint32_t noDraw[] = {(3u<<30)|(1u<<16)|(0x2du<<8), 0u, 0u};
    assert(!interpretPM4(noDraw, 3).valid);
    const std::uint32_t truncated[] = {(3u<<30)|(1u<<16)|(0x2du<<8), 3u};
    assert(!interpretPM4(truncated, 2).valid);
}
