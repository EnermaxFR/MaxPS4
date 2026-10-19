#include "../Native/OrbisV4PRXLaunch.hpp"
#include "../Native/maxps4_ps4_libkernel.h"
#include "OrbisV4ImportFixture.h"
#include <cassert>
#include <future>
#include <chrono>
#include <iostream>
#include <time.h>
#include <type_traits>
using namespace maxps4;
int main() {
    static_assert(!std::is_move_constructible<OrbisV4Translation::Lease>::value,"Borrowed adapters pin one lease object");
    OrbisV4Translation owner([]{});
    const size_t page=owner.page_size(); const uint64_t base=0x500000;
    assert(owner.reserve(base,4*page)==0);
    assert(owner.map(base,page,6)==0 && owner.map(base+page,page,6)==0);
    const uint8_t halt=0xf4;
    assert(owner.map(base+3*page,page,5,&halt,1)==0);
    auto exercise=[&] {
        auto lease=owner.lease(); PS4GuestMemory memory(lease); PS4Libkernel kernel(memory);
        uint8_t source[16], readback[16]; std::fill(std::begin(source),std::end(source),0x5a);
        const uint64_t split=base+page-8;
        assert(memory.write(split,source,16) && memory.read(split,readback,16));
        assert(std::equal(std::begin(source),std::end(source),std::begin(readback)));
        timespec resolution{}; assert(::clock_getres(CLOCK_MONOTONIC,&resolution)==0);
        uint8_t guarded[18]; std::fill(std::begin(guarded),std::end(guarded),0xcc);
        assert(memory.write(split-1,guarded,sizeof(guarded)));
        assert(kernel.invoke(PS4KernelService::clock_getres,{4,split})==0);
        assert(memory.read(split-1,guarded,sizeof(guarded)) && guarded[0]==0xcc && guarded[17]==0xcc);
        assert(memory.read(split,readback,16));
        assert(OrbisMetadata::read(readback,8)==uint64_t(resolution.tv_sec));
        assert(OrbisMetadata::read(readback+8,8)==uint64_t(resolution.tv_nsec));
        assert(memory.write(split,source,16));
        assert(kernel.invoke(PS4KernelService::clock_getres,{99,split})==ps4::kernel_error(ps4::inval));
        assert(memory.read(split,readback,16) && !std::memcmp(source,readback,16));
        for (uint64_t bad:{uint64_t{0},base+2*page,UINT64_MAX-7,base+3*page,base+2*page-8}) {
            assert(kernel.invoke(PS4KernelService::clock_getres,{4,bad})==ps4::kernel_error(ps4::fault));
        }
        assert(!memory.write(base,source,OrbisV4Translation::Lease::copy_limit+1));
        assert(!memory.read(base,readback,OrbisV4Translation::Lease::copy_limit+1));
        assert(!memory.read(base,nullptr,1) && !memory.write(base,nullptr,1));
        assert(!memory.read(base,readback,0) && !memory.write(base,source,0));
        assert(!memory.write(base+3*page,source,1));
        uint8_t instruction=0; assert(memory.fetch(base+3*page,&instruction,1) && instruction==halt);
        assert(!memory.fetch(base,&instruction,1));
        const char name[]="guest"; char text[6]{};
        assert(memory.write(base,name,sizeof(name)) && memory.read_cstring(base,text,sizeof(text)) && !std::strcmp(text,name));
        // An entire span is validated before touching any byte.
        assert(memory.write(base+2*page-8,source,8));
        assert(!memory.write(base+2*page-8,readback,16));
        uint8_t unchanged[8]; assert(memory.read(base+2*page-8,unchanged,8) && !std::memcmp(unchanged,source,8));
        std::fill(std::begin(readback),std::end(readback),0x33);
        assert(!memory.read(base+2*page-8,readback,16));
        for (auto b:readback) assert(b==0x33);
        char text_before[8]; std::fill(std::begin(text_before),std::end(text_before),'?');
        assert(!memory.read_cstring(base+2*page-8,text_before,sizeof(text_before)));
        for (char c:text_before) assert(c=='?');
    };
    exercise();
    assert(owner.protect(base+page,page,4)==0);
    {
        auto lease=owner.lease(); PS4GuestMemory memory(lease); PS4Libkernel kernel(memory);
        uint8_t before[16],after[16]; assert(memory.read(base+page-8,before,16));
        assert(kernel.invoke(PS4KernelService::clock_getres,{4,base+page-8})==ps4::kernel_error(ps4::fault));
        assert(memory.read(base+page-8,after,16) && !std::memcmp(before,after,16));
    }
    assert(owner.protect(base+page,page,6)==0);
    auto first=std::async(std::launch::async,exercise), second=std::async(std::launch::async,exercise);
    first.get(); second.get(); // Leases serialize writes and keep permissions stable.
    std::promise<void> started;
    std::future<int> released;
    {
        auto lease=owner.lease(); PS4GuestMemory memory(lease);
        released=std::async(std::launch::async,[&] {started.set_value(); return owner.release(base);});
        started.get_future().wait();
        assert(released.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
        uint8_t byte=0; assert(memory.read(base,&byte,1));
    }
    assert(released.get()==0);
    { auto lease=owner.lease(); PS4GuestMemory memory(lease); PS4Libkernel kernel(memory);
      assert(kernel.invoke(PS4KernelService::clock_getres,{4,base})==ps4::kernel_error(ps4::fault)); }
    owner.close();
    uint8_t byte=0; assert(owner.read(base,&byte,1)==-ESHUTDOWN);
    bool closed=false; try {auto lease=owner.lease();} catch (const std::logic_error&) {closed=true;} assert(closed);

    auto bytes=v4_import_fixture::make_clock();
    maxps4_v4_prx_request request{bytes.data(),bytes.size(),orbis_execution::app_bias,orbis_execution::entry,
                                MAXPS4_V4_ABI_IMPORT_CLOCK_GETRES,7};
    std::unique_ptr<OrbisV4Translation> prepared; std::string diagnostic;
    assert(OrbisV4PRXLaunch::prepare(request,[]{},prepared,&diagnostic)==0 && prepared);
    const uint64_t initial=OrbisV4Startup::rsp(prepared->page_size()), returned=request.entry+25;
    assert(prepared->write_span(initial-16,&returned,8)==0);
    {
        auto lease=prepared->lease();
        auto gate=[&](uint64_t id,uint64_t output) {
            return OrbisV4Imports::gate(lease,request.entry,initial,OrbisV4Imports::thunk+5,
                                       OrbisV4Imports::token+1,initial-16,true,id,output);
        };
        assert(gate(4,orbis_execution::data+0x20));
        assert(!gate(0,orbis_execution::data+0x20));
        assert(!gate(4,orbis_execution::data+0x28));
        assert(!gate(4,0));
    }
    prepared->close(); prepared.reset();
    for (uint64_t bad:{uint64_t{0},UINT64_MAX-7,orbis_execution::entry,orbis_execution::data,orbis_execution::data+4,
                      orbis_execution::data+0x1000,
                      orbis_execution::data+0x1f8,OrbisV4Startup::stack,OrbisV4Imports::provider_bias+0x8000}) {
        bytes=v4_import_fixture::make_clock(bad); request.file=bytes.data();
        assert(OrbisV4PRXLaunch::prepare(request,[]{},prepared,&diagnostic)==-ENOTSUP && !prepared);
        assert(diagnostic.find("clock output")!=std::string::npos);
    }
    std::cout<<"PASS V4 leased libkernel clock_getres: split copies, wire layout, null/hole/overflow/RX/RO/released, atomic failure, concurrent leases, cleanup and output admission\n";
}
