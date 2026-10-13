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
    Program missing_fourth;
    missing_fourth.call("sceKernelMmap"); missing_fourth.bytes({0xf4});
    rejected(missing_fourth.elf(),-ENOTSUP);
    Program private_rcx;
    private_rcx.reg(1,123); private_rcx.call("scePthreadSelf");
    private_rcx.store(data+0x100,1); private_rcx.bytes({0xf4});
    rejected(private_rcx.elf(),-ENOTSUP); // SYSCALL's host-domain return RIP cannot escape.
    Program uninitialized_saved;
    uninitialized_saved.call("scePthreadSelf"); uninitialized_saved.store(data+0x100,3);
    uninitialized_saved.bytes({0xf4}); rejected(uninitialized_saved.elf(),-ENOTSUP);
    assert(load(good,1) == -E2BIG && image.segments().empty());
    assert(load(good,10) == 0); // CALL + thunk + NOP + return store + ADD + final store + HLT.
    assert(load(good,9) == -E2BIG);
    assert(load(combined()) == 0 && linker.imports().size() == 5);
    auto defined = [&](uint32_t type, uint64_t symbol, uint64_t addend = 0) {
        auto file = good;
        put(file,3,data_file+0x804,4); // Include the defined symbol in DT_HASH.
        const size_t sym = data_file+0x400+2*24;
        put(file,1,sym,4); file[sym+4] = 0x11; put(file,1,sym+6,2);
        put(file,symbol,sym+8); put(file,8,sym+16);
        put(file,data+0x80,data_file+0xc18);
        put(file,(2ULL<<32)|type,data_file+0xc20);
        put(file,addend,data_file+0xc28);
        put(file,48,dynamic_file+7*16+8);
        return file;
    };
    for (uint32_t type : {1U,2U,6U,7U,10U,11U}) {
        assert(load(defined(type,data+0x3000)) == 0);
        assert(memory.read(data+0x80,bytes,type == 2 || type == 10 || type == 11 ? 4 : 8));
        const size_t width = type == 2 || type == 10 || type == 11 ? 4 : 8;
        assert(maxps4::ps4::get_le(bytes,width) == (type == 2 ? 0x2f80 : data+0x3000));
    }
    bad = defined(8,data+0x3000,data+0x3000);
    put(bad,8,data_file+0xc20); // R_RELATIVE must have symbol index zero.
    assert(load(bad) == 0 && memory.read(data+0x80,bytes,8));
    assert(maxps4::ps4::get_le(bytes,8) == data+0x3000);
    assert(load(defined(1,data+0x3000,UINT64_MAX)) == 0);
    assert(memory.read(data+0x80,bytes,8) && maxps4::ps4::get_le(bytes,8) == data+0x2fff);
    // Restore a valid link after a successful negative addend check.
    assert(load(good) == 0);
    bad = defined(1,data+0x3000,UINT64_MAX-data-0x3000); rejected(bad,-EOVERFLOW);
    bad = defined(1,data+0x3000,0x100000); rejected(bad,-ENOEXEC);
    bad = defined(1,data+0x3000); bad[data_file+0x400+2*24+4] = 0x16; rejected(bad,-ENOTSUP);
    bad = defined(1,data+0x3000); put(bad,data,data_file+0xc18); rejected(bad,-ENOEXEC); // Duplicate target.
    auto distant = [&](uint32_t type, uint64_t symbol) {
        auto file = defined(type,symbol);
        put(file,4,56,2);
        put(file,1,232,4); put(file,4,236,4); put(file,0xa000,240);
        put(file,symbol,248); put(file,8,264); put(file,0x4000,272); put(file,1,280);
        return file;
    };
    rejected(distant(2,0x100000000ULL),-EOVERFLOW); // Signed PC32.
    rejected(distant(10,0x100000000ULL),-EOVERFLOW); // Unsigned R32.
    rejected(distant(11,0x80000000ULL),-EOVERFLOW); // Signed R32S.
    // Mutate the final writable-store proof after a valid import: no mapping
    // or partial image can escape a rejected late instruction.
    bad = good; put(bad,entry-(entry+25),entry+21,4); rejected(bad,-EFAULT);
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
