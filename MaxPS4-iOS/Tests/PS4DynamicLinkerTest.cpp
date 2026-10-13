#include "../Native/maxps4_ps4_linker.h"
#include "PS4LinkedELFFixture.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>

int main() {
    using namespace ps4_linked_test;
    maxps4::GuestELF image;
    maxps4::PS4DynamicLinker linker;
    auto good = minimal();
    auto load = [&](const std::vector<uint8_t>& file, uint32_t budget = 1000) {
        return linker.load(image,file.data(),file.size(),budget);
    };
    assert(load(good) == 0 && image.entry() == entry && linker.imports().size() == 1);
    assert(image.ps4_blockers() == 4);
    maxps4::PS4GuestMemory memory(image);
    uint8_t bytes[16]{};
    assert(memory.read(data,bytes,8));
    const auto& binding = linker.imports().front();
    assert(maxps4::ps4::get_le(bytes,8) == binding.thunk); // Logical, never host pointer.
    assert(memory.fetch(entry,bytes,6) && bytes[0] == 0xe8 && bytes[5] == 0x90);
    assert(memory.fetch(binding.thunk,bytes,11));
    assert(bytes[0] == 0x49 && bytes[8] == 0x0f && bytes[9] == 0x05 && bytes[10] == 0xc3);
    assert(!memory.write(binding.thunk,bytes,1));
    maxps4::PS4KernelService service{};
    const uint64_t cpu_gate = reinterpret_cast<uintptr_t>(image.translate(binding.thunk+8,2,5));
    assert(linker.gate(image,cpu_gate,static_cast<uint32_t>(binding.service),service));
    assert(service == maxps4::PS4KernelService::thread_self);
    assert(!linker.gate(image,cpu_gate+1,static_cast<uint32_t>(binding.service),service));
    assert(!linker.gate(image,cpu_gate,999,service));
    auto rejected = [&](std::vector<uint8_t> file, int expected) {
        assert(load(good) == 0);
        const int error = load(file);
        if (error != expected) std::fprintf(stderr,"expected %d got %d: %s\n",expected,error,linker.diagnostic().c_str());
        assert(error == expected && linker.imports().empty() && image.segments().empty());
        assert(!image.translate(entry,1,5) && !linker.diagnostic().empty());
    };
    auto bad = good; put(bad,999,data_file+0xc08); rejected(bad,-ENOTSUP);
    bad = good; put(bad,(99ULL<<32)|7,data_file+0xc08); rejected(bad,-ENOEXEC);
    bad = good; put(bad,entry,data_file+0xc00); rejected(bad,-ENOEXEC);
    bad = good; put(bad,UINT64_MAX-3,data_file+0xc00); rejected(bad,-ENOEXEC);
    bad = good; put(bad,data+1,data_file+0xc00); rejected(bad,-ENOEXEC);
    bad = good; put(bad,1,data_file+0xc10); rejected(bad,-ENOTSUP);
    bad = good; put(bad,25,dynamic_file); rejected(bad,-ENOEXEC);
    bad = good; put(bad,0x61000000,176,4); rejected(bad,-ENOTSUP);
    bad = good; put(bad,7,176,4); rejected(bad,-ENOTSUP);
    bad = good; put(bad,UINT64_MAX,dynamic_file+2*16+8); rejected(bad,-ENOEXEC);
    bad = good; put(bad,65537,dynamic_file+3*16+8); rejected(bad,-ENOEXEC);
    bad = good; put(bad,999,data_file+0x808,4); rejected(bad,-ENOEXEC);
    bad = good; bad[entry] = 0x0f; bad[entry+1] = 0x05; rejected(bad,-ENOTSUP); // Direct syscall refused.
    bad = good; bad[entry] = 0x48; bad[entry+1] = 0x89; bad[entry+2] = 0xe7; rejected(bad,-ENOTSUP); // RSP disclosure.
    bad = good; bad[entry+good[96]-1] = 0xc3; // Explicit final RET instead of HLT.
    rejected(bad,-ENOTSUP);
    Program unknown; unknown.call("missingKernelNID"); unknown.bytes({0xf4});
    rejected(unknown.elf(),-ENOENT);
    assert(linker.diagnostic().find("missingKernelNID") != std::string::npos);
    assert(load(good,1) == -E2BIG && image.segments().empty());
    assert(load(good,9) == 0); // CALL + generated thunk + NOP + ADD + store + HLT.
    assert(load(good,8) == -E2BIG);
    assert(load(combined()) == 0 && linker.imports().size() == 5);
    // Mutate the final writable-store proof after a valid import: no mapping
    // or partial image can escape a rejected late instruction.
    bad = good; put(bad,entry-(entry+18),entry+14,4); rejected(bad,-EFAULT);
    // Deterministic mutation coverage complements targeted malformed cases.
    for (size_t i = 0; i < 232; ++i) {
        bad = good; bad[i] ^= 0xff;
        const int error = load(bad);
        if (error) assert(image.segments().empty() && linker.imports().empty());
    }
    assert(load(good) == 0);
    image.clear(); linker.reset();
    assert(image.segments().empty() && linker.imports().empty());
    std::puts("PASS PS4 dynamic tables, NID imports, logical trampolines, malformed input and bounded CPU admission; FEX not executed");
}
