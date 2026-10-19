#include "../Native/OrbisV4TLSRuntime.hpp"
#include "OrbisTLSMission45Fixture.h"
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
int main() {
    using namespace maxps4;
    mission45_fixture::Bundle fixture;
    OrbisLinker::TLSImage image;OrbisLinker::Report report;std::string reason;
    assert(OrbisLinker::linkTLS(fixture.inputs(),{1,2,3,4},{},image,reason,32*1024*1024,&report));
    assert(report.committed && report.tlsRelocations==16 && report.resolvedImports==1);
    const auto* patched=image.image().translate(0x808000,8,4);
    assert(patched && OrbisMetadata::read(patched,8)==2 && !image.image().translate(0x808000,8,2));
    patched=image.image().translate(0xc08000,8,4);assert(patched && OrbisMetadata::read(patched,8)==3);
    OrbisLinker::Image strict;
    assert(!OrbisLinker::link(fixture.inputs(),{},strict,reason));
    bool strictMapRefused=false;try {image.image().mappings();}catch(const std::runtime_error&){strictMapRefused=true;}
    assert(strictMapRefused);
    unsigned invalidations=0;
    std::unique_ptr<OrbisV4TLSRuntime> runtime;
    assert(OrbisV4TLSRuntime::load(image,[&]{++invalidations;},runtime,reason)==0);
    const auto main=runtime->primary();OrbisV4TLSRuntime::Thread second;
    assert(runtime->create_thread(second)==0);
    uint64_t mainFS=0,secondFS=0;
    {
        auto lease=runtime->lease(main);mainFS=lease.fs_base();
        uint64_t address=0;
        assert((lease.rsp()&15)==8 && lease.memory().permitted(lease.rsp(),8,6));
        assert(!lease.memory().permitted(lease.rsp(),8,1));
        assert(lease.resolve(2,0,4,address)==0);
        uint32_t initial=0;assert(lease.memory().read_data(address,&initial,4)==0 && initial==42);
        for(size_t offset:{size_t(8),size_t(512)}) {
            uint64_t relocated=0;assert(lease.resolve(2,offset,8,address)==0);
            assert(lease.memory().read_data(address,&relocated,8)==0 && relocated==0x804000);
        }
        assert(lease.resolve(2,0,4,address)==0);
        const uint32_t change=99;assert(lease.memory().write_data(address,&change,4)==0);
        assert(lease.resolve(3,0,64,address)==0);
        uint64_t zero=1;assert(lease.memory().read_data(address,&zero,8)==0 && zero==0);
        uint64_t unchanged=123;
        assert(lease.resolve(3,64,1,unchanged)==-EFAULT && unchanged==123);
        assert(lease.resolve(1,0,1,unchanged)==-ENOENT);
        assert(lease.resolve(129,0,1,unchanged)==-EINVAL);
        assert(lease.memory().write_data(mainFS,&zero,8)==-EFAULT);
        uint64_t dtv=0;assert(lease.memory().read_data(mainFS+8,&dtv,8)==0);
        assert(lease.memory().write_data(dtv+24,&zero,8)==-EFAULT);
    }
    {auto lease=runtime->lease(second);secondFS=lease.fs_base();uint64_t address=0;
     assert(secondFS!=mainFS && lease.resolve(2,0,4,address)==0);
     uint32_t initial=0;assert(lease.memory().read_data(address,&initial,4)==0 && initial==42);}
    assert(runtime->release_thread(main)==-EBUSY);
    // A held lease prevents another thread from unmapping its TLS or stack.
    std::promise<void> attempted;auto begin=attempted.get_future();std::future<int> release;
    {
        auto lease=runtime->lease(second);
        release=std::async(std::launch::async,[&]{attempted.set_value();return runtime->release_thread(second);});
        begin.wait();assert(release.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
    }
    assert(release.get()==0);
    bool stale=false;try {auto lease=runtime->lease(second);(void)lease;}catch(const std::logic_error&){stale=true;}
    assert(stale);
    OrbisV4TLSRuntime::Thread replacement;assert(runtime->create_thread(replacement)==0);
    {auto lease=runtime->lease(replacement);assert(lease.fs_base()==secondFS);}
    stale=false;try {auto lease=runtime->lease(second);(void)lease;}catch(const std::logic_error&){stale=true;}
    assert(stale);
    // A failed load preserves the currently live runtime; handles cannot be
    // borrowed across runtime instances, even with identical guest addresses.
    auto* originalRuntime=runtime.get();OrbisLinker::TLSImage empty;
    assert(OrbisV4TLSRuntime::load(empty,[]{},runtime,reason)==-ENOTSUP && runtime.get()==originalRuntime);
    std::unique_ptr<OrbisV4TLSRuntime> foreign;
    assert(OrbisV4TLSRuntime::load(image,[]{},foreign,reason)==0);
    bool foreignRefused=false;
    try {auto lease=foreign->lease(main);(void)lease;}catch(const std::logic_error&){foreignRefused=true;}
    assert(foreignRefused && foreign->release_thread(replacement)==-EINVAL);
    foreign->close();
    std::vector<OrbisV4TLSRuntime::Thread> allocated;
    for(size_t i=0;i<14;++i){OrbisV4TLSRuntime::Thread next;assert(runtime->create_thread(next)==0);allocated.push_back(next);}
    OrbisV4TLSRuntime::Thread beyond;
    assert(runtime->create_thread(beyond)==-EAGAIN);
    for(const auto& next:allocated)assert(runtime->release_thread(next)==0);
    auto preserved=image.image().translate(0x808000,8,4);
    auto invalid=fixture.inputs();invalid[1].bias=invalid[2].bias;
    assert(!OrbisLinker::linkTLS(invalid,{1,2,3,4},{},image,reason));
    assert(image.image().translate(0x808000,8,4)==preserved);
    runtime->close();assert(invalidations>0);
    assert(runtime->create_thread(second)==-ESHUTDOWN);
    stale=false;try {auto lease=runtime->lease(main);(void)lease;}catch(const std::logic_error&){stale=true;}
    assert(stale);
    std::cout<<"PASS TLS runtime: linked 16 DTPMOD64, sealed RELRO, TCB/DTV, RO metadata, private RW/NX stack and TLS, eager main/secondary, pinned release, stale/ABA/closed rejection; no CPU admission\n";
}
