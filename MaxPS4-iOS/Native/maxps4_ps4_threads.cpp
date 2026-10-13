#include "maxps4_ps4_threads.h"
#include <algorithm>

namespace maxps4 {
using namespace ps4;
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
PS4Threads::Mutex* PS4Threads::lookup(uint64_t slot) {
    uint8_t wire[8];
    if (!slot || (slot & 7U) || !memory_.read(slot, wire, 8)) return nullptr;
    const uint64_t token = get_le(wire, 8);
    for (auto& mutex : mutexes_) if (token && mutex.token == token) return &mutex;
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
        uint8_t wire[8]; put_le(wire, 0, next_mutex_, 8);
        if (!memory_.write(slot, wire, 8)) return fault;
        mutex = {next_mutex_++, 0, slot};
        return ok;
    }
    return again;
}
Error PS4Threads::trylock(uint64_t slot, uint64_t thread) {
    std::lock_guard<std::mutex> lock(guard_);
    if (!known_locked(thread)) return inval;
    if (!slot || !memory_.permitted(slot, 8, 4)) return fault;
    Mutex* mutex = lookup(slot);
    if (!mutex) return inval;
    if (mutex->owner) return busy;
    mutex->owner = thread;
    return ok;
}
Error PS4Threads::unlock(uint64_t slot, uint64_t thread) {
    std::lock_guard<std::mutex> lock(guard_);
    if (!known_locked(thread)) return inval;
    if (!slot || !memory_.permitted(slot, 8, 4)) return fault;
    Mutex* mutex = lookup(slot);
    if (!mutex) return inval;
    if (mutex->owner != thread) return perm;
    mutex->owner = 0;
    return ok;
}
Error PS4Threads::destroy(uint64_t slot) {
    std::lock_guard<std::mutex> lock(guard_);
    if (!writable(slot)) return fault;
    Mutex* mutex = lookup(slot);
    if (!mutex) return inval;
    if (mutex->owner) return busy;
    const uint8_t wire[8]{};
    if (!memory_.write(slot, wire, 8)) return fault;
    *mutex = {};
    return ok;
}
void PS4Threads::reset() {
    std::lock_guard<std::mutex> lock(guard_);
    threads_.fill(0); threads_[0] = 1;
    mutexes_.fill({}); // Keep counters monotonic: stale handles never reactivate.
}
} // namespace maxps4
