#include "../Native/maxps4_ps4_libkernel.h"
#include "GuestELFFixture.h"
#include <cassert>
#include <cstdio>

using namespace maxps4;
using namespace maxps4::ps4;
int main() {
    GuestELF image;
    auto elf = guest_test::elf(40, 2, true);
    guest_test::put(elf, 8192, 160, 8);
    assert(image.load(elf.data(), elf.size(), 6) == 0);
    PS4VirtualMemory dynamic(image);
    PS4GuestMemory memory(image, &dynamic);
    PS4Libkernel kernel(memory, &dynamic);
    using S = PS4KernelService;
    const auto invoke = [&](S s, const PS4KernelCall& c) { return kernel.invoke(s, c); };
    uint8_t word[8]{};
    put_le(word, 0, 0x10100, 8);
    assert(memory.write(0x11008, word, 8));
    PS4KernelCall map{0, 3 * PS4VirtualMemory::page_size, 3, 0x1002, uint64_t(-1LL), 0, 0x11000};
    assert(invoke(S::mmap, map) == 0);
    assert(memory.read(0x10100, word, 8));
    const uint64_t address = get_le(word, 8), page = PS4VirtualMemory::page_size;
    assert(address >= PS4VirtualMemory::base && address % page == 0);
    assert(dynamic.allocated() == 3 * page);
    uint8_t byte = 0xff;
    assert(memory.read(address, &byte, 1) && byte == 0);
    byte = 42;
    assert(memory.write(address + page, &byte, 1));
    assert(!memory.fetch(address, &byte, 1));
    assert(memory.permitted(address, 3 * page, 6));
    assert(memory.contiguous(address + 1, SIZE_MAX, 6) == 3 * page - 1);
    assert(!memory.read(address + 3 * page - 1, word, 8));
    assert(invoke(S::mprotect, {address + page, page, 1}) == 0);
    assert(!memory.write(address + page, word, 1));
    assert(memory.read(address + page, &byte, 1) && byte == 42);
    assert(memory.contiguous(address, 3 * page, 2) == page);
    assert(invoke(S::mprotect, {address, page, 5}) == 0);
    assert(memory.fetch(address, &byte, 1));
    assert(!memory.write(address, word, 1));
    assert(invoke(S::mprotect, {address, page, 7}) == kernel_error(access));
    assert(memory.executable(address, 1)); // Failed protect preserves permissions.
    assert(invoke(S::mprotect, {address + 1, page, 3}) == kernel_error(inval));
    assert(invoke(S::mprotect, {address, page, 8}) == kernel_error(notsup));
    assert(invoke(S::mprotect, {address + 2 * page, page, 0}) == 0);
    assert(!memory.read(address + 2 * page, &byte, 1));
    assert(invoke(S::munmap, {address + page, page}) == 0);
    assert(!memory.read(address + page, &byte, 1));
    assert(!memory.read(address + page - 1, word, 8));
    assert(invoke(S::mprotect, {address, 3 * page, 3}) == kernel_error(nomem));
    assert(memory.executable(address, 1)); // No partial protect across a hole.
    assert(invoke(S::munmap, {address + page, page}) == kernel_error(nomem));
    assert(invoke(S::munmap, {address + 1, page}) == kernel_error(inval));
    assert(invoke(S::munmap, {address, 0}) == kernel_error(inval));
    assert(invoke(S::munmap, {address, page}) == 0);
    assert(invoke(S::munmap, {address + 2 * page, page}) == 0);
    assert(dynamic.allocated() == 0);
    // All map failures preserve the output pointer and allocation accounting.
    put_le(word, 0, 0xdeadbeef, 8); assert(memory.write(0x10100, word, 8));
    auto invalid = map; invalid.rdi = 0x10000;
    assert(invoke(S::mmap, invalid) == kernel_error(notsup));
    invalid = map; invalid.rcx = 0x1012;
    assert(invoke(S::mmap, invalid) == kernel_error(notsup));
    invalid = map; invalid.r8 = 3;
    assert(invoke(S::mmap, invalid) == kernel_error(inval));
    invalid = map; invalid.r9 = 1;
    assert(invoke(S::mmap, invalid) == kernel_error(inval));
    invalid = map; invalid.rsi = UINT64_MAX;
    assert(invoke(S::mmap, invalid) == kernel_error(inval));
    invalid = map; invalid.rsi = PS4VirtualMemory::budget + 1;
    assert(invoke(S::mmap, invalid) == kernel_error(nomem));
    invalid = map; invalid.rsp = UINT64_MAX;
    assert(invoke(S::mmap, invalid) == kernel_error(fault));
    assert(memory.read(0x10100, word, 8) && get_le(word, 8) == 0xdeadbeef);
    assert(dynamic.allocated() == 0);
    put_le(word, 0, 0x1000, 8); assert(memory.write(0x11008, word, 8));
    assert(invoke(S::mmap, map) == kernel_error(fault));
    assert(dynamic.allocated() == 0);
    uint64_t output = 0xdeadbeef;
    assert(dynamic.map(0, 1, 3, 0x1002, -1, 0, output) == ok);
    assert(dynamic.allocated() == page);
    dynamic.reset();
    assert(dynamic.map(0, PS4VirtualMemory::budget, 3, 0x1002, -1, 0, output) == ok);
    uint64_t unchanged = 42;
    assert(dynamic.map(0, 1, 3, 0x1002, -1, 0, unchanged) == nomem && unchanged == 42);
    kernel.reset(); assert(dynamic.allocated() == 0);
    // Allocation must skip a staged ELF segment at the default virtual base.
    GuestELF high_image;
    guest_test::put(elf, PS4VirtualMemory::base, 80, 8);
    guest_test::put(elf, PS4VirtualMemory::base, 24, 8);
    guest_test::put(elf, PS4VirtualMemory::base + page, 136, 8);
    assert(high_image.load(elf.data(), elf.size(), 6) == 0);
    PS4VirtualMemory high_dynamic(high_image);
    assert(high_dynamic.map(0, 1, 3, 0x1002, -1, 0, output) == ok);
    assert(output >= PS4VirtualMemory::base + 2 * page);
    // Clock wire structs use two signed 64-bit words, including on Apple ARM64.
    uint8_t time[16]{};
    for (uint32_t id : {0U, 4U}) {
        assert(invoke(S::clock_gettime, {id, 0x11100}) == 0);
        assert(memory.read(0x11100, time, sizeof(time)));
        assert(get_le(time, 8) > 0 && get_le(time + 8, 8) < 1000000000);
        const uint64_t seconds = get_le(time, 8), nanos = get_le(time + 8, 8);
        assert(invoke(S::clock_gettime, {id, 0x11100}) == 0);
        assert(memory.read(0x11100, time, sizeof(time)));
        if (id == 4) assert(get_le(time, 8) > seconds ||
            (get_le(time, 8) == seconds && get_le(time + 8, 8) >= nanos));
        assert(invoke(S::clock_getres, {id, 0x11100}) == 0);
        assert(memory.read(0x11100, time, sizeof(time)));
        assert(get_le(time, 8) || get_le(time + 8, 8));
    }
    assert(invoke(S::clock_gettime, {1, 0x11100}) == kernel_error(inval));
    assert(invoke(S::clock_gettime, {4, 0x1000}) == kernel_error(fault));
    assert(invoke(S::gettimeofday, {0x11100}) == 0);
    assert(memory.read(0x11100, time, sizeof(time)));
    assert(get_le(time, 8) > 0 && get_le(time + 8, 8) < 1000000);
    assert(invoke(S::gettimeofday, {0}) == kernel_error(fault));
    const auto elapsed = invoke(S::process_time, {});
    assert(elapsed >= 0 && invoke(S::process_time, {}) >= elapsed);
    assert(resolve_libkernel("PGhQHd-dzv8", "libkernel", "libkernel", 1) == S::mmap);
    assert(resolve_libkernel("QBi7HCK03hw", "libkernel", "libkernel", 1) == S::clock_gettime);
    std::puts("PS4 virtual memory and clock checks passed");
}
