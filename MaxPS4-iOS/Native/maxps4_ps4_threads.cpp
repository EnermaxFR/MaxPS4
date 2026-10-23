#include "maxps4_ps4_threads.h"
#include <algorithm>
#include <chrono>

namespace maxps4 {
using namespace ps4;
namespace {
// Slot values with a meaning of their own (see PS4Threads).
constexpr uint64_t static_initializer = 0, adaptive_initializer = 1, destroyed = 2;
}
bool PS4Threads::known_locked(uint64_t thread) const {
    return thread && std::find(threads_.begin(), threads_.end(), thread) != threads_.end();
}
bool PS4Threads::known(uint64_t thread) const {
    std::lock_guard<std::mutex> lock(guard_); return known_locked(thread);
}
uint64_t PS4Threads::attach() {
    std::lock_guard<std::mutex> lock(guard_);
    if (next_thread_ == UINT64_MAX) return 0;
    for (auto& thread : threads_) if (!thread) { thread = next_thread_++; return thread; }
    return 0;
}
Error PS4Threads::detach(uint64_t thread) {
    std::lock_guard<std::mutex> lock(guard_);
    if (!known_locked(thread) || thread == 1) return inval;
    for (const auto& mutex : mutexes_) if (mutex.owner == thread) return busy;
    for (auto& value : threads_) if (value == thread) { value = 0; break; }
    return ok;
}
bool PS4Threads::writable(uint64_t slot) const {
    return slot && !(slot & 7U) && memory_.permitted(slot, 8, 6) && !memory_.permitted(slot, 8, 1);
}
bool PS4Threads::read_token(uint64_t slot, uint64_t& token) const {
    uint8_t wire[8];
    if (!slot || (slot & 7U) || !memory_.read(slot, wire, 8)) return false;
    token = get_le(wire, 8);
    return true;
}
bool PS4Threads::write_token(uint64_t slot, uint64_t token) {
    uint8_t wire[8]; put_le(wire, 0, token, 8);
    return memory_.write(slot, wire, 8);
}
PS4Threads::Mutex* PS4Threads::lookup(uint64_t slot) {
    uint64_t token = 0;
    if (!read_token(slot, token)) return nullptr;
    for (auto& mutex : mutexes_) if (token && mutex.token == token) return &mutex;
    return nullptr;
}
PS4Threads::Mutex* PS4Threads::acquire(uint64_t slot, Error& error) {
    uint64_t token = 0;
    if (!slot || !memory_.permitted(slot, 8, 4) || !read_token(slot, token)) { error = fault; return nullptr; }
    if (token != static_initializer && token != adaptive_initializer) {
        Mutex* mutex = lookup(slot);
        if (!mutex) error = inval;
        return mutex;
    }
    if (!writable(slot)) { error = fault; return nullptr; }
    if (next_mutex_ == UINT64_MAX) { error = again; return nullptr; }
    for (auto& mutex : mutexes_) if (!mutex.token) {
        if (!write_token(slot, next_mutex_)) { error = fault; return nullptr; }
        mutex = {next_mutex_++, 0, slot};
        return &mutex;
    }
    error = again;
    return nullptr;
}
Error PS4Threads::init(uint64_t slot, uint64_t attributes, uint64_t name) {
    std::lock_guard<std::mutex> lock(guard_);
    if (attributes) return notsup; // Default private/error-check mutex only.
    if (!writable(slot)) return fault;
    if (name) {
        char text[1024];
        if (!memory_.read_cstring(name, text, sizeof(text))) return fault;
    }
    for (const auto& mutex : mutexes_) if (mutex.token && mutex.slot == slot) return busy;
    if (next_mutex_ == UINT64_MAX) return again;
    for (auto& mutex : mutexes_) if (!mutex.token) {
        if (!write_token(slot, next_mutex_)) return fault;
        mutex = {next_mutex_++, 0, slot};
        return ok;
    }
    return again;
}
Error PS4Threads::trylock(uint64_t slot, uint64_t thread) {
    std::lock_guard<std::mutex> lock(guard_);
    if (!known_locked(thread)) return inval;
    Error error = ok;
    Mutex* mutex = acquire(slot, error);
    if (!mutex) return error;
    if (mutex->owner) return busy;
    mutex->owner = thread;
    return ok;
}
Error PS4Threads::lock(uint64_t slot, uint64_t thread) {
    std::unique_lock<std::mutex> lock(guard_);
    if (!known_locked(thread)) return inval;
    Error error = ok;
    Mutex* mutex = acquire(slot, error);
    if (!mutex) return error;
    if (mutex->owner == thread) return deadlk;
    // A free mutex can be destroyed before this thread wakes: never take
    // another mutex's record.
    const uint64_t token = mutex->token;
    changed_.wait(lock, [&] { return interrupted_ || mutex->token != token || !mutex->owner; });
    if (interrupted_) return intr;
    if (mutex->token != token) return inval;
    mutex->owner = thread;
    return ok;
}
Error PS4Threads::unlock(uint64_t slot, uint64_t thread) {
    {
        std::lock_guard<std::mutex> lock(guard_);
        if (!known_locked(thread)) return inval;
        if (!slot || !memory_.permitted(slot, 8, 4)) return fault;
        uint64_t token = 0;
        if (read_token(slot, token) && (token == static_initializer || token == adaptive_initializer)) return perm;
        Mutex* mutex = lookup(slot);
        if (!mutex) return inval;
        if (mutex->owner != thread) return perm;
        mutex->owner = 0;
    }
    changed_.notify_all();
    return ok;
}
Error PS4Threads::destroy(uint64_t slot) {
    std::lock_guard<std::mutex> lock(guard_);
    if (!writable(slot)) return fault;
    Mutex* mutex = lookup(slot);
    if (!mutex) return inval;
    if (mutex->owner) return busy;
    if (!write_token(slot, destroyed)) return fault;
    *mutex = {};
    return ok;
}
PS4Threads::Cond* PS4Threads::acquire_cond(uint64_t slot, Error& error) {
    uint64_t token = 0;
    if (!slot || !memory_.permitted(slot, 8, 4) || !read_token(slot, token)) { error = fault; return nullptr; }
    if (token != static_initializer) {
        for (auto& cond : conds_) if (cond.token == token) return &cond;
        error = inval;
        return nullptr;
    }
    if (!writable(slot)) { error = fault; return nullptr; }
    if (next_mutex_ == UINT64_MAX) { error = again; return nullptr; }
    for (auto& cond : conds_) if (!cond.token) {
        if (!write_token(slot, next_mutex_)) { error = fault; return nullptr; }
        cond = {next_mutex_++, slot, 0, 0, 0};
        return &cond;
    }
    error = again;
    return nullptr;
}
Error PS4Threads::cond_init(uint64_t slot, uint64_t, uint64_t name) {
    std::lock_guard<std::mutex> lock(guard_);
    if (!writable(slot)) return fault;
    if (name) {
        char text[1024];
        if (!memory_.read_cstring(name, text, sizeof(text))) return fault;
    }
    for (const auto& cond : conds_) if (cond.token && cond.slot == slot) return busy;
    if (next_mutex_ == UINT64_MAX) return again;
    for (auto& cond : conds_) if (!cond.token) {
        if (!write_token(slot, next_mutex_)) return fault;
        cond = {next_mutex_++, slot, 0, 0, 0};
        return ok;
    }
    return again;
}
Error PS4Threads::cond_destroy(uint64_t slot) {
    std::lock_guard<std::mutex> lock(guard_);
    if (!writable(slot)) return fault;
    uint64_t token = 0;
    if (!read_token(slot, token)) return fault;
    if (token == static_initializer) return ok; // Never used: nothing to free.
    const auto cond = std::find_if(conds_.begin(), conds_.end(),
        [token](const Cond& c) { return c.token && c.token == token; });
    if (cond == conds_.end()) return inval;
    if (cond->waiters) return busy;
    if (!write_token(slot, destroyed)) return fault;
    *cond = {};
    return ok;
}
Error PS4Threads::cond_wait(uint64_t cond_slot, uint64_t mutex_slot, uint64_t thread, bool timed, uint64_t timeout_us) {
    std::unique_lock<std::mutex> lock(guard_);
    if (!known_locked(thread)) return inval;
    Error error = ok;
    Cond* cond = acquire_cond(cond_slot, error);
    if (!cond) return error;
    if (!mutex_slot || !memory_.permitted(mutex_slot, 8, 4)) return fault;
    Mutex* mutex = lookup(mutex_slot);
    if (!mutex) return inval;
    if (mutex->owner != thread) return perm;
    // A waiter keeps the condition alive (destroy is EBUSY). The mutex is free
    // meanwhile: if it is destroyed, the wait ends with EINVAL.
    const uint64_t generation = cond->generation, mutex_token = mutex->token;
    ++cond->waiters;
    mutex->owner = 0;
    changed_.notify_all();
    auto signalled = [&] { return cond->generation != generation && cond->wakeups; };
    auto woken = [&] { return interrupted_ || signalled(); };
    if (timed) {
        // Saturate: an enormous timeout is an untimed wait.
        const auto limit = std::chrono::microseconds(std::min<uint64_t>(timeout_us, uint64_t(1) << 50));
        changed_.wait_for(lock, limit, woken);
    } else {
        changed_.wait(lock, woken);
    }
    const bool consumed = !interrupted_ && signalled();
    --cond->waiters;
    if (consumed) --cond->wakeups;
    cond->wakeups = std::min(cond->wakeups, cond->waiters);
    changed_.wait(lock, [&] { return interrupted_ || mutex->token != mutex_token || !mutex->owner; });
    if (interrupted_) return intr;
    if (mutex->token != mutex_token) return inval;
    mutex->owner = thread;
    return consumed ? ok : timedout;
}
Error PS4Threads::cond_signal(uint64_t slot, bool all) {
    {
        std::lock_guard<std::mutex> lock(guard_);
        Error error = ok;
        Cond* cond = acquire_cond(slot, error);
        if (!cond) return error;
        if (cond->wakeups >= cond->waiters) return ok; // Nobody left to wake.
        cond->wakeups = all ? cond->waiters : cond->wakeups + 1;
        ++cond->generation;
    }
    changed_.notify_all();
    return ok;
}
void PS4Threads::interrupt() {
    { std::lock_guard<std::mutex> lock(guard_); interrupted_ = true; }
    changed_.notify_all();
}
void PS4Threads::resume() {
    std::lock_guard<std::mutex> lock(guard_);
    interrupted_ = false;
}
void PS4Threads::reset() {
    {
        std::lock_guard<std::mutex> lock(guard_);
        threads_.fill(0); threads_[0] = 1;
        // Keep counters monotonic: stale handles never reactivate.
        mutexes_.fill({}); conds_.fill({});
        interrupted_ = false;
    }
    changed_.notify_all();
}
} // namespace maxps4
