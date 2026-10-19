#include "../Native/OrbisV4TLSMulti.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    using namespace maxps4;
    unsigned invalidations = 0;
    OrbisV4Translation owner([&] { ++invalidations; });
    const std::vector<OrbisV4TLSModule> modules{
        {1, 32, 16, {0x11, 0x22}},
        {2, 64, 64, {0x33, 0x44, 0x55}}
    };
    OrbisV4TLSMulti::Thread thread;
    assert(OrbisV4TLSMulti::initialize(owner, modules, thread) == 0);
    assert(thread.placements.size() == 2);
    assert(thread.placements[0].offset == 0);
    assert(thread.placements[1].offset == 64);
    assert(thread.storage.fs_base == thread.storage.tls_begin + 128);
    {
        auto lease = owner.lease();
        std::array<uint8_t, 128> data{};
        assert(lease.read_data(thread.storage.tls_begin, data.data(), data.size()) == 0);
        assert(data[0] == 0x11 && data[1] == 0x22 && data[2] == 0);
        assert(data[32] == 0 && data[63] == 0);
        assert(data[64] == 0x33 && data[65] == 0x44 && data[66] == 0x55 && data[67] == 0);
        std::array<uint64_t, 6> tcb{};
        assert(lease.read_data(thread.storage.fs_base, tcb.data(), sizeof(tcb)) == 0);
        assert(tcb[0] == thread.storage.fs_base);
    }
    OrbisV4TLSMulti::Thread collision;
    assert(OrbisV4TLSMulti::initialize(owner, modules, collision) != 0);
    assert(collision.storage.fs_base == 0);
    assert(owner.release(thread.storage.tls_begin) == 0);
    OrbisV4TLSMulti::Thread replacement;
    assert(OrbisV4TLSMulti::initialize(owner, modules, replacement) == 0);
    assert(replacement.storage.fs_base == thread.storage.fs_base);
    auto bad = modules;
    bad[1].id = bad[0].id;
    assert(OrbisV4TLSMulti::initialize(owner, bad, collision) == -EINVAL);
    assert(collision.storage.fs_base == 0);
    owner.close();
    assert(invalidations > 0);
    std::cout << "PASS V4 multi-module TLS backing, BSS, collision, release, rollback\n";
}
