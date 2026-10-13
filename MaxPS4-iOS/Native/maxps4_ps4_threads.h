#pragma once
#include "maxps4_ps4_abi.h"
#include "maxps4_ps4_guest_memory.h"
#include <array>
#include <mutex>

namespace maxps4 {
// Guest thread identities supplied by a future CPU scheduler. Never launch an
// x86 function using host pthread_create. No guest execution or wait scheduler.
class PS4Threads final {
public:
    explicit PS4Threads(PS4GuestMemory& memory) : memory_(memory) { threads_[0] = 1; }
    uint64_t attach();
    ps4::Error detach(uint64_t thread);
    bool known(uint64_t thread) const;
    ps4::Error init(uint64_t slot, uint64_t attributes, uint64_t name);
    ps4::Error trylock(uint64_t slot, uint64_t thread);
    ps4::Error unlock(uint64_t slot, uint64_t thread);
    ps4::Error destroy(uint64_t slot);
    void reset();
    static constexpr size_t thread_limit = 64, mutex_limit = 256;
private:
    struct Mutex { uint64_t token = 0, owner = 0, slot = 0; };
    PS4GuestMemory& memory_;
    mutable std::mutex guard_;
    std::array<uint64_t, thread_limit> threads_{};
    std::array<Mutex, mutex_limit> mutexes_{};
    uint64_t next_thread_ = 2, next_mutex_ = 0x10000;
    bool known_locked(uint64_t thread) const;
    bool writable(uint64_t slot) const;
    Mutex* lookup(uint64_t slot);
};
} // namespace maxps4
