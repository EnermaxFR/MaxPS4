#include "../Native/maxps4_ps4_threads.h"
#include "GuestELFFixture.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

// Blocking guest mutexes and condition variables (PS4Threads). Host workers
// stand for guest threads with their own identities.
using namespace maxps4;
using namespace maxps4::ps4;
int main() {
    using namespace std::chrono_literals;
    GuestELF image;
    auto file = guest_test::elf(40, 2, true);
    guest_test::put(file, 8192, 160, 8);
    assert(image.load(file.data(), file.size(), 6) == 0);
    PS4GuestMemory memory(image);
    PS4Threads threads(memory);
    constexpr uint64_t m = 0x10000, adaptive = 0x10008, cv = 0x10010, data = 0x10018;
    uint8_t wire[8]{};
    const uint8_t zero[8]{}, one[8]{1};
    assert(memory.write(m, zero, 8) && memory.write(adaptive, one, 8) && memory.write(cv, zero, 8));
    // Static initializers create the object on first use.
    assert(threads.unlock(m, 1) == perm);
    assert(threads.lock(m, 1) == ok && memory.read(m, wire, 8) && get_le(wire, 8) > 2);
    assert(threads.lock(m, 1) == deadlk && threads.trylock(m, 1) == busy);
    assert(threads.trylock(adaptive, 1) == ok && threads.unlock(adaptive, 1) == ok);
    assert(threads.lock(data + 1, 1) == fault && threads.lock(UINT64_MAX - 7, 1) == fault);
    assert(threads.lock(m, 99) == inval);
    const uint64_t a = threads.attach(), b = threads.attach();
    assert(a && b);
    // A blocked lock returns once the owner unlocks.
    std::atomic<bool> taken{false};
    std::thread waiter([&] { assert(threads.lock(m, a) == ok); taken = true; assert(threads.unlock(m, a) == ok); });
    std::this_thread::sleep_for(20ms);
    assert(!taken);
    assert(threads.unlock(m, 1) == ok);
    waiter.join();
    assert(taken);
    // interrupt() ends blocked locks with EINTR until resume().
    assert(threads.lock(m, 1) == ok);
    std::thread stopped([&] { assert(threads.lock(m, a) == intr); });
    std::this_thread::sleep_for(20ms);
    threads.interrupt();
    stopped.join();
    assert(threads.lock(m, b) == intr);
    threads.resume();
    assert(threads.trylock(m, b) == busy);
    // Condition waits need the mutex; a timed wait times out and retakes it.
    assert(threads.cond_wait(cv, m, a, false, 0) == perm);
    assert(threads.cond_wait(cv, m, 1, true, 1000) == timedout);
    assert(threads.trylock(m, a) == busy && threads.unlock(m, 1) == ok);
    // Signal wakes one waiter, broadcast the rest; each returns holding the mutex.
    int arrived = 0, woken = 0; // Guarded by the guest mutex.
    std::vector<std::thread> waiters;
    for (uint64_t id : {a, b}) waiters.emplace_back([&, id] {
        assert(threads.lock(m, id) == ok);
        ++arrived;
        assert(threads.cond_wait(cv, m, id, false, 0) == ok);
        ++woken;
        assert(threads.unlock(m, id) == ok);
    });
    auto count = [&](int& value) {
        assert(threads.lock(m, 1) == ok);
        const int seen = value;
        assert(threads.unlock(m, 1) == ok);
        return seen;
    };
    while (count(arrived) < 2) std::this_thread::yield();
    assert(threads.cond_signal(cv, false) == ok);
    while (count(woken) < 1) std::this_thread::yield();
    std::this_thread::sleep_for(20ms);
    assert(count(woken) == 1);
    assert(threads.cond_destroy(cv) == busy);
    assert(threads.cond_signal(cv, true) == ok);
    for (auto& thread : waiters) thread.join();
    assert(woken == 2);
    // A signal with nobody waiting is lost.
    assert(threads.cond_signal(cv, false) == ok && threads.lock(m, 1) == ok);
    assert(threads.cond_wait(cv, m, 1, true, 1000) == timedout && threads.unlock(m, 1) == ok);
    // Destroyed objects stay invalid until initialized again.
    assert(threads.cond_destroy(cv) == ok && threads.cond_destroy(cv) == inval);
    assert(threads.cond_signal(cv, false) == inval);
    assert(threads.cond_init(cv, 0, 0) == ok && threads.cond_init(cv, 0, 0) == busy);
    assert(threads.destroy(m) == ok && threads.lock(m, 1) == inval && threads.trylock(m, 1) == inval);
    assert(threads.detach(a) == ok && threads.detach(b) == ok);
    std::puts("PS4 blocking mutex and condition variable checks passed");
}
