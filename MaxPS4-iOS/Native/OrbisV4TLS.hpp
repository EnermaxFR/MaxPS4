#pragma once

#include "maxps4_orbis_v4_translation.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace maxps4 {

struct OrbisV4TLS {
    static constexpr uint64_t base = 0x710000000000ULL;
    static constexpr size_t max_tls_size = 1024 * 1024;

    struct Thread {
        uint64_t tls_begin = 0;
        uint64_t fs_base = 0;
        size_t mapped_size = 0;
    };

    static int initialize(
        OrbisV4Translation& owner,
        const std::vector<uint8_t>& initial,
        size_t memory_size,
        size_t alignment,
        Thread& output
    ) {
        output = {};

        if (!memory_size ||
            memory_size > max_tls_size ||
            initial.size() > memory_size ||
            alignment < 16 ||
            alignment > 16384 ||
            (alignment & (alignment - 1)) != 0) {
            return -EINVAL;
        }

        const size_t page = owner.page_size();

        if (!page || (page & (page - 1)) != 0)
            return -EINVAL;

        if (memory_size > SIZE_MAX - (alignment - 1))
            return -EOVERFLOW;

        const size_t block_size =
            (memory_size + alignment - 1) & ~(alignment - 1);

        constexpr size_t tcb_size = 48;

        if (block_size > SIZE_MAX - tcb_size - (page - 1))
            return -EOVERFLOW;

        const size_t mapped_size =
            (block_size + tcb_size + page - 1) & ~(page - 1);

        int status = owner.reserve(base, mapped_size);
        if (status)
            return status;

        std::vector<uint8_t> bytes;

        try {
            bytes.resize(mapped_size, 0);
        } catch (...) {
            owner.release(base);
            return -ENOMEM;
        }

        std::copy(initial.begin(), initial.end(), bytes.begin());

        const uint64_t fs_base = base + block_size;

        const std::array<uint64_t, 6> tcb = {
            fs_base,
            fs_base + 16,
            1,
            base,
            0,
            0
        };

        std::memcpy(bytes.data() + block_size,
                    tcb.data(),
                    sizeof(tcb));

        status = owner.map(
            base,
            mapped_size,
            6,
            bytes.data(),
            bytes.size()
        );

        if (status) {
            owner.release(base);
            return status;
        }

        output.tls_begin = base;
        output.fs_base = fs_base;
        output.mapped_size = mapped_size;

        return 0;
    }
};

}
