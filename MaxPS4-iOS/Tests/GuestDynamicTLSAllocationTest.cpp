#include "maxps4_guest_dynamic_tls.h"
#include "OrbisM1Fixture.h"
#include <cstdio>
#include <cstdlib>
#include <new>

// Fail each allocation in resolve independently, including VM bookkeeping.
// Keep injection disabled while constructing fixtures and checking results.
static int remaining = -1;
void* operator new(std::size_t size) {
    if (remaining == 0) throw std::bad_alloc();
    if (remaining > 0) --remaining;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
int main() {
    using namespace maxps4;
    const auto bytes = orbis_m1_fixture::make(83,false);
    PS4ExecutableImage image;
    CHECK(image.load(bytes.data(),bytes.size()) == 0);
    GuestTLSModules modules;
    uint64_t module = 0;
    CHECK(modules.add(image,module) == 0);
    unsigned failures = 0;
    for (int allocation = 0; allocation < 8; ++allocation) {
        GuestAddressSpace space;
        {
            GuestDynamicTLS thread(space,modules,0x1000000000ULL);
            uint64_t address = 123;
            remaining = allocation;
            const int status = thread.resolve(module,0,16,address);
            remaining = -1;
            if (!status) break;
            ++failures;
            CHECK(status == -ENOMEM && address == 0);
            CHECK(space.allocated() == 0 && space.regions().empty());
            // Failed publication must neither return guest VA zero nor retain
            // a reservation that collides with this retry.
            CHECK(thread.resolve(module,0,16,address) == 0 && address != 0);
            uint64_t words[2]{};
            CHECK(space.read(address,words,sizeof(words)) == 0);
            CHECK(words[0] == 83 && words[1] == 0);
        }
        CHECK(space.allocated() == 0);
    }
    CHECK(failures >= 3);
    CHECK(modules.remove(module) == 0);
    std::printf("PASS TLS allocation failure rollback and retry (%u allocation sites); no FEX CPU execution\n",failures);
}
