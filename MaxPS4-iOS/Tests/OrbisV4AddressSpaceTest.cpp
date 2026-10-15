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
    // Staged synthetic code is readable as guest RX, but translated host
    // storage must never be mistaken for an executable FEX-direct mapping.
    maxps4::OrbisV4ExecutionGate staged_gate(staged);
    const uint8_t* staged_executable=reinterpret_cast<const uint8_t*>(1);
    const uint8_t* staged_stack=reinterpret_cast<const uint8_t*>(1);
    CHECK(staged_gate.preflight(staged_base,staged_base+staged_page,staged_page,
                                staged_executable,staged_stack)==-ENOTSUP);
    CHECK(staged_executable==nullptr && staged_stack==nullptr);
    CHECK(staged.read(staged_base,fetched,sizeof(fetched),true)==0);
    CHECK(std::memcmp(fetched,code,sizeof(code))==0);
    CHECK(maxps4::stage_v4_synthetic_image(staged,staged_base,code,sizeof(code),
                                           staged_base+staged_page,staged_page)==-EEXIST);
    CHECK(staged.release(staged_base)==0);
    CHECK(staged.allocated()==0);
    // A pre-existing stack page must not be overwritten by staging.
    GuestAddressSpace collision;
    CHECK(collision.reserve(staged_base,2*staged_page)==0);
    CHECK(collision.map(staged_base+staged_page,staged_page,6)==0);
    CHECK(maxps4::stage_v4_synthetic_image(collision,staged_base,code,sizeof(code),
                                           staged_base+staged_page,staged_page)==-EEXIST);
    CHECK(collision.regions().size()==1);
    CHECK(collision.release(staged_base)==0);
    GuestAddressSpace direct_space(GuestAddressSpace::Backend::fex_direct);
    CHECK(maxps4::stage_v4_synthetic_image(direct_space,staged_base,code,sizeof(code),
                                           staged_base+staged_page,staged_page)==-ENOTSUP);
    CHECK(direct_space.allocated()==0);

    CHECK(maxps4::stage_v4_synthetic_image(staged,UINT64_MAX,code,sizeof(code),
                                           staged_base+staged_page,staged_page)==-EINVAL);
    CHECK(maxps4::stage_v4_synthetic_image(staged,staged_base,code,staged_page+1,
                                           staged_base+staged_page,staged_page)==-EINVAL);
    CHECK(maxps4::stage_v4_synthetic_image(staged,staged_base,code,sizeof(code),
                                           staged_base+staged_page,0)==-EINVAL);
    CHECK(staged.allocated()==0);
    CHECK(maxps4::stage_v4_synthetic_image(staged,staged_base,nullptr,sizeof(code),
                                           staged_base+staged_page,staged_page)==-EINVAL);
    CHECK(maxps4::stage_v4_synthetic_image(staged,staged_base,code,sizeof(code),
                                           staged_base+2*staged_page,staged_page)==-EINVAL);
    // A real occupied host address, independent of fixed-address availability.
    // Exercise #203's non-overwrite/rollback/retry contract on the V4 base.
    const size_t bytes = 3*page;
    void* occupied = ::mmap(nullptr,bytes,PROT_READ|PROT_WRITE,
                           MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(occupied != MAP_FAILED);
    auto* middle = static_cast<uint8_t*>(occupied)+page;
    const uint64_t guest = reinterpret_cast<uintptr_t>(middle);
    CHECK(guest >= 0x100000000ULL && guest < (uint64_t{1}<<47)-2*page);
    middle[0] = 0x59;
    CHECK(direct_space.reserve(guest,page)==-EADDRINUSE);
    const auto failure = direct_space.reservation_failure();
    CHECK(failure.guest==guest && failure.bytes==bytes && failure.error==-EADDRINUSE);
#if defined(__APPLE__)
    CHECK(failure.host_status==KERN_NO_SPACE);
#else
    CHECK(failure.host_status==0); // mmap hint relocated rather than failed.
#endif
    CHECK(middle[0]==0x59 && direct_space.allocated()==0 && direct_space.regions().empty());
    CHECK(!direct_space.translate(guest,1,4));
    // Distinct translated storage can use the same guest number without
    // touching the occupied host allocation, but MUST fail FEX admission.
    GuestAddressSpace separate;
    CHECK(separate.reserve(guest,page)==0 && separate.map(guest,page,6)==0);
    CHECK(separate.translate(guest,1,4)!=middle);
    CHECK(separate.write(guest,data,sizeof(data))==0 && middle[0]==0x59);
    maxps4::OrbisV4MemoryBridge translated_bridge(separate);
    const uint8_t* rejected=middle;
    CHECK(translated_bridge.admit_fex_direct(guest,1,4,rejected)==-ENOTSUP && !rejected);
    CHECK(::munmap(occupied,bytes)==0);
    CHECK(direct_space.reserve(guest,page)==0);
    CHECK(direct_space.reservation_failure().error==0);
    CHECK(direct_space.reservation_failure().guest==0);
    CHECK(direct_space.reservation_failure().bytes==0);
    CHECK(direct_space.reservation_failure().host_status==0);
    CHECK(direct_space.map(guest,page,5,code,sizeof(code))==0);
    CHECK(direct_space.translate(guest,sizeof(code),5)==middle);
    CHECK(!direct_space.translate(guest-1,1,4));
    CHECK(!direct_space.translate(guest+page,1,4));
    CHECK(direct_space.write(guest,data,sizeof(data))==-EFAULT);
    maxps4::OrbisV4ExecutionGate direct_gate(direct_space);
    executable=middle; writable_stack=middle;
    CHECK(direct_gate.preflight(guest,guest,page,executable,writable_stack)==-EFAULT);
    CHECK(!executable && !writable_stack); // No partially published code pointer.
    direct_space.clear();
    CHECK(direct_space.allocated()==0 && direct_space.regions().empty());
    CHECK(direct_space.reserve(guest,page)==0 && direct_space.release(guest)==0);
    CHECK(direct_space.release(guest)==-ENOENT);
    // Repeated translated lifecycles; invalid writes across RX preserve all bytes.
    for (int iteration=0; iteration<3; ++iteration) {
        GuestAddressSpace life;
        CHECK(life.reserve(base,0)==-EINVAL);
        CHECK(life.reserve(UINT64_MAX-page+1,page)==-EINVAL);
        CHECK(life.reserve(base,GuestAddressSpace::budget+page)==-E2BIG);
        CHECK(life.reserve(base,2*page)==0);
        CHECK(life.map(base,page,7)==-EINVAL); // W^X
        CHECK(life.map(base,page,6)==0 && life.map(base+page,page,5)==0);
        CHECK(life.write(base+page-2,data,4)==-EFAULT);
        uint8_t unchanged[2]={9,9};
        CHECK(life.read(base+page-2,unchanged,2)==0 && unchanged[0]==0 && unchanged[1]==0);
        CHECK(life.read(base,unchanged,0)==-EINVAL);
        CHECK(life.read(UINT64_MAX-1,unchanged,4)==-EINVAL);
        CHECK(life.protect(base,page,7)==-EINVAL);
        CHECK(life.protect(base,page,0)==0);
        CHECK(life.read(base,unchanged,2)==-EFAULT);
        CHECK(life.release(base)==0 && life.allocated()==0);
    }
    // A translated access spanning two separately mapped RW pages must preserve
    // guest ordering. A later permission downgrade must reject the whole write.
    GuestAddressSpace spanning;
    const uint64_t span_base = 0x500000000ULL;
    CHECK(spanning.reserve(span_base,2*page)==0);
    CHECK(spanning.map(span_base,page,6)==0);
    CHECK(spanning.map(span_base+page,page,6)==0);
    const uint8_t across[6]={11,22,33,44,55,66};
    uint8_t across_read[6]={};
    CHECK(spanning.write(span_base+page-3,across,sizeof(across))==0);
    CHECK(spanning.read(span_base+page-3,across_read,sizeof(across_read))==0);
    CHECK(std::memcmp(across,across_read,sizeof(across))==0);
    CHECK(spanning.protect(span_base+page,page,4)==0);
    const uint8_t replacement[6]={1,1,1,1,1,1};
    CHECK(spanning.write(span_base+page-3,replacement,sizeof(replacement))==-EFAULT);
    std::memset(across_read,0,sizeof(across_read));
    CHECK(spanning.read(span_base+page-3,across_read,sizeof(across_read))==0);
    CHECK(std::memcmp(across,across_read,sizeof(across))==0);
    CHECK(spanning.release(span_base)==0);
    std::puts("PASS Orbis V4 isolated guest address-space checks");
}
