#include "../Native/maxps4_orbis_v4_translation.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
static void destruction_invalidates_host_addresses() {
    // Model two thread caches embedding translated host constants. Their
    // invalidator must still be able to inspect owned storage when called.
    std::vector<const uint8_t*> cached_hosts;
    unsigned invalidations=0;
    auto owner=std::make_unique<maxps4::OrbisV4Translation>([&] {
        for (const auto* host : cached_hosts) CHECK(*host==0x59);
        cached_hosts.clear();
        ++invalidations;
    });
    const size_t page=owner->page_size();
    constexpr uint64_t guest=0x400000;
    const uint8_t marker=0x59;
    CHECK(owner->reserve(guest,page)==0);
    CHECK(owner->map(guest,page,5,&marker,1)==0);
    {
        auto lease=owner->lease();
        const uint8_t* host=nullptr;
        CHECK(lease.resolve_contiguous(guest,1,5,host)==0);
        CHECK(reinterpret_cast<uintptr_t>(host)!=guest && *host==marker);
        cached_hosts={host,host};
    }
    const auto before=invalidations;
    owner.reset(); // No explicit clear/release: this was the missing path.
    CHECK(invalidations==before+1);
    CHECK(cached_hosts.empty());

    // Explicit cleanup may be followed by destruction of the FEX context.
    // An empty owner must not call back into that destroyed context later.
    bool context_alive=true;
    unsigned empty_invalidations=0;
    auto cleared=std::make_unique<maxps4::OrbisV4Translation>([&] {
        CHECK(context_alive); ++empty_invalidations;
    });
    CHECK(cleared->reserve(guest,page)==0);
    cleared->clear();
    const auto after_clear=empty_invalidations;
    context_alive=false;
    cleared.reset();
    CHECK(empty_invalidations==after_clear);
    std::puts("PASS destruction invalidates cached host addresses before unmap; empty destruction skips callback");
}
int main() {
    destruction_invalidates_host_addresses();
    constexpr uint64_t base=0x300000000ULL;
    std::atomic<unsigned> invalidations{0};
    bool reject=false;
    const uint8_t* pinned=nullptr;
    maxps4::OrbisV4Translation memory([&] {
        if(reject) throw std::runtime_error("Invalidation failure");
        if(pinned) CHECK(*pinned==42); // callback runs before unmap/protect
        ++invalidations;
    });
    const size_t page=memory.page_size();
    CHECK(memory.reserve(base,page)==0); CHECK(memory.map(base,page,6)==0);
    uint8_t value=42; CHECK(memory.write(base,&value,1)==0);
    {
        auto lease=memory.lease(); CHECK(lease.ranges().size()==1);
        CHECK(lease.ranges()[0].guest==base && lease.ranges()[0].host!=base);
        pinned=lease.memory().translate(base,1,6); CHECK(pinned && *pinned==42);
    }
    reject=true;
    bool threw=false;
    try { memory.release(base); } catch(const std::runtime_error&) { threw=true; }
    CHECK(threw);
    { auto lease=memory.lease(); CHECK(lease.memory().translate(base,1,6)==pinned); }
    reject=false;
    std::future<int> result;
    std::promise<void> started;
    const auto before=invalidations.load();
    {
        auto lease=memory.lease();
        result=std::async(std::launch::async,[&] { started.set_value(); return memory.release(base); });
        started.get_future().wait();
        CHECK(result.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout);
        CHECK(invalidations.load()==before); CHECK(*pinned==42);
    }
    CHECK(result.get()==0); pinned=nullptr;
    CHECK(invalidations.load()==before+1);
    { auto lease=memory.lease(); CHECK(lease.ranges().empty()); CHECK(!lease.memory().translate(base,1,6)); }
    CHECK(memory.reserve(base,page)==0); CHECK(memory.map(base,page,6)==0);
    CHECK(memory.write(base,&value,1)==0);
    CHECK(memory.protect(base,page,4)==0);
    { auto lease=memory.lease(); CHECK(!lease.memory().translate(base,1,2)); CHECK(lease.memory().translate(base,1,4)); }
    // A revoked mapping must remain leasable, and fault without leaking an
    // old host pointer. Restoring permissions must make the same VA usable.
    CHECK(memory.protect(base,page,0)==0);
    {
        auto lease=memory.lease();
        CHECK(lease.ranges().empty());
        const uint8_t* host=reinterpret_cast<const uint8_t*>(1);
        CHECK(lease.resolve_contiguous(base,1,4,host)==-EFAULT && !host);
        uint8_t unchanged=0x59;
        CHECK(lease.memory().read(base,&unchanged,1)==-EFAULT && unchanged==0x59);
    }
    CHECK(memory.protect(base,page,6)==0);
    {
        auto lease=memory.lease();
        const uint8_t* host=nullptr;
        CHECK(lease.resolve_contiguous(base,1,4,host)==0 && host && *host==42);
    }
    // Checked split-region accesses remain atomic with respect to mapping
    // changes, and a failed write must leave the first region unchanged.
    CHECK(memory.release(base)==0);
    CHECK(memory.reserve(base,2*page)==0);
    CHECK(memory.map(base,page,6)==0);
    CHECK(memory.map(base+page,page,6)==0);
    const uint8_t across[4]={7,8,9,10};
    uint8_t result_bytes[4]={};
    CHECK(memory.write_span(base+page-2,across,sizeof(across))==0);
    CHECK(memory.read(base+page-2,result_bytes,sizeof(result_bytes))==0);
    for (size_t i=0;i<sizeof(across);++i) CHECK(result_bytes[i]==across[i]);
    CHECK(memory.protect(base+page,page,0)==0);
    uint8_t blocked_read[4]={0xa5,0xa5,0xa5,0xa5};
    CHECK(memory.read(base+page-2,blocked_read,sizeof(blocked_read))==-EFAULT);
    for (const auto byte : blocked_read) CHECK(byte==0xa5);
    CHECK(memory.protect(base+page,page,6)==0);
    CHECK(memory.protect(base+page,page,4)==0);
    const uint8_t replacement[4]={1,1,1,1};
    CHECK(memory.write_span(base+page-2,replacement,sizeof(replacement))==-EFAULT);
    CHECK(memory.read(base+page-2,result_bytes,sizeof(result_bytes))==0);
    for (size_t i=0;i<sizeof(across);++i) CHECK(result_bytes[i]==across[i]);
    memory.clear();
    { auto lease=memory.lease(); CHECK(lease.ranges().empty()); }
    std::puts("PASS translation leases, invalidation-before-mutation, failed invalidation rollback, protection, release and reuse");
}

