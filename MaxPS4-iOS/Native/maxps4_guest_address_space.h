#pragma once
#include "maxps4_ps4_virtual_memory.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <new>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/vm_map.h>
#endif

namespace maxps4 {
// Guest addresses and host storage are distinct domains. FEX's direct-memory
// backend additionally requires equal numeric addresses; it must opt in and
// fails on a collision. mmap hints never replace an existing host mapping.
class GuestAddressSpace final : public PS4DynamicMemory {
public:
    enum class Backend { translated, fex_direct };
    struct ReservationFailure { uint64_t guest = 0; size_t bytes = 0; int host_status = 0; int error = 0; };
    const ReservationFailure& reservation_failure() const { return failure_; }
    struct Region { uint64_t address; size_t size; uint32_t permissions; };
    static constexpr size_t budget = 32 * 1024 * 1024;
    explicit GuestAddressSpace(Backend backend = Backend::translated) : backend_(backend) {
        const long p = ::sysconf(_SC_PAGESIZE);
        if (p == 4096 || p == 16384) page_ = static_cast<size_t>(p);
    }
    ~GuestAddressSpace() { clear(); }
    GuestAddressSpace(const GuestAddressSpace&) = delete;
    GuestAddressSpace& operator=(const GuestAddressSpace&) = delete;
    size_t page_size() const { return page_; }
    size_t allocated() const { return allocated_; }
    bool fex_direct() const { return backend_ == Backend::fex_direct; }
    const std::vector<Region>& regions() const { return regions_; }
    // Checked copies may span adjacent regions backed by different mappings.
    // Validate the complete span first; a hole/protection failure copies nothing.
    // Owner serializes map/protect/release against these operations.
    int read(uint64_t address, void* output, size_t size, bool instruction = false) const {
        if (!output) return -EINVAL;
        const uint32_t permission = instruction ? 5 : 4;
        const int error = check_span(address,size,permission);
        if (error) return error;
        try {
            std::vector<uint8_t> scratch(size);
            copy_span(address,scratch.data(),size,permission,false);
            std::memmove(output,scratch.data(),size);
        } catch (const std::bad_alloc&) { return -ENOMEM; }
        return 0;
    }
    int write(uint64_t address, const void* input, size_t size) {
        if (!input) return -EINVAL;
        const int error = check_span(address,size,2);
        if (error) return error;
        try {
            const auto* bytes = static_cast<const uint8_t*>(input);
            std::vector<uint8_t> scratch(bytes,bytes+size);
            copy_span(address,scratch.data(),size,2,true);
        } catch (const std::bad_alloc&) { return -ENOMEM; }
        return 0;
    }
    int reserve(uint64_t address, size_t size) {
        failure_ = {};
        if (!page_) return -ENOTSUP;
        if (!size || address % page_ || size % page_ || address < page_ ||
            address > (uint64_t{1} << 47) - page_ || size > (uint64_t{1} << 47) - address - page_)
            return -EINVAL;
        if (size > budget - allocated_) return -E2BIG;
        const uint64_t low = address - page_, high = address + size + page_;
        for (const auto& r : reservations_)
            if (low < r.address + r.size + page_ && r.address - page_ < high) return -EEXIST;
        if (backend_ == Backend::fex_direct && low < 0x100000000ULL) return -ENOTSUP;
        const size_t bytes = size + 2 * page_;
        void* hint = backend_ == Backend::fex_direct ? reinterpret_cast<void*>(low) : nullptr;
        void* host = MAP_FAILED;
#if defined(__APPLE__)
        if (hint) {
            // Darwin may relocate mmap hints. Mach fixed allocation, WITHOUT
            // VM_FLAGS_OVERWRITE, fails on existing mappings instead of replacing
            // them. Protect guards before publishing ownership or running FEX.
            vm_address_t requested = static_cast<vm_address_t>(low);
            const kern_return_t status = vm_allocate(mach_task_self(),&requested,bytes,VM_FLAGS_FIXED);

            if (status != KERN_SUCCESS) {
                const int error = status == KERN_NO_SPACE ? -EADDRINUSE : -ENOMEM;
                failure_ = {address,bytes,static_cast<int>(status),error};
                return error;
            }
            if (vm_protect(mach_task_self(),requested,bytes,FALSE,VM_PROT_NONE) != KERN_SUCCESS) {
                vm_deallocate(mach_task_self(),requested,bytes); return -EACCES;
            }
            host = reinterpret_cast<void*>(requested);
        } else
#endif
        host = ::mmap(hint, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (host == MAP_FAILED) {
            const int status = errno;
            failure_ = {address,bytes,status,-status};
            return -status;
        }
        if (hint && host != hint) {
            ::munmap(host, bytes);
            failure_ = {address,bytes,0,-EADDRINUSE}; // Hint relocated, no OS failure.
            return -EADDRINUSE;
        }
        try { reservations_.push_back({address,size,host}); }
        catch (...) { ::munmap(host,bytes); throw; }
        allocated_ += size;
        return 0;
    }
    int map(uint64_t address, size_t size, uint32_t permissions,
            const void* source = nullptr, size_t source_size = 0) {
        if (!size || !permissions || (permissions & ~7U) || (permissions & 3U) == 3U ||
            ((permissions & 1U) && !(permissions & 4U)) || source_size > size || (source_size && !source) || size > UINT64_MAX - address)
            return -EINVAL;
        auto* r = containing(address,size);
        if (!r) return -EFAULT;
        const uint64_t low = address / page_ * page_;
        const uint64_t high = (address + size + page_ - 1) / page_ * page_;
        for (const auto& old : regions_) {
            const uint64_t old_low = old.address / page_ * page_;
            const uint64_t old_high = (old.address + old.size + page_ - 1) / page_ * page_;
            if (low < old_high && old_low < high) return -EEXIST;
        }
        // Allocate metadata before making any host page accessible.
        regions_.push_back({address,size,permissions});
        auto* host = static_cast<uint8_t*>(r->host) + page_ + low - r->address;
        if (::mprotect(host,high-low,PROT_READ | PROT_WRITE)) {
            const int error = -errno; regions_.pop_back(); return error;
        }
        std::memset(host,0,high-low);
        if (source_size) std::memcpy(host + address - low,source,source_size);
        // Guest X is metadata. x86 bytes are never host executable on ARM64.
        const int protection = ((permissions & 4) ? PROT_READ : 0) | ((permissions & 2) ? PROT_WRITE : 0);
        if (::mprotect(host,high-low,protection)) {
            const int error = -errno;
            const uint64_t reservation_address = r->address;
            release(reservation_address); return error;
        }
        return 0;
    }
    int protect(uint64_t address, size_t size, uint32_t permissions) {
        if ((permissions & ~7U) || (permissions & 3U) == 3U ||
            ((permissions & 1U) && !(permissions & 4U))) return -EINVAL;
        for (auto& region : regions_) if (region.address == address && region.size == size) {
            auto* r = containing(address,size);
            const uint64_t low = address / page_ * page_;
            const size_t bytes = (address + size + page_ - 1) / page_ * page_ - low;
            void* host = static_cast<uint8_t*>(r->host) + page_ + low - r->address;
            const int p = ((permissions & 4) ? PROT_READ : 0) | ((permissions & 2) ? PROT_WRITE : 0);
            if (::mprotect(host,bytes,p)) return -errno;
            region.permissions = permissions;
            return 0;
        }
        return -EFAULT;
    }
    int release(uint64_t address) {
        for (auto it = reservations_.begin(); it != reservations_.end(); ++it) if (it->address == address) {
            if (::munmap(it->host,it->size+2*page_)) return -errno;
            const uint64_t end = address + it->size;
            regions_.erase(std::remove_if(regions_.begin(),regions_.end(),[&](const Region& r) {
                return r.address >= address && r.address < end;
            }),regions_.end());
            allocated_ -= it->size; reservations_.erase(it); return 0;
        }
        return -ENOENT;
    }
    void clear() {
        for (const auto& r : reservations_) ::munmap(r.host,r.size+2*page_);
        reservations_.clear(); regions_.clear(); allocated_ = 0;
    }
    const uint8_t* translate(uint64_t address, size_t size, uint32_t permissions) const override {
        if (!size || !permissions || (permissions & ~7U)) return nullptr;
        for (const auto& region : regions_)
            if (address >= region.address && address - region.address < region.size &&
                size <= region.size - (address-region.address) &&
                (region.permissions & permissions) == permissions)
                for (const auto& r : reservations_) if (address >= r.address && address - r.address < r.size)
                    return static_cast<const uint8_t*>(r.host) + page_ + address - r.address;
        return nullptr;
    }
    size_t contiguous(uint64_t address, size_t limit, uint32_t permissions) const override {
        if (!limit || !translate(address,1,permissions)) return 0;
        for (const auto& r : regions_) if (address >= r.address && address - r.address < r.size)
            return std::min(limit,r.size-static_cast<size_t>(address-r.address));
        return 0;
    }
private:
    struct Reservation { uint64_t address; size_t size; void* host; };
    ReservationFailure failure_{};
    Backend backend_;
    size_t page_ = 0, allocated_ = 0;
    std::vector<Reservation> reservations_;
    std::vector<Region> regions_;
    int check_span(uint64_t address, size_t size, uint32_t permission) const {
        if (!size || size > budget || size > UINT64_MAX-address) return -EINVAL;
        while (size) {
            const size_t chunk = contiguous(address,size,permission);
            if (!chunk) return -EFAULT;
            address += chunk; size -= chunk;
        }
        return 0;
    }
    void copy_span(uint64_t address, uint8_t* buffer, size_t size, uint32_t permission, bool write) const {
        while (size) {
            const size_t chunk = contiguous(address,size,permission);
            const auto* host = translate(address,chunk,permission);
            if (write) std::memcpy(const_cast<uint8_t*>(host),buffer,chunk);
            else std::memcpy(buffer,host,chunk);
            address += chunk; buffer += chunk; size -= chunk;
        }
    }
    Reservation* containing(uint64_t address, size_t size) {
        for (auto& r : reservations_) if (address >= r.address && address-r.address <= r.size &&
            size <= r.size-(address-r.address)) return &r;
        return nullptr;
    }
};
}

