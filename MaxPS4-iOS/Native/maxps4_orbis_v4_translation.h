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
    ~OrbisV4Translation() noexcept {
        std::lock_guard lock(mutex_);
        if (memory_.allocated()) {
            // Generated code embeds host addresses, including when callers
            // omit explicit clear(). Revoke them before releasing storage.
            // The invalidator/context must outlive nonempty ownership. A
            // throwing invalidator terminates rather than releasing memory
            // still referenced by native code; close()/clear() permit earlier retry.
            invalidate_();
            memory_.clear();
        }
        // After explicit close()/clear(), the context may already be destroyed.
        // Do not invoke its callback again for an empty address space.
    }
    // Finish ownership while the invalidator's context/threads are still live.
    // Unlike clear(), this is terminal: later cleanup is harmless and attempts
    // to reuse memory cannot call a context that may already be destroyed.
    // If invalidation throws, storage and the open state remain unchanged.
    void close() {
        std::lock_guard lock(mutex_);
        if (closed_) return;
        invalidate_();
        memory_.clear();
        closed_ = true;
    }
    class Lease {
    public:
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&&)=delete;
        Lease& operator=(Lease&&)=delete;
        explicit Lease(OrbisV4Translation& owner) : owner_(owner), lock_(owner.mutex_) {
            if (owner_.closed_) throw std::logic_error("Guest memory owner is closed");
            for (const auto& r : owner_.memory_.regions()) {
                // PROT_NONE is a valid protection state, not corrupt metadata.
                // Omit inaccessible ranges so native resolution faults safely.
                if (!r.permissions) continue;
                const auto* host = owner_.memory_.translate(r.address,r.size,r.permissions);
                if (!host) throw std::runtime_error("Inconsistent guest mapping");
                ranges_.push_back({r.address,reinterpret_cast<uintptr_t>(host),r.size,r.permissions});
            }
        }
        const std::vector<Range>& ranges() const { return ranges_; }
        const GuestAddressSpace& memory() const { return owner_.memory_; }
        static constexpr size_t copy_limit=1024*1024;
        // Data copies during dispatch must not reacquire the owner mutex or
        // invalidate code executing on this thread. Only RW/NX data is writable.
        // The lease pins all mappings/permissions until these copies complete.
        bool permitted(uint64_t guest,size_t bytes,uint32_t permissions) const {
            if (!guest || !bytes || bytes>copy_limit || guest>UINT64_MAX-bytes ||
                !permissions || (permissions&~7U) || (permissions&3U)==3U) return false;
            while (bytes) {
                size_t chunk=0;
                for (const auto& r:ranges_) if (guest>=r.guest && guest-r.guest<r.size) {
                    if ((r.permissions&permissions)!=permissions || ((permissions&2) && (r.permissions&1))) return false;
                    chunk=std::min(bytes,static_cast<size_t>(r.size-(guest-r.guest)));
                    break;
                }
                if (!chunk) return false;
                guest+=chunk; bytes-=chunk;
            }
            return true;
        }
        int read_data(uint64_t guest,void* output,size_t bytes,bool instruction=false) const {
            if (!output || !bytes || bytes>copy_limit || guest>UINT64_MAX-bytes) return -EINVAL;
            if (!permitted(guest,bytes,instruction ? 5 : 4)) return -EFAULT;
            return owner_.memory_.read(guest,output,bytes,instruction);
        }
        int write_data(uint64_t guest,const void* input,size_t bytes) {
            if (!input || !bytes || bytes>copy_limit || guest>UINT64_MAX-bytes) return -EINVAL;
            if (!permitted(guest,bytes,2)) return -EFAULT;
            return owner_.memory_.write(guest,input,bytes);
        }
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
    // Checked split-region slow paths. Hold the owner lock across validation and
    // copying so protect/unmap cannot invalidate a partially completed access.
    // GuestAddressSpace validates the entire span before changing destination.
    int read(uint64_t guest, void* destination, size_t bytes, bool instruction=false) {
        std::lock_guard lock(mutex_);
        if (closed_) return -ESHUTDOWN;
        return memory_.read(guest,destination,bytes,instruction);
    }
    int write_span(uint64_t guest, const void* source, size_t bytes) {
        std::lock_guard lock(mutex_);
        if (closed_) return -ESHUTDOWN;
        // Invalidate before writes: generated code may have cached guest bytes.
        invalidate_();
        return memory_.write(guest,source,bytes);
    }
    Lease lease() { return Lease(*this); }
    int set_limit(size_t bytes) {
        std::lock_guard lock(mutex_);
        if (closed_) return -ESHUTDOWN;
        return memory_.set_limit(bytes);
    }
    size_t page_size() const { return memory_.page_size(); }
    int reserve(uint64_t guest, size_t bytes) {
        std::lock_guard lock(mutex_);
        if (closed_) return -ESHUTDOWN;
        invalidate_();
        return memory_.reserve(guest,bytes);
    }
    int map(uint64_t guest, size_t bytes, uint32_t permissions,
            const void* source=nullptr, size_t count=0) {
        std::lock_guard lock(mutex_);
        if (closed_) return -ESHUTDOWN;
        invalidate_();
        return memory_.map(guest,bytes,permissions,source,count);
    }
    int protect(uint64_t guest, size_t bytes, uint32_t permissions) {
        std::lock_guard lock(mutex_);
        if (closed_) return -ESHUTDOWN;
        invalidate_();
        return memory_.protect(guest,bytes,permissions);
    }
    int release(uint64_t guest) {
        std::lock_guard lock(mutex_);
        if (closed_) return -ESHUTDOWN;
        invalidate_();
        return memory_.release(guest);
    }
    void clear() {
        std::lock_guard lock(mutex_);
        if (closed_) return;
        invalidate_(); memory_.clear();
    }
    int write(uint64_t guest, const void* source, size_t bytes) {
        std::lock_guard lock(mutex_);
        if (closed_) return -ESHUTDOWN;
        invalidate_();
        return memory_.write(guest,source,bytes);
    }
private:
    std::mutex mutex_;
    Invalidator invalidate_;
    GuestAddressSpace memory_;
    bool closed_ = false;
};
}

