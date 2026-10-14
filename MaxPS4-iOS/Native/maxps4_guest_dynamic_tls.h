#pragma once
#include "maxps4_guest_tls.h"
#include <map>

namespace maxps4 {
// Loader-side dynamic TLS storage. This resolver returns guest addresses only;
// it is not an implementation of an unverified SCE __tls_get_addr import/DTV.
// Process owner serializes registry and thread access and outlives each thread.
class GuestTLSModules final {
public:
    int add(const PS4ExecutableImage& image, uint64_t& id) {
        id = 0;
        GuestTLSTemplate candidate;
        const int status = candidate.load(image);
        if (status) return status;
        if (!candidate.memory_size) return -ENOENT;
        if (modules_.size() == 16 || next_ == UINT64_MAX) return -E2BIG;
        modules_.emplace(next_, std::move(candidate));
        id = next_++;
        return 0;
    }
    int remove(uint64_t id) {
        if (!modules_.count(id)) return -ENOENT;
        // A live thread can contain pointers into any module. Do not allow
        // unload/reuse to turn these into dangling or aliased guest pointers.
        if (threads_) return -EBUSY;
        modules_.erase(id);
        return 0;
    }
    const GuestTLSTemplate* find(uint64_t id) const {
        const auto it = modules_.find(id);
        return it == modules_.end() ? nullptr : &it->second;
    }
private:
    friend class GuestDynamicTLS;
    std::map<uint64_t, GuestTLSTemplate> modules_;
    uint64_t next_ = 1;
    size_t threads_ = 0;
};

class GuestDynamicTLS final {
public:
    GuestDynamicTLS(GuestAddressSpace& space, GuestTLSModules& modules, uint64_t base)
        : space_(space), modules_(modules), base_(base) { ++modules_.threads_; }
    ~GuestDynamicTLS() { clear(); --modules_.threads_; }
    GuestDynamicTLS(const GuestDynamicTLS&) = delete;
    GuestDynamicTLS& operator=(const GuestDynamicTLS&) = delete;
    int resolve(uint64_t module, uint64_t offset, size_t length, uint64_t& address) {
        address = 0;
        const auto* tls = modules_.find(module);
        if (!tls) return -ENOENT;
        if (!length || offset >= tls->memory_size || length > tls->memory_size-offset)
            return -EFAULT;
        auto it = blocks_.find(module);
        if (it == blocks_.end()) {
            const size_t page = space_.page_size();
            if (!page || base_ % std::max(page,tls->alignment) || blocks_.size() >= 16 ||
                base_ > UINT64_MAX-16*stride) return -EINVAL;
            const uint64_t block = base_+blocks_.size()*stride;
            const size_t size = (tls->block_size+page-1)/page*page;
            // Allocate bookkeeping before acquiring VM; publish only success.
            it = blocks_.emplace(module, 0).first;
            int error = space_.reserve(block,size);
            const bool reserved = error == 0;
            if (!error) error = space_.map(block,size,6,tls->initial.data(),tls->initial.size());
            if (error) {
                if (reserved) space_.release(block);
                blocks_.erase(it);
                return error;
            }
            it->second = block;
        }
        address = it->second+offset;
        return 0;
    }
    void clear() {
        for (const auto& block : blocks_) space_.release(block.second);
        blocks_.clear();
    }
private:
    static constexpr uint64_t stride = 0x200000;
    GuestAddressSpace& space_;
    GuestTLSModules& modules_;
    uint64_t base_;
    std::map<uint64_t, uint64_t> blocks_;
};
}
