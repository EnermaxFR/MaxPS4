#include "../Native/maxps4_guest_elf.h"
#include "GuestELFFixture.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
int main() {
    maxps4::GuestELF image;
    maxps4::GuestELF::Execution state;
    auto file = guest_test::elf();
    assert(image.load(file.data(), file.size(), 3) == 0);
    assert(image.interpret(3, state) == 0);
    assert(state.rax == 42 && state.rip == 0x100b && state.instructions == 3);
    assert(image.interpret(2, state) == -E2BIG);
    assert(state.rip == 0x100a && state.instructions == 2);
    file = guest_test::elf(0xffffffff, 2);
    assert(image.load(file.data(), file.size(), 3) == 0);
    assert(image.interpret(3, state) == 0 && state.rax == 1);
    file = guest_test::generated_pm4();
    assert(image.load(file.data(), file.size(), 6) == 0);
    const auto* data = image.translate(0x10000, 12, 6);
    assert(data && data[0] == 0);
    assert(image.interpret(6, state) == 0);
    assert(state.rax == 42 && state.rip == 0x1029 && state.instructions == 6);
    assert(data[0] == 0 && data[1] == 0x2d && data[2] == 1 && data[3] == 0xc0);
    assert(data[4] == 3 && data[8] == 2);
    guest_test::put(file, 0, 196, 4); // store targets RX code: reject before execution
    assert(image.load(file.data(), file.size(), 6) == -ENOTSUP);
    assert(image.interpret(6, state) == -EINVAL && state.instructions == 0);
    std::puts("ELF interpreter: arithmetic, guest RIP, budget, real stores and RX isolation passed");
}
