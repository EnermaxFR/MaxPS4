#include "../Native/OrbisV4TLS.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

int main() {
    using namespace maxps4;

    unsigned invalidations = 0;
    OrbisV4Translation owner([&] { ++invalidations; });

    OrbisV4TLS::Thread thread;
    const std::vector<uint8_t> initial{0x11, 0x22, 0x33, 0x44};

    assert(OrbisV4TLS::initialize(
        owner, initial, 64, 16, thread) == 0);

    assert(thread.tls_begin == OrbisV4TLS::base);
    assert(thread.fs_base == thread.tls_begin + 64);
    assert(thread.mapped_size >= 64 + 48);

    {
        auto lease = owner.lease();

        std::array<uint8_t, 64> data{};
        assert(lease.read_data(thread.tls_begin,
                               data.data(), data.size()) == 0);

        for (size_t i = 0; i < initial.size(); ++i)
            assert(data[i] == initial[i]);

        for (size_t i = initial.size(); i < data.size(); ++i)
            assert(data[i] == 0);

        std::array<uint64_t, 6> tcb{};
        assert(lease.read_data(thread.fs_base,
                               tcb.data(), sizeof(tcb)) == 0);

        assert(tcb[0] == thread.fs_base);
        assert(tcb[1] == thread.fs_base + 16);
        assert(tcb[2] == 1);
        assert(tcb[3] == thread.tls_begin);
    }

    OrbisV4TLS::Thread collision;
    assert(OrbisV4TLS::initialize(
        owner, initial, 64, 16, collision) != 0);

    assert(collision.tls_begin == 0);
    assert(collision.fs_base == 0);

    assert(owner.release(thread.tls_begin) == 0);

    OrbisV4TLS::Thread replacement;
    assert(OrbisV4TLS::initialize(
        owner, initial, 64, 16, replacement) == 0);

    assert(replacement.fs_base == thread.fs_base);

    owner.close();

    assert(invalidations > 0);

    std::cout
        << "PASS Orbis V4 TLS: initialization, BSS, "
        << "TCB, FS base, collision, release and cleanup\n";

    return 0;
}
