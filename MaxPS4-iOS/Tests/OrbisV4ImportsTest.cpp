#include "../Native/OrbisV4PRXLaunch.hpp"
#include "OrbisV4ImportFixture.h"
#include <cassert>
#include <iostream>
int main() {
    const auto original=v4_import_fixture::make();
    auto bytes=original;
    maxps4_v4_prx_request r{bytes.data(),bytes.size(),orbis_execution::app_bias,
                          orbis_execution::entry,MAXPS4_V4_ABI_IMPORT_THREAD_SELF,5};
    std::unique_ptr<maxps4::OrbisV4Translation> owner;
    std::string diagnostic;
    unsigned invalidations=0;
    auto prepare=[&] {r.file=bytes.data(); return maxps4::OrbisV4PRXLaunch::prepare(r,[&]{++invalidations;},owner,&diagnostic);};
    for (unsigned i=0;i<10;++i) {
        assert(prepare()==0 && owner);
        uint64_t got=0;
        assert(owner->read(orbis_execution::data,&got,8)==0 && got==maxps4::OrbisV4Imports::thunk);
        const auto initial=maxps4::OrbisV4Startup::rsp(owner->page_size());
        const uint64_t returned=r.entry+10;
        assert(owner->write_span(initial-16,&returned,8)==0);
        {
            auto lease=owner->lease();
            for (const auto& range:lease.ranges()) assert(range.guest!=range.host);
            assert(!lease.memory().translate(got,1,2));
            assert(!lease.memory().translate(orbis_execution::data,8,1));
            assert(!lease.memory().translate(initial-16,8,1));
            auto gate=[&](uint64_t rip,uint64_t token,uint64_t rsp) {
                return maxps4::OrbisV4Imports::gate(lease,r.entry,initial,rip,token,rsp);
            };
            assert(gate(got+5,maxps4::OrbisV4Imports::token,initial-16));
            assert(!gate(got+6,maxps4::OrbisV4Imports::token,initial-16));
            assert(!gate(got+5,0,initial-16));
            assert(!gate(got+5,maxps4::OrbisV4Imports::token,initial-8));
            assert(!gate(got+5,maxps4::OrbisV4Imports::token,UINT64_MAX));
        }
        const uint64_t wrong=returned+1;
        assert(owner->write_span(initial-16,&wrong,8)==0);
        { auto lease=owner->lease();
          assert(!maxps4::OrbisV4Imports::gate(lease,r.entry,initial,got+5,maxps4::OrbisV4Imports::token,initial-16)); }
        owner->close(); owner.reset();
    }
    assert(invalidations>10);
    auto refuse=[&](const char* category) {
        const int status=prepare();
        if (status!=-ENOTSUP || owner || diagnostic.find(category)==std::string::npos)
            std::cerr<<"expected "<<category<<" status="<<status<<" diagnostic="<<diagnostic<<"\n";
        assert(status==-ENOTSUP && !owner && diagnostic.find(category)!=std::string::npos);
    };
    bytes[0x200]=0x0f; refuse("body must"); bytes=original;
    bytes[0x206]++; refuse("relocated GOT"); bytes=original;
    r.instruction_budget=4; refuse("budget"); r.instruction_budget=5;
    r.entry++; refuse("entry mismatch"); r.entry--;
    const std::string nid="aI+OeCz8xrQ";
    auto at=std::search(bytes.begin(),bytes.end(),nid.begin(),nid.end()); *at='Z';
    refuse("unresolved NID ZI+OeCz8xrQ"); bytes=original;
    at=std::search(bytes.begin(),bytes.end(),nid.begin(),nid.end());
    const std::string other="UK2Tl2DWUns"; std::copy(other.begin(),other.end(),at);
    refuse("unresolved NID UK2Tl2DWUns"); bytes=original; // Existing close service is NOT authorized here.
    orbis_fixture::appendTag(bytes,12,0x4000); refuse("init/fini"); bytes=original;
    orbis_fixture::setTag(bytes,0x61000015,1|(uint64_t{2}<<32)|(uint64_t{1}<<48));
    refuse("library"); bytes=original;
    orbis_fixture::put(bytes,0x908,(uint64_t{1}<<32)|2); refuse("relocation"); bytes=original;
    orbis_fixture::put(bytes,0x908,(uint64_t{1}<<32)|1); refuse("JUMP_SLOT"); bytes=original;
    orbis_fixture::put(bytes,0x910,1); refuse("zero-addend"); bytes=original;
    orbis_fixture::put(bytes,0x900,0x4000); refuse("writable"); bytes=original;
    orbis_fixture::put(bytes,0x900,UINT64_MAX); refuse("RELA"); bytes=original;
    orbis_fixture::put(bytes,64+4,7,4); refuse("writable executable"); bytes=original;
    orbis_fixture::put(bytes,64+56+16,0x4000); refuse("chevauch"); bytes=original;
    r.load_bias=maxps4::OrbisV4Imports::provider_bias; r.entry=r.load_bias+0x4000;
    refuse("overlap"); r.load_bias=orbis_execution::app_bias; r.entry=orbis_execution::entry;
    orbis_fixture::put(bytes,64+56,7,4); refuse("TLS"); bytes=original;
    r.load_bias=UINT64_MAX; r.entry=UINT64_MAX; refuse("overflow");
    r.load_bias=orbis_execution::app_bias; r.entry=orbis_execution::entry;
    assert(prepare()==0 && owner); owner->close();
    std::cout<<"PASS V4 imports: existing SCE linker/HLE registry, guest JUMP_SLOT, RX thunk, gate token/RIP/stack, unknown NID, relocation, collision, overflow, W^X/NX and cleanup; no CPU execution\n";
}
