#include "../Native/OrbisPRXFEXBridge.hpp"
#include "OrbisFixture.h"
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>

int main() {
    using namespace maxps4;
    auto bytes=orbis_test::fixture(false);
    orbis_test::put(bytes,0x4000,64+16); orbis_test::put(bytes,0x8000,120+16);
    orbis_test::put(bytes,0x4000,0xb20);
    orbis_test::put(bytes,0x8000,0xc00); orbis_test::put(bytes,0x8008,0xc18);
    orbis_test::put(bytes,0x4000,0xc28);
    OrbisPRXPreparation prepared;
    assert(prepared.prepare({{"synthetic.prx",bytes,0x10000}}));
    OrbisPRXFEXBridge bridge;
    assert(bridge.map(prepared));
    size_t invalidations=0;
    const uint8_t* live=nullptr;
    auto owner=bridge.make_translation([&] {
        if (live) assert(*live==0xc3); // Invalidate while RX storage is live.
        ++invalidations;
    });
    auto other=bridge.make_translation([] {});
    // A failed copy after partial preparation never alters staging or publishes
    // an owner. A one-shot failure allows destructor invalidation to complete.
    size_t attempts=0;
    bool partialFailed=false;
    try {
        auto failed=bridge.make_translation([&] {
            if (++attempts==3) throw std::runtime_error("injected invalidation failure");
        });
    } catch (const std::runtime_error&) { partialFailed=true; }
    assert(partialFailed && attempts==4);
    uint64_t stagingRelocation=0;
    assert(bridge.memory()->read(0x18000,&stagingRelocation,8)==0 && stagingRelocation==0x14000);
    const uint8_t* denied=reinterpret_cast<const uint8_t*>(1);
    assert(bridge.admit_dispatch(0x14000,denied)==-ENOTSUP && !denied);
    auto writeOnly=prepared;
    writeOnly.modules[0].regions[1].flags=2;
    OrbisPRXFEXBridge unsupported;
    assert(unsupported.map(writeOnly));
    bool unreadableFailed=false;
    try { auto failed=unsupported.make_translation([] {}); }
    catch (const std::runtime_error&) { unreadableFailed=true; }
    assert(unreadableFailed); // Never memcpy from a write-only source mapping.
    denied=reinterpret_cast<const uint8_t*>(1);
    assert(unsupported.admit_dispatch(0x14000,denied)==-ENOTSUP && !denied);
    assert(owner->reserve(0x14000,owner->page_size())==-EEXIST);
    uint64_t unchanged=0xa5;
    assert(owner->read(0x14100,&unchanged,sizeof(unchanged))==-EFAULT && unchanged==0xa5);
    {
        auto a=owner->lease(); auto b=other->lease();
        const uint8_t *left=nullptr,*right=nullptr;
        assert(a.resolve_contiguous(0x14000,1,5,left)==0);
        assert(b.resolve_contiguous(0x14000,1,5,right)==0);
        assert(left!=right && reinterpret_cast<uintptr_t>(left)!=0x14000);
        uint64_t relocation=0;
        assert(a.memory().read(0x18000,&relocation,8)==0 && relocation==0x14000);
        assert(!a.memory().translate(0x14000,1,2));
        assert(!a.memory().translate(0x18000,1,1));
    }
    bridge.unload(); prepared.unload(); // Snapshots own bytes, not loader storage.
    uint8_t opcode=0;
    assert(owner->read(0x14000,&opcode,1,true)==0 && opcode==0xc3);
    assert(owner->protect(0x14000,0x100,7)==-EINVAL); // W+X cannot be introduced.
    {
        auto lease=owner->lease();
        assert(lease.memory().translate(0x14000,1,5));
        assert(!lease.memory().translate(0x14000,1,2));
    }
    std::future<int> release;
    std::promise<void> started;
    {
        auto lease=owner->lease();
        assert(lease.resolve_contiguous(0x14000,1,5,live)==0);
        release=std::async(std::launch::async,[&] {
            started.set_value(); return owner->release(0x10000+0x4000);
        });
        started.get_future().wait();
        assert(release.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
        assert(*live==0xc3);
    }
    // reserve() used the first RX page as the reservation base.
    assert(release.get()==0);
    live=nullptr;
    assert(owner->read(0x14000,&opcode,1,true)==-EFAULT);
    owner->close(); owner->close();
    assert(owner->reserve(0x14000,owner->page_size())==-ESHUTDOWN);
    assert(invalidations>=4);
    bool refused=false;
    try { auto missing=bridge.make_translation([] {}); }
    catch (const std::logic_error&) { refused=true; }
    assert(refused);
    // The separate snapshot API never changes direct dispatch admission.
    assert(bridge.map(OrbisPRXPreparation{})==false);
    const uint8_t* output=reinterpret_cast<const uint8_t*>(1);
    assert(bridge.admit_dispatch(0x14000,output)==-ENOENT && !output);
    std::cout << "PASS PRX translated snapshot: relocated guest pointers, independent host storage, loader unload, W^X/NX, lease blocks release, invalidation before unmap and terminal close; no CPU execution\n";
}
