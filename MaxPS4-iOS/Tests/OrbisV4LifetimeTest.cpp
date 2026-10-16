#include "../Native/maxps4_orbis_v4_translation.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <future>
#include <memory>
#include <sys/wait.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
using Owner=maxps4::OrbisV4Translation;
constexpr uint64_t guest=0x400000;

static void blocked_mutations() {
    for (unsigned operation=0; operation<6; ++operation) {
        std::atomic<unsigned> calls{0};
        const uint8_t* cached=nullptr;
        Owner owner([&] { if (cached) CHECK(*cached==42); cached=nullptr; ++calls; });
        const auto page=owner.page_size();
        CHECK(owner.reserve(guest,page)==0 && owner.map(guest,page,6)==0);
        uint8_t value=42;
        CHECK(owner.write(guest,&value,1)==0);
        std::promise<void> started;
        std::future<int> result;
        const auto before=calls.load();
        {
            auto lease=owner.lease();
            CHECK(lease.resolve_contiguous(guest,1,4,cached)==0);
            result=std::async(std::launch::async,[&] {
                started.set_value();
                switch (operation) {
                case 0: return owner.release(guest);
                case 1: owner.clear(); return 0;
                case 2: return owner.protect(guest,page,0);
                case 3: value=99; return owner.write(guest,&value,1);
                case 4: value=99; return owner.write_span(guest,&value,1);
                default: owner.close(); return 0;
                }
            });
            started.get_future().wait();
            CHECK(result.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout);
            CHECK(calls.load()==before && *cached==42);
        }
        CHECK(result.get()==0 && calls.load()==before+1 && !cached);
        if (operation==5) {
            CHECK(owner.reserve(guest,page)==-ESHUTDOWN);
            continue;
        }
        auto lease=owner.lease();
        const uint8_t* host=reinterpret_cast<const uint8_t*>(1);
        if (operation<3) CHECK(lease.resolve_contiguous(guest,1,4,host)==-EFAULT && !host);
        else CHECK(lease.resolve_contiguous(guest,1,4,host)==0 && *host==99);
    }
    std::puts("PASS lease excludes release/clear/protect/write/write_span/close; invalidation before mutation");
}

static void reuse_and_double_release() {
#if defined(__linux__) && defined(MAP_FIXED_NOREPLACE)
    void* replacement=MAP_FAILED;
    size_t page=0;
    unsigned calls=0;
    {
        Owner owner([&] { ++calls; });
        page=owner.page_size();
        CHECK(owner.reserve(guest,page)==0 && owner.map(guest,page,6)==0);
        void* old=nullptr;
        { auto lease=owner.lease(); old=const_cast<uint8_t*>(lease.memory().translate(guest,1,4)); CHECK(old); }
        CHECK(owner.release(guest)==0);
        // Exact reuse without overwriting an occupant. A repeated release or
        // destructor unmap must not remove this independently owned mapping.
        replacement=::mmap(old,page,PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
        CHECK(replacement==old);
        *static_cast<uint8_t*>(replacement)=0x59;
        CHECK(owner.release(guest)==-ENOENT);
        CHECK(*static_cast<uint8_t*>(replacement)==0x59);
        owner.clear(); owner.clear();
        CHECK(owner.reserve(guest,page)==0 && owner.map(guest,page,6)==0);
        { auto lease=owner.lease(); const auto* fresh=lease.memory().translate(guest,1,4);
          CHECK(fresh && fresh!=replacement && *fresh==0); }
        // Leave this new reservation to the destructor.
    }
    CHECK(*static_cast<uint8_t*>(replacement)==0x59 && calls>0);
    CHECK(::munmap(replacement,page)==0);
    std::puts("PASS exact host VA reuse, repeated release/clear, guest VA remap and destructor cleanup");
#else
    std::puts("SKIP exact host VA reuse: requires Linux MAP_FIXED_NOREPLACE without overwrite");
#endif
}

static void throwing_clear_and_unwind() {
    unsigned calls=0;
    bool reject=false;
    const uint8_t* cached=nullptr;
    {
        Owner owner([&] {
            if (cached) CHECK(*cached==42);
            if (reject) throw std::runtime_error("invalidation refused");
            cached=nullptr; ++calls;
        });
        auto page=owner.page_size();
        CHECK(owner.reserve(guest,page)==0 && owner.map(guest,page,6)==0);
        uint8_t marker=42; CHECK(owner.write(guest,&marker,1)==0);
        { auto lease=owner.lease(); cached=lease.memory().translate(guest,1,4); CHECK(cached); }
        reject=true;
        bool threw=false;
        try { owner.clear(); } catch (const std::runtime_error&) { threw=true; }
        CHECK(threw && *cached==42);
        { auto lease=owner.lease(); CHECK(lease.memory().translate(guest,1,4)==cached); }
        reject=false; owner.clear();
        CHECK(!cached);
    }
    const auto before=calls;
    bool caught=false;
    try {
        Owner owner([&] { ++calls; });
        CHECK(owner.reserve(guest,owner.page_size())==0);
        throw std::runtime_error("outer unwind");
    } catch (const std::runtime_error&) { caught=true; }
    CHECK(caught && calls==before+2); // reserve + nonempty destructor
    // Allocated reservations must be invalidated even with no accessible range.
    const auto prior=calls;
    { Owner owner([&] { ++calls; }); auto page=owner.page_size();
      CHECK(owner.reserve(guest,page)==0 && owner.map(guest,page,6)==0);
      CHECK(owner.protect(guest,page,0)==0);
      { auto lease=owner.lease(); CHECK(lease.ranges().empty()); } }
    CHECK(calls==prior+4);
    std::puts("PASS failed clear retains storage for retry; exception unwind; reserved-only and PROT_NONE destruction");
}

static const uint8_t* terminating_host=nullptr;
static void shared_lifetime() {
    std::atomic<unsigned> calls{0};
    const uint8_t* cached=nullptr;
    auto owner=std::make_shared<Owner>([&] { if (cached) CHECK(*cached==42); cached=nullptr; ++calls; });
    const auto page=owner->page_size();
    CHECK(owner->reserve(guest,page)==0 && owner->map(guest,page,6)==0);
    const uint8_t marker=42; CHECK(owner->write(guest,&marker,1)==0);
    std::weak_ptr<Owner> weak=owner;
    std::promise<void> pinned, finish;
    auto finish_signal=finish.get_future();
    auto worker=std::async(std::launch::async,[keep=owner,&cached,&pinned,&finish_signal]() mutable {
        { auto lease=keep->lease();
          CHECK(lease.resolve_contiguous(guest,1,4,cached)==0);
          pinned.set_value(); finish_signal.wait(); CHECK(*cached==42); }
        keep.reset(); // The lease has ended before final-owner destruction.
    });
    pinned.get_future().wait();
    const auto before=calls.load();
    owner.reset();
    CHECK(!weak.expired() && calls.load()==before && *cached==42);
    finish.set_value(); worker.get();
    CHECK(weak.expired() && calls.load()==before+1 && !cached);
    std::puts("PASS worker retains owner through lease; final shared release invalidates and destroys once");
}

static void termination_before_unmap() {
    const auto child=::fork(); CHECK(child>=0);
    if (!child) {
        std::set_terminate([] { std::_Exit(terminating_host && *terminating_host==42 ? 79 : 80); });
        bool reject=false;
        auto owner=std::make_unique<Owner>([&] { if (reject) throw std::runtime_error("destructor invalidation refused"); });
        const auto page=owner->page_size();
        CHECK(owner->reserve(guest,page)==0 && owner->map(guest,page,6)==0);
        const uint8_t marker=42; CHECK(owner->write(guest,&marker,1)==0);
        { auto lease=owner->lease(); terminating_host=lease.memory().translate(guest,1,4); }
        reject=true; owner.reset(); std::_Exit(81);
    }
    int status=0; CHECK(::waitpid(child,&status,0)==child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status)==79);
    std::puts("PASS throwing destructor invalidator terminates before host storage is unmapped");
}

static void context_shutdown() {
    // Model the FEX callback's borrowed context/thread pointers. Fail before
    // dereferencing them if cleanup ever calls back after context destruction.
    struct Context { unsigned calls=0; const uint8_t* cached=nullptr; };
    bool context_alive=true;
    auto context=std::make_unique<Context>();
    auto* borrowed=context.get();
    auto owner=std::make_unique<Owner>([&] {
        CHECK(context_alive);
        if (borrowed->cached) CHECK(*borrowed->cached==42);
        borrowed->cached=nullptr; ++borrowed->calls;
    });
    const auto page=owner->page_size();
    CHECK(owner->reserve(guest,page)==0 && owner->map(guest,page,6)==0);
    const uint8_t marker=42; CHECK(owner->write(guest,&marker,1)==0);
    { auto lease=owner->lease(); borrowed->cached=lease.memory().translate(guest,1,4); }
    const auto before=borrowed->calls;
    owner->close();
    CHECK(borrowed->calls==before+1 && !borrowed->cached);
    context.reset(); context_alive=false;
    owner->close(); owner->clear();
    uint8_t unchanged=0xa5;
    CHECK(owner->read(guest,&unchanged,1)==-ESHUTDOWN && unchanged==0xa5);
    CHECK(owner->reserve(guest,page)==-ESHUTDOWN);
    CHECK(owner->map(guest,page,6)==-ESHUTDOWN);
    CHECK(owner->protect(guest,page,0)==-ESHUTDOWN);
    CHECK(owner->release(guest)==-ESHUTDOWN);
    CHECK(owner->write(guest,&marker,1)==-ESHUTDOWN);
    CHECK(owner->write_span(guest,&marker,1)==-ESHUTDOWN);
    bool refused=false;
    try { auto lease=owner->lease(); } catch (const std::logic_error&) { refused=true; }
    CHECK(refused);
    owner.reset();

    // Failed close must preserve storage and remain retryable, with context live.
    bool reject=false;
    unsigned calls=0;
    Owner retry([&] { if (reject) throw std::runtime_error("close refused"); ++calls; });
    CHECK(retry.reserve(guest,page)==0 && retry.map(guest,page,6)==0);
    CHECK(retry.write(guest,&marker,1)==0);
    reject=true; refused=false;
    try { retry.close(); } catch (const std::runtime_error&) { refused=true; }
    CHECK(refused);
    { auto lease=retry.lease(); CHECK(*lease.memory().translate(guest,1,4)==42); }
    CHECK(retry.read(guest,&unchanged,1)==0 && unchanged==42);
    reject=false; retry.close(); const auto after=calls;
    retry.close(); retry.clear(); CHECK(calls==after);

    // Closing an empty owner also prevents its later first allocation from
    // calling a dead context. clear() alone intentionally remains reusable.
    bool empty_alive=true;
    Owner empty([&] { CHECK(empty_alive); });
    empty.close(); empty_alive=false;
    CHECK(empty.reserve(guest,page)==-ESHUTDOWN);
    empty.close(); empty.clear();
    std::puts("PASS terminal close precedes context destruction; every reuse path refused; failed close retry");
}

int main() {
    context_shutdown();
    blocked_mutations();
    reuse_and_double_release();
    throwing_clear_and_unwind();
    shared_lifetime();
    termination_before_unmap();
}
