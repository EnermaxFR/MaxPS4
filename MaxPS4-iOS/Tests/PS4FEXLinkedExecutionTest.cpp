#include "../Native/maxps4_fex_runtime.h"
#include "PS4LinkedELFFixture.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static uint64_t value(const uint8_t* bytes) {
    uint64_t result = 0;
    for (size_t i = 0; i < 8; ++i) result |= uint64_t(bytes[i]) << (8*i);
    return result;
}
int main(int argc, char** argv) {
    using namespace ps4_linked_test;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    // Export the identical tested bytes for the application's import picker.
    if (argc == 2 || argc == 3) {
        for (int i = 1; i < argc; ++i) {
            const auto file = i == 1 ? minimal() : combined();
            FILE* out = std::fopen(argv[i],"wb");
            CHECK(out && std::fwrite(file.data(),1,file.size(),out) == file.size());
            CHECK(std::fclose(out) == 0);
        }
        return 0;
    }
    maxps4_fex_engine* engine = nullptr;
    std::puts("BEGIN real FEX context and linked Orbis execution");
    CHECK(maxps4_fex_engine_open(&engine) == 0 && engine);
    struct Close { maxps4_fex_engine* engine; ~Close() { maxps4_fex_engine_close(engine); } } close{engine};
    uint64_t entry_out = 99, result = 99;
    uint32_t imports = 99, backend = 99, calls = 99;
    auto file = minimal();
    CHECK(maxps4_fex_engine_link_ps4(engine,file.data(),file.size(),9,&entry_out,&imports) == 0);
    CHECK(entry_out == entry && imports == 1);
    CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == -EAGAIN);
    CHECK(!result && !backend && !entry_out && !calls);
    std::puts("PASS linking and pre-initialization refusal; initialize real FEX dispatcher");
    CHECK(maxps4_fex_engine_initialize(engine) == 0);
    std::puts("PASS dispatcher initialization; begin CPU execution");
    for (size_t repeat = 0; repeat < 3; ++repeat) {
        CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == 0);
        CHECK(result == 2 && calls == 1 && entry_out == entry);
#ifdef VIXL_SIMULATOR
        CHECK(backend == MAXPS4_CPU_FEX_ARM64_SIMULATOR);
#else
        CHECK(backend == MAXPS4_CPU_FEX_ARM64_JIT);
#endif
        uint8_t output[8]{};
        CHECK(maxps4_fex_engine_read_ps4(engine,data+0x100,output,8) == 0 && value(output) == 2);
    }
    std::printf("PASS linked Orbis ELF -> NID -> FEX -> libkernel -> resumed ADD/store/HLT (backend=%u)\n",backend);
    Program preserved;
    const uint8_t saved[] = {3,5,12,13,14,15}; // RBX, RBP, R12-R15.
    for (auto reg : saved) preserved.reg(reg,0x12340000+reg);
    preserved.call("scePthreadSelf");
    for (size_t i = 0; i < sizeof(saved); ++i) preserved.store(data+0x100+i*8,saved[i]);
    preserved.bytes({0xf4}); file = preserved.elf();
    CHECK(maxps4_fex_engine_link_ps4(engine,file.data(),file.size(),1000,&entry_out,&imports) == 0);
    CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == 0 && result == 1 && calls == 1);
    uint8_t preserved_bytes[48]{};
    CHECK(maxps4_fex_engine_read_ps4(engine,data+0x100,preserved_bytes,sizeof(preserved_bytes)) == 0);
    for (size_t i = 0; i < sizeof(saved); ++i) CHECK(value(preserved_bytes+i*8) == 0x12340000+saved[i]);
    std::puts("PASS System V callee-saved RBX/RBP/R12-R15 preserved through actual FEX syscall trampoline");
    file = combined();
    CHECK(maxps4_fex_engine_link_ps4(engine,file.data(),file.size(),1000,&entry_out,&imports) == 0 && imports == 5);
    char directory[] = "/tmp/maxps4-linked-XXXXXX";
    CHECK(::mkdtemp(directory));
    char host_path[512]; std::snprintf(host_path,sizeof(host_path),"%s/fixture",directory);
    const int host = ::open(host_path,O_CREAT|O_EXCL|O_WRONLY,0600);
    CHECK(host >= 0 && ::write(host,"PS4!",4) == 4 && ::close(host) == 0);
    struct Remove {
        const char* path; const char* directory;
        ~Remove() { ::unlink(path); ::rmdir(directory); }
    } remove{host_path,directory};
    CHECK(maxps4_fex_engine_mount_app0(engine,directory) == 0);
    for (size_t repeat = 0; repeat < 2; ++repeat) {
        CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == 0);
        CHECK(result == 0 && calls == 5);
        uint8_t output[48]{};
        CHECK(maxps4_fex_engine_read_ps4(engine,data+0x100,output,sizeof(output)) == 0);
        CHECK(value(output) == 3 && value(output+8) == 4 && value(output+16) == 0 && value(output+24) == 0 && value(output+40) == 0);
        const uint64_t allocation = value(output+32);
        CHECK(allocation >= 0x1000000000ULL);
        uint8_t bytes[16]{};
        CHECK(maxps4_fex_engine_read_ps4(engine,data+0x2000,bytes,4) == 0 && !std::memcmp(bytes,"PS4!",4));
        CHECK(maxps4_fex_engine_read_ps4(engine,data+0x2100,bytes,16) == 0);
        CHECK(value(bytes) > 0 && value(bytes+8) < 1000000000);
        const uint8_t pattern[] = {1,2,3,4};
        CHECK(maxps4_fex_engine_write_ps4(engine,allocation,pattern,sizeof(pattern)) == 0);
        CHECK(maxps4_fex_engine_read_ps4(engine,allocation,bytes,4) == 0 && !std::memcmp(bytes,pattern,4));
    }
    std::puts("PASS real FEX calls open/read/close/clock/mmap, register return, fourth and seventh ABI arguments");
    // A pointer with the numerical value of an application-host address must
    // still be treated as a logical guest pointer and return Orbis EFAULT.
    Program invalid_pointer;
    invalid_pointer.reg(7,reinterpret_cast<uintptr_t>(host_path));
    invalid_pointer.reg(6,0); invalid_pointer.reg(2,0);
    invalid_pointer.call("sceKernelOpen"); invalid_pointer.bytes({0xf4});
    file = invalid_pointer.elf();
    CHECK(maxps4_fex_engine_link_ps4(engine,file.data(),file.size(),100,&entry_out,&imports) == 0);
    CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == 0);
    CHECK(result == static_cast<uint64_t>(-2147352562LL) && calls == 1);
    CHECK(maxps4_fex_engine_link_ps4(engine,nullptr,0,100,&entry_out,&imports) != 0);
    CHECK(!entry_out && !imports);
    CHECK(maxps4_fex_engine_run_linked_ps4(engine,&result,&backend,&entry_out,&calls) == -EFAULT);
    CHECK(!result && !backend && !entry_out && !calls);
    std::puts("PASS host-pointer isolation, failed restage and cleanup; iPhone execution requires separate device validation");
}
