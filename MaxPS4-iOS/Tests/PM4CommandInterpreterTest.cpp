#include "../Native/PM4CommandInterpreter.hpp"
#include <cassert>
#include <cstdio>
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
    const std::uint32_t badReg[] = {(3u<<30)|(1u<<16)|(0x69u<<8), 0x400u, 1u};
    assert(!interpretPM4(badReg, 3).valid);
    const std::uint32_t unknown[] = {(3u<<30)|(0xffu<<8), 0u};
    assert(!interpretPM4(unknown, 2).valid);
    const std::uint32_t noDraw[] = {(3u<<30)|(1u<<16)|(0x2du<<8), 0u, 0u};
    assert(!interpretPM4(noDraw, 3).valid);
    const std::uint32_t truncated[] = {(3u<<30)|(1u<<16)|(0x2du<<8), 3u};
    assert(!interpretPM4(truncated, 2).valid);
    const std::uint32_t ordered[] = {
        0xc0001000, 123, // NOP
        0xc0017600, 0x3ff, 77, // last shader register
        0xc0012d00, 3, 2,
        0xc0017900, 0, 88,
        0xc0016900, 1, 99,
        0xc0012d00, 3, 2,
    };
    auto commands = interpretPM4(ordered, sizeof(ordered) / sizeof(ordered[0]));
    assert(commands.valid && commands.writes.size() == 3 && commands.draws.size() == 2);
    assert(commands.writes[0].bank == RegisterBank::shader && commands.writes[0].offset == 0x3ff);
    assert(commands.writes[1].bank == RegisterBank::uconfig && commands.writes[1].value == 88);
    assert(commands.writes[2].bank == RegisterBank::context && commands.writes[2].value == 99);
    assert(commands.draws[0].writesBefore == 1 && commands.draws[1].writesBefore == 3);
    for (auto opcode : {kSetContextReg, kSetShReg, kSetUconfigReg}) {
        std::uint32_t boundary[] = {0xc0020000U | (std::uint32_t(opcode) << 8), 0x3ff, 1, 2};
        assert(interpretPM4(boundary, 4).commandError == PM4CommandError::invalid_register);
        boundary[1] = 0x3fe;
        assert(interpretPM4(boundary, 4).valid);
        boundary[1] = 0x10000000;
        assert(!interpretPM4(boundary, 4).valid); // unknown offset flags
    }
    const std::uint32_t conditional[] = {0xc0012d01, 3, 2};
    assert(interpretPM4(conditional, 3).commandError == PM4CommandError::unsupported_flags);
    const std::uint32_t partial[] = {0xc0012d00, 3, 2, 0xc000ff00, 0};
    commands = interpretPM4(partial, 5);
    assert(!commands.valid && commands.draws.empty() && commands.writes.empty());
    std::puts("PASS GCN context/shader/uconfig writes, NOP, ordering, boundaries and flags");
}

