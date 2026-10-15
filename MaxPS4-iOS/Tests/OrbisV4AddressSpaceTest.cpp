#include "../Native/maxps4_orbis_v4_memory_bridge.h"
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
    std::puts("PASS Orbis V4 isolated guest address-space checks");
}
