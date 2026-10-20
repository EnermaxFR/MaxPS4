#pragma once
#include "OrbisPRXPreparation.hpp"
#include "OrbisLinker.hpp"
#include "maxps4_orbis_v4_memory_bridge.h"
#include "maxps4_orbis_v4_translation.h"
#include <memory>

namespace maxps4 {
// Offline, single-owner transaction. No concurrent CPU, TLS setup, constructors,
// JIT registration or execution. Reuses the staging linker's mapping contract.
class OrbisPRXFEXBridge final {
public:
    bool map(const OrbisLinker::Image& image) {
        try { return commit(image.mappings()); }
        catch (const std::bad_alloc&) { return fail("Linker mapping allocation failed"); }
        catch (const std::runtime_error& error) { return fail(error.what()); }
    }
    bool map(const OrbisLinker::TLSImage& image) {
        try { return commit(image.mappings()); }
        catch(const std::bad_alloc&) {return fail("TLS mapping allocation failed");}
        catch(const std::runtime_error& error) {return fail(error.what());}
    }
    bool map(const OrbisPRXPreparation& prepared) {
        std::vector<OrbisLinker::Mapping> mappings;
        try {
            for (const auto& module : prepared.modules) {
                for (const auto& s : module.metadata.segments)
                    if (s.type == 7 || s.type == 0x6474e552)
                        return fail("TLS/GNU RELRO unsupported by bounded PRX mapping");
                for (const auto& r : module.regions) {
                    uint32_t flags = r.flags;
                    for (const auto& s : module.metadata.segments)
                        if (s.type == 0x61000010 && s.address <= UINT64_MAX-module.base &&
                            module.base+s.address == r.address && s.memsz == r.bytes.size())
                            flags = 4; // RELA already applied; seal SCE RELRO read-only/NX.
                    mappings.push_back({r.address,r.bytes.size(),flags,r.bytes.data()});
                }
            }
        } catch (const std::bad_alloc&) { return fail("PRX mapping allocation failed"); }
        return commit(std::move(mappings));
    }
    const GuestAddressSpace* memory() const { return memory_.get(); }
    const std::string& diagnostic() const { return diagnostic_; }
    // Snapshot a validated PRX into a separately owned, lease-pinned address
    // space. Serialize this call with map/unload on this offline bridge;
    // the invalidator must not reenter or mutate this staging bridge.
    // No CPU admission is granted. The invalidator must cover every FEX cache
    // using the returned owner and outlive its close/destruction.
    std::unique_ptr<OrbisV4Translation> make_translation(OrbisV4Translation::Invalidator invalidate) const {
        if (!memory_ || memory_->regions().empty())
            throw std::logic_error("PRX not mapped; translated snapshot refused");
        const auto& regions = memory_->regions();
        const uint64_t page = memory_->page_size();
        const uint64_t low = regions.front().address / page * page;
        const auto& last = regions.back();
        const uint64_t high = (last.address + last.size + page - 1) / page * page;
        auto pending = std::make_unique<OrbisV4Translation>(std::move(invalidate));
        // Headroom of the default budget for what is reserved next (return gate, stacks).
        if (high-low > GuestAddressSpace::budget && pending->set_limit(static_cast<size_t>(high-low) + GuestAddressSpace::budget))
            throw std::runtime_error("PRX translated reservation limit could not be raised");
        if (pending->reserve(low,static_cast<size_t>(high-low)))
            throw std::runtime_error("PRX translated reservation failed");
        for (const auto& r : regions) {
            // The validated staging owner remains live throughout each copy.
            const auto* source = memory_->translate(r.address,r.size,4);
            if (!source || pending->map(r.address,r.size,r.permissions,source,r.size))
                throw std::runtime_error("PRX translated mapping failed");
        }
        return pending; // Publish only after every region has been copied.
    }
    void unload() { memory_.reset(); }
    int admit_dispatch(uint64_t entry, const uint8_t*& host) {
        host = nullptr;
        if (!memory_) { diagnostic_ = "PRX not mapped; dispatch refused"; return -ENOENT; }
        const int status = OrbisV4MemoryBridge(*memory_).admit_fex_direct(entry,1,5,host);
        diagnostic_ = "PRX mapped to translated storage; FEX direct-memory dispatch refused (-ENOTSUP); translated JIT integration, stack/TLS and initialization remain unvalidated";
        return status;
    }
private:
    std::unique_ptr<GuestAddressSpace> memory_;
    std::string diagnostic_;
    bool fail(const char* why) { diagnostic_ = why; return false; }
    static constexpr uint64_t imageSpanMax = uint64_t(256) * 1024 * 1024;
    bool commit(std::vector<OrbisLinker::Mapping> mappings) {
        try {
            if (mappings.empty() || mappings.size() > 4096) return fail("Mapping count outside 1..4096");
            auto pending = std::make_unique<GuestAddressSpace>();
            const uint64_t page = pending->page_size(), limit = uint64_t{1}<<47;
            if (!page) return fail("Unsupported host page size");
            std::sort(mappings.begin(),mappings.end(),[](const auto& a,const auto& b){return a.address < b.address;});
            uint64_t low = mappings.front().address/page*page, high = low;
            for (const auto& r : mappings) {
                if (!r.size || !r.data || r.address < page || r.address > limit-page ||
                    r.size > limit-r.address-page) return fail("Invalid/overflowing guest range");
                if (!r.permissions || (r.permissions & ~7U) || (r.permissions & 3U) == 3U ||
                    ((r.permissions & 1U) && !(r.permissions & 4U)))
                    return fail("Invalid permissions or W+X guest mapping");
                const uint64_t start = r.address/page*page;
                const uint64_t end = (r.address+r.size+page-1)/page*page;
                if (start < high) return fail("Guest mappings collide at host-page granularity");
                high = end;
                // Process images may exceed the 32 MiB default (Sonic Mania's
                // eboot alone maps 14 MiB): the reservation limit is raised for
                // this image only, up to 256 MiB including gaps.
                if (high-low > imageSpanMax) return fail("Guest mapping span exceeds 256 MiB (including gaps)");
            }
            if (high-low > GuestAddressSpace::budget && pending->set_limit(size_t(high-low) + GuestAddressSpace::budget))
                return fail("Guest reservation limit could not be raised");
            int status = pending->reserve(low,size_t(high-low));
            if (status) { diagnostic_ = "Guest reservation failed: "+std::to_string(status); return false; }
            for (const auto& r : mappings) {
                status = pending->map(r.address,size_t(r.size),r.permissions,r.data,size_t(r.size));
                if (status) { diagnostic_ = "Guest mapping failed, transaction discarded: "+std::to_string(status); return false; }
            }
            // Allocate the report before publishing. Every failure retains the
            // previous image and releases all pending host reservations.
            diagnostic_ = "PRX mapped experimentally; guest RX/RW/NX enforced; no execution";
            memory_.swap(pending);
            return true;
        } catch (const std::bad_alloc&) { return fail("Guest mapping allocation failed; transaction discarded"); }
    }
};
}
