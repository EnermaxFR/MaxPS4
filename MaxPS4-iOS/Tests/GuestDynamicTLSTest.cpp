#include "maxps4_guest_dynamic_tls.h"
#include "OrbisM1Fixture.h"
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
using namespace maxps4;
static_assert(!std::is_copy_constructible_v<GuestTLSModules>);
static_assert(!std::is_move_constructible_v<GuestTLSModules>);
static PS4ExecutableImage image(uint32_t value) {
    const auto bytes = orbis_m1_fixture::make(value,false);
    PS4ExecutableImage parsed;
    CHECK(parsed.load(bytes.data(),bytes.size()) == 0);
    return parsed;
}
int main() {
    GuestAddressSpace space;
    const size_t page = space.page_size();
    const uint64_t base = 0x1000000000ULL;
    CHECK(!space.fex_direct());
    CHECK(space.reserve(base,4*page) == 0);
    CHECK(space.map(base,page,6) == 0);
    CHECK(space.map(base+page,page,6) == 0);
    CHECK(space.map(base+3*page,page,5) == 0);
    const uint64_t value = 0xfedcba9876543210ULL;
    uint64_t output = 0;
    CHECK(space.write(base+page-4,&value,8) == 0);
    CHECK(space.read(base+page-4,&output,8) == 0 && output == value);
    // An absolute guest pointer remains a guest VA in memory, independent of
    // host placement; follow it through the checked interface.
    const uint64_t pointer = base+page-4;
    CHECK(space.write(base,&pointer,8) == 0);
    uint64_t loaded = 0;
    CHECK(space.read(base,&loaded,8) == 0 && loaded == pointer);
    CHECK(space.read(loaded,&output,8) == 0 && output == value);
    CHECK(space.read(base+page-4,&output,8,true) == -EFAULT);
    CHECK(space.read(base+2*page-4,&output,8) == -EFAULT && output == value);
    CHECK(space.write(base+2*page-4,&value,8) == -EFAULT);
    CHECK(space.read(base+2*page-4,&output,4) == 0 && (output & 0xffffffffULL) == 0);
    CHECK(space.write(base+3*page,&value,8) == -EFAULT);
    CHECK(space.read(UINT64_MAX-3,&output,8) == -EINVAL);
    CHECK(space.protect(base+page,page,4) == 0);
    CHECK(space.write(pointer,&output,8) == -EFAULT);
    CHECK(space.read(pointer,&output,8) == 0 && output == value);
    // Overlapping host buffers must preserve memmove semantics across chunks.
    auto* host = const_cast<uint8_t*>(space.translate(base,page,6));
    CHECK(space.read(base+4,host, page) == 0);
    CHECK(space.release(base) == 0 && space.allocated() == 0);
    CHECK(space.read(pointer,&output,8) == -EFAULT);

    GuestTLSModules modules;
    uint64_t first = 0, second = 0;
    CHECK(modules.add(image(17),first) == 0 && first == 1);
    auto aligned = image(83);
    for (auto& h : aligned.headers) if (h.type == 7) {
        h.alignment = 16384; h.address = h.address/16384*16384;
        for (auto& s : aligned.segments) if (h.address >= s.header.address &&
            h.address-s.header.address+16 <= s.bytes.size()) {
            h.offset = s.header.offset+h.address-s.header.address;
            const uint64_t initial = 83;
            std::memcpy(s.bytes.data()+h.address-s.header.address,&initial,8);
        }
    }
    CHECK(modules.add(aligned,second) == 0 && second == 2);
    {
        GuestDynamicTLS one(space,modules,base), two(space,modules,base+0x4000000);
        uint64_t a = 0, b = 0, c = 0, d = 0;
        CHECK(one.resolve(first,0,16,a) == 0);
        CHECK(one.resolve(second,0,16,b) == 0 && b % 16384 == 0);
        CHECK(two.resolve(first,0,16,c) == 0 && a != c);
        CHECK(two.resolve(second,0,16,d) == 0 && b != d);
        uint64_t words[2]{};
        CHECK(space.read(a,words,16) == 0 && words[0] == 17 && words[1] == 0);
        CHECK(space.read(d,words,16) == 0 && words[0] == 83 && words[1] == 0);
        CHECK(space.write(a,&value,8) == 0);
        CHECK(space.read(c,words,16) == 0 && words[0] == 17 && words[1] == 0);
        CHECK(space.read(b,words,16) == 0 && words[0] == 83 && words[1] == 0);
        uint64_t again = 0;
        CHECK(one.resolve(first,8,8,again) == 0 && again == a+8);
        CHECK(one.resolve(first,16,1,again) == -EFAULT && again == 0);
        CHECK(one.resolve(first,UINT64_MAX,8,again) == -EFAULT);
        CHECK(one.resolve(999,0,8,again) == -ENOENT);
        CHECK(modules.remove(first) == -EBUSY);
        // Module addition after thread creation: lazy initialization is per thread.
        uint64_t third = 0;
        CHECK(modules.add(image(129),third) == 0);
        CHECK(one.resolve(third,0,8,again) == 0);
        CHECK(space.read(again,&output,8) == 0 && output == 129);
        const size_t allocated = space.allocated();
        GuestDynamicTLS collision(space,modules,base);
        CHECK(collision.resolve(first,0,8,again) == -EEXIST && again == 0);
        CHECK(space.allocated() == allocated);
    }
    CHECK(space.allocated() == 0);
    CHECK(modules.remove(first) == 0);
    CHECK(!modules.find(first));
    uint64_t new_id = 0;
    CHECK(modules.add(image(1),new_id) == 0 && new_id > second);
    auto malformed = image(1);
    for (auto& h : malformed.headers) if (h.type == 7) h.alignment = 3;
    CHECK(modules.add(malformed,new_id) == -ENOEXEC && new_id == 0);
    auto missing = image(1);
    missing.headers.erase(std::remove_if(missing.headers.begin(),missing.headers.end(),
        [](const auto& h) { return h.type == 7; }),missing.headers.end());
    CHECK(modules.add(missing,new_id) == -ENOENT && new_id == 0);
    {
        GuestDynamicTLS invalid(space,modules,UINT64_MAX-page+1);
        CHECK(invalid.resolve(second,0,8,new_id) == -EINVAL && new_id == 0);
        GuestDynamicTLS misaligned(space,modules,base+page);
        if (page < 16384) CHECK(misaligned.resolve(second,0,8,new_id) == -EINVAL);
    }
    GuestTLSModules capacity;
    for (size_t i=0;i<16;++i) CHECK(capacity.add(image(1),new_id) == 0);
    CHECK(capacity.add(image(1),new_id) == -E2BIG && new_id == 0);
    CHECK(space.allocated() == 0);
    std::puts("PASS translated multi-region copies, absolute guest pointers, overflow/protection rollback and lazy multi-module TLS isolation; no FEX CPU execution");
}
