#include "../Native/maxps4_orbis_v4_translation.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
int main() {
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
    CHECK(memory.protect(base+page,page,4)==0);
    const uint8_t replacement[4]={1,1,1,1};
    CHECK(memory.write_span(base+page-2,replacement,sizeof(replacement))==-EFAULT);
    CHECK(memory.read(base+page-2,result_bytes,sizeof(result_bytes))==0);
    for (size_t i=0;i<sizeof(across);++i) CHECK(result_bytes[i]==across[i]);
    memory.clear();
    { auto lease=memory.lease(); CHECK(lease.ranges().empty()); }
    std::puts("PASS translation leases, invalidation-before-mutation, failed invalidation rollback, protection, release and reuse");
}

