#pragma once
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace maxps4 {
// Dynamic-table facts recognised by the staging linker (mission 48, see
// docs/mission48-keroblaster-dynamic-tags.md). Everything here is validated
// METADATA: nothing is executed, looked up through the hash table, or written.
// All addresses are module-relative guest addresses.
struct OrbisDynamicInfo {
    struct Array { bool present = false; uint64_t address = 0, size = 0; };
    // DT_SCE_HASH/DT_SCE_HASHSZ: SysV bucket/chain layout. The hash FUNCTION
    // used by Orbis is not established, so the table is never used to look a
    // symbol up; only its structure is proven (bounds, coverage, no cycles).
    bool hash = false;
    uint32_t buckets = 0, chains = 0;
    bool fingerprint = false;
    uint8_t fingerprintBytes[20] = {};
    std::string soname, originalFilename;
    bool init = false, fini = false; // DT_INIT/DT_FINI may legitimately be 0.
    uint64_t initAddress = 0, finiAddress = 0;
    Array preinitArray, initArray, finiArray;
    bool pltgot = false;
    uint64_t pltgotAddress = 0;
    bool textrel = false; // DT_TEXTREL/DF_TEXTREL declared; text is never made writable.
    std::map<uint16_t, uint64_t> exportLibraryAttributes, importLibraryAttributes;
    size_t recognisedTags = 0;

    // Proves the SysV bucket/chain structure against the symbol count.
    // words: nbucket, nchain, bucket[nbucket], chain[nchain] (little endian).
    static const char* validateHashStructure(const uint8_t* table, uint64_t bytes, uint64_t symbols,
                                             uint32_t& buckets, uint32_t& chains) {
        auto word = [&](uint64_t index) {
            const auto* p = table + index * 4;
            return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        };
        if (bytes < 8 || bytes % 4) return "hash table size";
        const uint64_t nbucket = word(0), nchain = word(1);
        if (!nbucket || nchain != symbols) return "hash chain count differs from symbol count";
        if (bytes / 4 != 2 + nbucket + nchain) return "hash table size differs from bucket/chain counts";
        if (word(2 + nbucket) != 0) return "hash chain[0] must be STN_UNDEF";
        std::vector<uint8_t> seen(size_t(nchain), 0);
        uint64_t visited = 0;
        for (uint64_t b = 0; b < nbucket; ++b) {
            for (uint64_t index = word(2 + b); index; index = word(2 + nbucket + index)) {
                if (index >= nchain) return "hash index out of bounds";
                if (seen[size_t(index)]) return "hash chain cycle or shared entry";
                seen[size_t(index)] = 1; ++visited;
            }
        }
        for (uint64_t i = 0; i < nchain; ++i) if (word(2 + nbucket + i) >= nchain) return "hash index out of bounds";
        if (visited != nchain - 1) return "hash chains do not cover every symbol exactly once";
        buckets = uint32_t(nbucket); chains = uint32_t(nchain);
        return nullptr;
    }
};
}
