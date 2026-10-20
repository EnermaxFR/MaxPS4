#pragma once
// Experimental multi-module TLS backing store. Not an Orbis TCB/DTV ABI.
#include "OrbisV4TLS.hpp"
#include "OrbisV4TLSLayout.hpp"
#include <algorithm>
#include <cerrno>
#include <new>
#include <vector>

namespace maxps4 {
struct OrbisV4TLSMulti {
    struct Thread {
        OrbisV4TLS::Thread storage{};
        std::vector<OrbisV4TLSPlacement> placements;
    };
    static int initialize(OrbisV4Translation& owner,
                          const std::vector<OrbisV4TLSModule>& modules,
                          Thread& output) {
        // Preserve caller state on failure; storage is never published partially.
        try {
            std::vector<OrbisV4TLSPlacement> placements;
            size_t size = 0;
            int status = OrbisV4TLSLayout::plan(modules, placements, size);
            if (status) return status;
            std::vector<uint8_t> bytes;
            status = OrbisV4TLSLayout::instantiate(modules, placements, size, bytes);
            if (status) return status;
            size_t alignment = 16;
            for (const auto& module : modules)
                alignment = std::max(alignment, module.alignment);
            OrbisV4TLS::Thread storage;
            status = OrbisV4TLS::initialize(owner, bytes, size, alignment, storage);
            if (status) return status;
            Thread result;
            result.storage = storage;
            result.placements = std::move(placements);
            output = std::move(result);
            return 0;
        } catch (const std::bad_alloc&) {
            return -ENOMEM;
        }
    }
};
} // namespace maxps4
