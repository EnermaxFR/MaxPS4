#include "../Native/OrbisPRXFEXBridge.hpp"
#include "OrbisFixture.h"
#include "OrbisLinkerFixture.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

// Fail one allocation, then restore the allocator so diagnostics can be built.
// Sweeping reaches failures after reservation and after the first region maps.
static long allocationCountdown = -1;
void* operator new(std::size_t size) {
    if (allocationCountdown == 0) { allocationCountdown = -1; throw std::bad_alloc(); }
    if (allocationCountdown > 0) --allocationCountdown;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    using namespace maxps4;
    using namespace orbis_test;
    // Separate RX and RW on both 4 KiB and 16 KiB hosts. Fabricated NIDs only.
    auto fixtureForPages = [](bool consumer) {
        auto f = fixture(consumer);
        put(f,0x4000,64+16); put(f,0x8000,120+16);
        if (!consumer) put(f,0x4000,0xb20);
        put(f,0x8000,0xc00); put(f,0x8008,0xc18); put(f,0x4000,0xc28);
        return f;
    };
    std::vector<OrbisPRXPreparation::Input> inputs{
        {"a.prx",fixtureForPages(false),0x10000}, {"b.prx",fixtureForPages(true),0x20000}};
    OrbisPRXPreparation prepared;
    assert(prepared.prepare(inputs));
    OrbisPRXFEXBridge bridge;
    assert(bridge.map(prepared));
    const auto* memory = bridge.memory();
    assert(memory && memory->regions().size()==4);
    uint64_t value = 0;
    assert(memory->read(0x28000,&value,8)==0 && value==0x14000); // qualified NID + GLOB_DAT
    assert(memory->read(0x28008,&value,8)==0 && value==0x24000); // RELATIVE
    uint8_t byte = 0;
    assert(memory->read(0x14000,&byte,1,true)==0 && byte==0xc3);
    assert(memory->read(0x282ff,&byte,1)==0 && byte==0); // BSS
    assert(!memory->translate(0x14000,1,2));
    assert(!memory->translate(0x28000,1,1)); // RW is NX
    assert(!memory->translate(0x13000,1,4)); // inaccessible gap/guard
    const uint8_t* host = reinterpret_cast<const uint8_t*>(1);
    assert(bridge.admit_dispatch(0x14000,host)==-ENOTSUP && !host);
    assert(bridge.diagnostic().find("dispatch refused")!=std::string::npos);
    // Refusal must be stable for executable, data and unmapped guest addresses.
    // No failed dispatch may publish a host pointer or mutate staged PRX bytes.
    for (uint64_t entry : {uint64_t{0x14000}, uint64_t{0x28000},
                           uint64_t{0x13000}, uint64_t{0x400000}}) {
        host = reinterpret_cast<const uint8_t*>(1);
        assert(bridge.admit_dispatch(entry,host)==-ENOTSUP);
        assert(host==nullptr);
        uint64_t after=0;
        assert(memory->read(0x28000,&after,8)==0 && after==0x14000);
        assert(bridge.memory()==memory);
    }
    auto rejected = [&](OrbisPRXPreparation bad) {
        assert(!bridge.map(bad) && !bridge.diagnostic().empty());
        assert(bridge.memory()==memory && memory->regions().size()==4);
        uint64_t retained=0; assert(memory->read(0x28000,&retained,8)==0 && retained==0x14000);
    };
    auto bad=prepared; bad.modules[1].regions[0].address=0x14000; rejected(bad);
    bad=prepared; bad.modules[0].regions[1].address=0x14100; rejected(bad); // page alias without byte overlap
    bad=prepared; bad.modules[0].regions[0].flags=7; rejected(bad);
    bad=prepared; bad.modules[0].regions[0].flags=1; rejected(bad); // execute-only unsupported
    bad=prepared; bad.modules[1].regions[0].address=UINT64_MAX; rejected(bad);
    bad=prepared; bad.modules[1].regions[0].address=(uint64_t{1}<<47)-1; rejected(bad);
    bad=prepared; bad.modules[1].regions[0].address=0x20000000; rejected(bad); // sparse span budget (256 MiB)
    bad=prepared; bad.modules[0].regions[0].bytes.clear(); rejected(bad);
    rejected(OrbisPRXPreparation{});
    bad=prepared; bad.modules[0].metadata.segments[0].type=7; rejected(bad);
    bad=prepared; bad.modules[0].metadata.segments[0].type=0x6474e552; rejected(bad);
    size_t allocationFailures=0;
    for (long n=0; n<64; ++n) {
        allocationCountdown=n;
        const bool ok=bridge.map(prepared);
        allocationCountdown=-1;
        if (ok) break;
        ++allocationFailures;
        assert(bridge.memory()==memory && memory->regions().size()==4);
        assert(memory->read(0x28000,&value,8)==0 && value==0x14000);
    }
    assert(allocationFailures>=8 && allocationFailures<64);
    // Seal standalone SCE_RELRO after preparation, preserving relocated data.
    auto relro=inputs; put(relro[1].bytes,0x61000010,120,4);
    assert(prepared.prepare(relro) && bridge.map(prepared));
    assert(bridge.memory()->read(0x28000,&value,8)==0 && value==0x14000);
    assert(!bridge.memory()->translate(0x28000,1,2) && !bridge.memory()->translate(0x28000,1,1));
    prepared.unload(); // mapped storage owns its bytes
    assert(bridge.memory()->read(0x28000,&value,8)==0 && value==0x14000);
    bridge.unload(); bridge.unload(); assert(!bridge.memory());
    assert(bridge.admit_dispatch(0x14000,host)==-ENOENT && !host);
    // Existing OrbisLinker image uses the same backend, without reparsing PRX.
    auto bytes=orbis_fixture::makeStatic();
    OrbisLinker::Image image; std::string diagnostic;
    OrbisLinker::Input input{{{"eboot.bin",{}},bytes.data(),bytes.size()},0x40000};
    assert(OrbisLinker::link({input},{},image,diagnostic));
    // Original fixture shares a 16 KiB page. Explicitly refuse that host case.
    GuestAddressSpace probe;
    if (probe.page_size()==4096) assert(bridge.map(image));
    else assert(!bridge.map(image));
    bridge.unload();
    // Public linker images must retain the same owned-byte/range contract as
    // prepared PRXs, even if a caller mutates an image after successful link.
    for (size_t i=0; i<image.modules[0].image.segments.size(); ++i)
        image.modules[0].image.segments[i].header.address=0x10000+i*0x4000;
    assert(bridge.map(image));
    const auto* committed=bridge.memory();
    auto rejectImage = [&](const OrbisLinker::Image& invalid) {
        assert(!bridge.map(invalid));
        assert(bridge.memory()==committed && !bridge.diagnostic().empty());
        host=reinterpret_cast<const uint8_t*>(1);
        assert(bridge.admit_dispatch(0x50000,host)==-ENOTSUP && !host);
    };
    auto invalid=image;
    // This wraps into a plausible low guest VA; final range checks alone
    // cannot detect that the linker's bias+segment addition overflowed.
    invalid.modules[0].bias=UINT64_MAX-0x7fff;
    rejectImage(invalid);
    invalid=image; invalid.modules[0].image.segments[0].bytes.clear(); rejectImage(invalid);
    invalid=image; invalid.modules[0].image.segments[0].bytes.resize(1); rejectImage(invalid);
    invalid=image; invalid.modules[0].image.segments[0].header.memorySize=UINT64_MAX; rejectImage(invalid);
    invalid=image; invalid.modules[0].image.requiresTLS=true; rejectImage(invalid);
    bridge.unload();
    host=reinterpret_cast<const uint8_t*>(1);
    assert(bridge.admit_dispatch(0x50000,host)==-ENOENT && !host);
    assert(bridge.map(image)); // reload after failed transactions and unload
    bridge.unload();
    std::puts("PASS PRX/linker guest mapping, NID/RELA bytes, RX/RW/NX, RELRO, collisions, overflow, transaction retention, unload, dispatch refusal; no guest execution");
}
