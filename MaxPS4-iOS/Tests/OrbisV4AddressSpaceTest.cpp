#include "../Native/maxps4_orbis_v4_execution_gate.h"
#include "../Native/maxps4_orbis_v4_synthetic_stage.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
int main() {
    using maxps4::GuestAddressSpace;
    GuestAddressSpace space;
    const size_t page=space.page_size();
    CHECK(page==4096 || page==16384);
    const uint64_t base=0x200000000ULL;
    CHECK(space.reserve(base,2*page)==0);
    CHECK(space.reserve(base,2*page)==-EEXIST);
    CHECK(space.map(base,page,6)==0);
    maxps4::OrbisV4MemoryBridge bridge(space);
    maxps4::OrbisV4ExecutionGate gate(space);
    const uint8_t* executable=nullptr;
    const uint8_t* writable_stack=nullptr;
    CHECK(gate.preflight(base,base+page,page,executable,writable_stack)==-ENOTSUP);
    CHECK(executable==nullptr && writable_stack==nullptr);
    CHECK(gate.preflight(base,UINT64_MAX,page,executable,writable_stack)==-EOVERFLOW);
    CHECK(executable==nullptr && writable_stack==nullptr);
    CHECK(gate.preflight(0,base,page,executable,writable_stack)==-EINVAL);
    CHECK(gate.preflight(base,0,page,executable,writable_stack)==-EINVAL);
    CHECK(gate.preflight(base,base+page,0,executable,writable_stack)==-EINVAL);
    CHECK(gate.preflight(base,base+page,GuestAddressSpace::budget+1,
                         executable,writable_stack)==-EINVAL);
    CHECK(gate.preflight(base,base+page,page,executable,writable_stack)==-ENOTSUP);
    CHECK(executable==nullptr && writable_stack==nullptr);
    const uint8_t* direct=nullptr;
    CHECK(bridge.admit_fex_direct(base,1,4,direct)==-ENOTSUP);
    CHECK(direct==nullptr);
    CHECK(bridge.admit_fex_direct(base,0,4,direct)==-ENOTSUP);
    CHECK(direct==nullptr);
    CHECK(bridge.admit_fex_direct(base,1,8,direct)==-ENOTSUP);
    CHECK(direct==nullptr);
    // Translated storage must never be passed to FEX as direct guest VA.
    const uint8_t data[4]={4,2,1,7};
    uint8_t readback[4]={};
    CHECK(bridge.write_guest(base,data,sizeof(data))==0);
    // Exercise the same PS4DynamicMemory interface used by the guest runtime.
    const maxps4::PS4DynamicMemory& runtime_memory=space;
    CHECK(runtime_memory.translate(base,sizeof(readback),4)!=nullptr);
    CHECK(runtime_memory.contiguous(base,page,4)==page);
    CHECK(runtime_memory.translate(base,sizeof(readback),1)==nullptr);
    CHECK(bridge.read_guest(base,readback,sizeof(readback))==0);
    CHECK(std::memcmp(data,readback,sizeof(data))==0);
    CHECK(bridge.read_guest(base+page-2,readback,4)==-EFAULT); // No partial copy.
    CHECK(bridge.write_guest(base+page-2,data,4)==-EFAULT);
    CHECK(space.map(base+page,page,5)==0); // Guest RX.
    CHECK(bridge.write_guest(base+page,data,sizeof(data))==-EFAULT);
    CHECK(bridge.read_guest(base+page,readback,sizeof(readback),true)==0);
    CHECK(space.map(base+page,page,6)==-EEXIST);
    CHECK(space.protect(base,page,4)==0);
    CHECK(space.write(base,data,sizeof(data))==-EFAULT);
    CHECK(runtime_memory.translate(base,1,2)==nullptr);
    CHECK(runtime_memory.translate(base,1,4)!=nullptr);
    CHECK(bridge.read_guest(base,readback,sizeof(readback))==0);
    CHECK(bridge.read_guest(base,readback,sizeof(readback),true)==-EFAULT);
    CHECK(space.release(base)==0);
    CHECK(space.allocated()==0);
    CHECK(space.regions().empty());
    // Stage synthetic x86 bytes and a distinct writable stack, without FEX dispatch.
    GuestAddressSpace staged;
    const size_t staged_page=staged.page_size();
    const uint64_t staged_base=0x300000000ULL;
    const uint8_t code[]={0xb8,42,0,0,0,0xf4};
    CHECK(maxps4::stage_v4_synthetic_image(staged,staged_base,code,sizeof(code),
                                           staged_base+staged_page,staged_page)==0);
    uint8_t fetched[sizeof(code)]{};
    CHECK(staged.read(staged_base,fetched,sizeof(fetched),true)==0);
    CHECK(std::memcmp(fetched,code,sizeof(code))==0);
    CHECK(staged.write(staged_base,code,sizeof(code))==-EFAULT);
    CHECK(staged.read(staged_base+staged_page,fetched,sizeof(fetched),true)==-EFAULT);
    CHECK(staged.write(staged_base+staged_page,code,sizeof(code))==0);
    CHECK(maxps4::stage_v4_synthetic_image(staged,staged_base,code,sizeof(code),
                                           staged_base+staged_page,staged_page)==-EEXIST);
    CHECK(staged.release(staged_base)==0);
    CHECK(staged.allocated()==0);
    CHECK(maxps4::stage_v4_synthetic_image(staged,staged_base,nullptr,sizeof(code),
                                           staged_base+staged_page,staged_page)==-EINVAL);
    CHECK(maxps4::stage_v4_synthetic_image(staged,staged_base,code,sizeof(code),
                                           staged_base+2*staged_page,staged_page)==-EINVAL);
    std::puts("PASS Orbis V4 isolated guest address-space checks");
}
