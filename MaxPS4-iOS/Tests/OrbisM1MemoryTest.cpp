#include "OrbisM1Fixture.h"
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <signal.h>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
using namespace maxps4;
static void guard_fault(const uint8_t* p) {
    const pid_t child = fork(); CHECK(child >= 0);
    if (!child) { signal(SIGSEGV,SIG_DFL); signal(SIGBUS,SIG_DFL); volatile uint8_t v = *p; (void)v; _exit(1); }
    int status = 0; CHECK(waitpid(child,&status,0) == child);
    CHECK(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
}
int main() {
    GuestAddressSpace space;
    const size_t page = space.page_size(); CHECK(page == 4096 || page == 16384);
    const uint64_t base = 0x1000000000ULL;
    CHECK(space.reserve(base,4*page) == 0);
    uint64_t value = 0x12345678;
    CHECK(space.map(base,page,6,&value,8) == 0);
    CHECK(space.map(base+2*page,page,5,&value,8) == 0);
    const auto* host = space.translate(base,8,4); CHECK(host && reinterpret_cast<uintptr_t>(host) != base);
    CHECK(!space.translate(base+page,1,4) && !space.translate(base+2*page,1,2));
    CHECK(space.map(base+1,8,6) == -EEXIST);
    CHECK(space.reserve(base+4*page,page) == -EEXIST);
    CHECK(space.reserve(UINT64_MAX-page+1,page) == -EINVAL);
    CHECK(space.map(base,8,7) == -EINVAL);
    CHECK(space.protect(base,page,4) == 0 && !space.translate(base,8,2));
    CHECK(space.protect(base,page,6) == 0);
    guard_fault(host-page); guard_fault(host+page); guard_fault(host+4*page);
    CHECK(space.release(base) == 0 && space.allocated() == 0 && !space.translate(base,1,4));
    // A FEX collision cannot overwrite host memory; never use MAP_FIXED.
    GuestAddressSpace direct(GuestAddressSpace::Backend::fex_direct);
    const uint64_t direct_base = 0x300000000000ULL;
    CHECK(direct.reserve(direct_base,page) == 0);
    CHECK(direct.map(direct_base,page,6,&value,8) == 0);
    GuestAddressSpace collision(GuestAddressSpace::Backend::fex_direct);
    CHECK(collision.reserve(direct_base,page) == -EADDRINUSE);
    CHECK(*reinterpret_cast<const uint64_t*>(direct.translate(direct_base,8,4)) == value);
    CHECK(direct.release(direct_base) == 0);

    auto file = orbis_m1_fixture::make();
    {
        OrbisM1 direct_process(GuestAddressSpace::Backend::fex_direct);
        CHECK(direct_process.load(file.data(),file.size(),100) == 0);
        GuestThreadMemory direct_thread(direct_process.space);
        CHECK(direct_thread.initialize(direct_process.tls,OrbisM1::thread_base,direct_process.entry) == 0);
        CHECK(direct_process.admit(direct_thread,100) == 0);
        CHECK(reinterpret_cast<uintptr_t>(direct_process.space.translate(direct_process.entry,1,5)) == direct_process.entry);
        CHECK(reinterpret_cast<uintptr_t>(direct_process.space.translate(direct_thread.tls_begin,16,6)) == direct_thread.tls_begin);
        CHECK(direct_process.space.translate(OrbisM1::heap_base,0x10000,6));
        direct_thread.clear(); direct_process.space.clear(); CHECK(direct_process.space.allocated() == 0);
    }
    OrbisM1 process(GuestAddressSpace::Backend::translated);
    CHECK(process.load(file.data(),file.size(),100) == 0);
    const size_t baseline = process.space.allocated();
    {
        GuestThreadMemory one(process.space), two(process.space);
        CHECK(one.initialize(process.tls,OrbisM1::thread_base,process.entry) == 0);
        CHECK(two.initialize(process.tls,OrbisM1::thread_base+0x100000,process.entry) == 0);
        CHECK(one.fs_base != two.fs_base && !(one.rsp & 15) && !(one.fs_base & 15));
        CHECK(process.admit(one,100) == 0 && process.admit(two,100) == 0);
        const auto* a = process.space.translate(one.tls_begin,16,6);
        const auto* b = process.space.translate(two.tls_begin,16,6);
        uint64_t words[2]; std::memcpy(words,a,16); CHECK(words[0] == 17 && words[1] == 0);
        uint64_t changed = 91; std::memcpy(const_cast<uint8_t*>(a),&changed,8);
        std::memcpy(words,b,16); CHECK(words[0] == 17 && words[1] == 0);
        uint64_t stack[9]; std::memcpy(stack,process.space.translate(one.rsp,sizeof(stack),4),sizeof(stack));
        CHECK(stack[0] == 0 && stack[4] == page && stack[6] == process.entry && stack[8] == 0);
        CHECK(process.admit(one,1) == -E2BIG);
    }
    CHECK(process.space.allocated() == baseline);
    using maxps4_ps4_synthetic::put;
    auto bad = file;
    put(bad,5,56,2); std::memcpy(bad.data()+288,bad.data()+232,56);
    CHECK(process.load(bad.data(),bad.size(),100) == -ENOEXEC && process.space.allocated() == 0);
    bad = file; put(bad,0xa008,232+8);
    CHECK(process.load(bad.data(),bad.size(),100) == -ENOEXEC);
    bad = file; put(bad,3,232+48); CHECK(process.load(bad.data(),bad.size(),100) == -ENOEXEC);
    bad = file; put(bad,4,56,2); put(bad,32*1024*1024,232+40); CHECK(process.load(bad.data(),bad.size(),100) == -ENOEXEC);
    bad = file; put(bad,orbis_m1_fixture::data+0x2008,232+16); CHECK(process.load(bad.data(),bad.size(),100) == -ENOTSUP);
    bad = file; bad[0x4000] = 0xeb;
    CHECK(process.load(bad.data(),bad.size(),100) == 0);
    GuestThreadMemory thread(process.space); CHECK(thread.initialize(process.tls,OrbisM1::thread_base,process.entry) == 0);
    CHECK(process.admit(thread,100) == -ENOTSUP);
    thread.clear();
    for (int fault=0;fault<5;++fault) {
        bad = file;
        if (fault == 0) put(bad,0xffffffd0,0x4005,4); // FS outside TLS.
        if (fault == 1) put(bad,orbis_m1_fixture::data,0x401b); // Write GOT capability.
        if (fault == 2) put(bad,orbis_m1_fixture::entry,0x401b); // Write RX.
        if (fault == 3) put(bad,UINT64_MAX-3,0x401b); // Absolute overflow.
        if (fault == 4) bad[0x4000] = 0x58; // POP without PUSH.
        CHECK(process.load(bad.data(),bad.size(),100) == 0);
        CHECK(thread.initialize(process.tls,OrbisM1::thread_base,process.entry) == 0);
        CHECK(process.admit(thread,100) == (fault == 4 ? -ENOTSUP : -EFAULT));
        thread.clear();
    }
    process.space.clear();
    CHECK(process.space.reserve(base,2*page) == 0);
    CHECK(process.space.reserve(base+0x100000000ULL,2*page) == 0); // Sparse VA, bounded host storage.
    CHECK(process.space.allocated() == 4*page);
    CHECK(process.space.map(base,page,1) == -EINVAL); // X requires R, host pages never executable.
    process.space.clear();
    std::puts("PASS guest/host separation, mappings, guards, collisions, rollback, static TLS isolation, stack and M1 admission");
}
