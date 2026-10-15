#pragma once
#include "maxps4_guest_address_space.h"
#include <functional>
#include <mutex>
#include <utility>
#include <stdexcept>

namespace maxps4 {
// All translated dispatch and mapping changes must go through this owner.
// A lease pins the mappings until generated native memory accesses finish.
// Invalidation runs under the same lock, BEFORE changing/releasing storage.
class OrbisV4Translation final {
public:
    struct Range { uint64_t guest, host, size; uint32_t permissions; };
    using Invalidator = std::function<void()>;
    explicit OrbisV4Translation(Invalidator invalidate) : invalidate_(std::move(invalidate)) {
        if (!invalidate_) throw std::invalid_argument("JIT invalidator required");
    }
    class Lease {
    public:
        explicit Lease(OrbisV4Translation& owner) : owner_(owner), lock_(owner.mutex_) {
            for (const auto& r : owner_.memory_.regions()) {
                const auto* host = owner_.memory_.translate(r.address,r.size,r.permissions);
                if (!host) throw std::runtime_error("Inconsistent guest mapping");
                ranges_.push_back({r.address,reinterpret_cast<uintptr_t>(host),r.size,r.permissions});
            }
        }
        const std::vector<Range>& ranges() const { return ranges_; }
        const GuestAddressSpace& memory() const { return owner_.memory_; }
        // Resolve only complete operands in one contiguous translated mapping.
        // The lease keeps host storage and permissions stable for the caller.
        // A split operand must take an explicit slow path, never a raw JIT load.
        int resolve_contiguous(uint64_t guest, size_t bytes, uint32_t permissions,
                               const uint8_t*& host) const {
            host = nullptr;
            if (!bytes || !permissions || (permissions & ~7U) ||
                (permissions & 3U) == 3U || guest > UINT64_MAX - bytes)
                return -EINVAL;
            for (const auto& r : ranges_) {
                if (guest < r.guest || guest - r.guest >= r.size) continue;
                const uint64_t offset = guest - r.guest;
                if (bytes > r.size - offset || (r.permissions & permissions) != permissions)
                    return -EFAULT;
                if (r.host > UINT64_MAX - offset) return -EFAULT;
                host = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(r.host + offset));
                return 0;
            }
            return -EFAULT;
        }
    private:
        OrbisV4Translation& owner_;
        std::unique_lock<std::mutex> lock_;
        std::vector<Range> ranges_;
    };
    Lease lease() { return Lease(*this); }
    size_t page_size() const { return memory_.page_size(); }
    int reserve(uint64_t guest, size_t bytes) {
        std::lock_guard lock(mutex_); invalidate_();
        return memory_.reserve(guest,bytes);
    }
    int map(uint64_t guest, size_t bytes, uint32_t permissions,
            const void* source=nullptr, size_t count=0) {
        std::lock_guard lock(mutex_); invalidate_();
        return memory_.map(guest,bytes,permissions,source,count);
    }
    int protect(uint64_t guest, size_t bytes, uint32_t permissions) {
        std::lock_guard lock(mutex_); invalidate_();
        return memory_.protect(guest,bytes,permissions);
    }
    int release(uint64_t guest) {
        std::lock_guard lock(mutex_); invalidate_();
        return memory_.release(guest);
    }
    void clear() {
        std::lock_guard lock(mutex_); invalidate_(); memory_.clear();
    }
    int write(uint64_t guest, const void* source, size_t bytes) {
        std::lock_guard lock(mutex_); invalidate_();
        return memory_.write(guest,source,bytes);
    }
private:
    std::mutex mutex_;
    Invalidator invalidate_;
    GuestAddressSpace memory_;
};
}
