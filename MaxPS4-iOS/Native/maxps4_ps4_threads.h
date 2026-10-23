#pragma once
#include "maxps4_ps4_abi.h"
#include "maxps4_ps4_guest_memory.h"
#include <array>
#include <condition_variable>
#include <mutex>

namespace maxps4 {
// Guest thread identities, mutexes and condition variables. Guest threads
// themselves run on FEX (GuestThreads in maxps4_fex_runtime.cpp), which
// attaches/detaches them here; x86 code never runs natively on a host thread.
// A guest slot holding 0 (or 1 for a mutex: the adaptive initializer) is a
// static initializer: the object is created on first use. Destroyed objects
// leave 2 in their slot, which stays invalid.
class PS4Threads final {
public:
    explicit PS4Threads(PS4GuestMemory& memory) : memory_(memory) { threads_[0] = 1; }
    uint64_t attach();
    ps4::Error detach(uint64_t thread);
    bool known(uint64_t thread) const;
    ps4::Error init(uint64_t slot, uint64_t attributes, uint64_t name);
    ps4::Error trylock(uint64_t slot, uint64_t thread);
    // Blocks until the mutex is free (error-checking: the owner gets EDEADLK).
    ps4::Error lock(uint64_t slot, uint64_t thread);
    ps4::Error unlock(uint64_t slot, uint64_t thread);
    ps4::Error destroy(uint64_t slot);
    // Condition attributes (clock, process sharing) do not change waits here.
    ps4::Error cond_init(uint64_t slot, uint64_t attributes, uint64_t name);
    ps4::Error cond_destroy(uint64_t slot);
    // Releases the caller's mutex, waits for a signal (or `timeout_us` when
    // `timed`), then takes the mutex again. ETIMEDOUT still retakes it.
    ps4::Error cond_wait(uint64_t cond, uint64_t mutex, uint64_t thread, bool timed, uint64_t timeout_us);
    ps4::Error cond_signal(uint64_t slot, bool all);
    // Wakes every blocked lock and wait with EINTR until resume(): a stopping
    // run's threads go back to guest code, where the stop signal lands.
    void interrupt();
    void resume();
    void reset();
    static constexpr size_t thread_limit = 64, mutex_limit = 256, cond_limit = 256;
private:
    struct Mutex { uint64_t token = 0, owner = 0, slot = 0; };
    // A signal or broadcast starts a new generation with `wakeups` waiters of
    // older generations allowed to return.
    struct Cond { uint64_t token = 0, slot = 0, generation = 0; uint32_t waiters = 0, wakeups = 0; };
    PS4GuestMemory& memory_;
    mutable std::mutex guard_;
    std::condition_variable changed_;
    std::array<uint64_t, thread_limit> threads_{};
    std::array<Mutex, mutex_limit> mutexes_{};
    std::array<Cond, cond_limit> conds_{};
    uint64_t next_thread_ = 2, next_mutex_ = 0x10000;
    bool interrupted_ = false;
    bool known_locked(uint64_t thread) const;
    bool writable(uint64_t slot) const;
    bool read_token(uint64_t slot, uint64_t& token) const;
    bool write_token(uint64_t slot, uint64_t token);
    Mutex* lookup(uint64_t slot);
    Mutex* acquire(uint64_t slot, ps4::Error& error);
    Cond* acquire_cond(uint64_t slot, ps4::Error& error);
};
} // namespace maxps4
