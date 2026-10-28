#include "../Native/maxps4_ps4_libkernel.h"
#include "GuestELFFixture.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

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
    constexpr uint64_t slot = 0x10000, name = slot + 8, events = slot + 0x100,
                       count = slot + 0x20, timeout = slot + 0x28;
    const char label[] = "queue";
    assert(memory.write(name, label, sizeof(label)));
    auto read = [&](uint64_t address, size_t width = 8) {
        uint8_t wire[8]{}; assert(memory.read(address, wire, width)); return get_le(wire, width);
    };
    auto write = [&](uint64_t address, uint64_t value, size_t width) {
        uint8_t wire[8]{}; put_le(wire, 0, value, width); assert(memory.write(address, wire, width));
    };
    assert(threads.equeue_create(0, name) == inval);
    assert(threads.equeue_create(0x1000, name) == fault); // RX code is not an output.
    assert(threads.equeue_create(slot, UINT64_MAX) == fault);
    const char long_name[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    assert(memory.write(name + 0x40, long_name, sizeof(long_name)));
    assert(threads.equeue_create(slot, name + 0x40) == nametoolong);
    assert(threads.equeue_create(slot, name) == ok);
    const uint64_t queue = read(slot);
    assert(queue && threads.equeue_delete(queue + 1) == badf);
    write(timeout, 0, 4); write(count, 77, 4);
    assert(threads.equeue_wait(queue, events, 0, count, timeout) == inval && read(count, 4) == 0);
    assert(threads.equeue_wait(queue, events, 65, count, timeout) == inval);
    assert(threads.equeue_wait(queue, events, 1, count, timeout) == timedout && read(count, 4) == 0);
    assert(threads.user_event_trigger(queue, 7, 42) == noent);
    assert(threads.user_event_add(queue, -7, true) == ok);
    assert(threads.user_event_trigger(queue, -7, 0xfedcba9876543210ULL) == ok);
    assert(threads.equeue_wait(queue, 0x1000, 1, count, timeout) == fault);
    assert(threads.equeue_wait(queue, events, 1, events + 4, timeout) == inval);
    assert(threads.equeue_wait(queue, events, 1, count, UINT64_MAX) == fault);
    // A rejected output never consumes the pending edge. Check the exact Orbis wire ABI.
    assert(threads.equeue_wait(queue, events, 1, count, timeout) == ok && read(count, 4) == 1);
    assert(read(events) == uint64_t(int64_t(-7)) && read(events + 8, 2) == 0xfff5);
    assert(read(events + 10, 2) == 0x20 && read(events + 12, 4) == 0 && read(events + 16) == 0);
    assert(read(events + 24) == 0xfedcba9876543210ULL);
    assert(threads.equeue_wait(queue, events, 1, count, timeout) == timedout);
    // Level events remain ready until removed/re-registered; duplicate registration
    // replaces the mode, and multiple triggers coalesce to the latest user data.
    assert(threads.user_event_add(queue, 8, false) == ok);
    assert(threads.user_event_trigger(queue, 8, 1) == ok);
    assert(threads.user_event_trigger(queue, 8, 2) == ok);
    for (int i = 0; i < 2; ++i) {
        assert(threads.equeue_wait(queue, events, 1, count, timeout) == ok);
        assert(read(events + 24) == 2 && read(events + 10, 2) == 0);
    }
    assert(threads.user_event_add(queue, 8, true) == ok);
    assert(threads.equeue_wait(queue, events, 1, count, timeout) == ok);
    assert(threads.equeue_wait(queue, events, 1, count, timeout) == timedout);
    assert(threads.user_event_delete(queue, 8) == ok && threads.user_event_delete(queue, 8) == noent);
    // Bounded batches consume only the edges actually returned.
    for (int id : {10, 11}) {
        assert(threads.user_event_add(queue, id, true) == ok);
        assert(threads.user_event_trigger(queue, id, uint64_t(id)) == ok);
    }
    for (int id : {10, 11}) {
        assert(threads.equeue_wait(queue, events, 1, count, timeout) == ok);
        assert(read(events) == uint64_t(id));
    }
    assert(threads.equeue_wait(queue, events, 1, count, timeout) == timedout);
    write(timeout, 2000, 4);
    assert(threads.equeue_wait(queue, events, 1, count, timeout) == timedout);
    // Cross-thread wakeup. Either scheduling order must preserve the notification.
    std::atomic<bool> entered{false};
    std::thread waiter([&] {
        entered = true;
        assert(threads.equeue_wait(queue, events, 1, count, 0) == ok);
    });
    while (!entered) std::this_thread::yield();
    std::this_thread::sleep_for(10ms);
    assert(threads.user_event_trigger(queue, -7, 123) == ok);
    waiter.join();
    assert(read(events + 24) == 123);
    // A watchdog/process stop interrupts indefinite waits; resume permits reuse.
    entered = false;
    std::thread stopped([&] {
        entered = true;
        assert(threads.equeue_wait(queue, events, 1, count, 0) == intr);
    });
    while (!entered) std::this_thread::yield();
    std::this_thread::sleep_for(10ms);
    threads.interrupt(); stopped.join(); threads.resume();
    // Delete/reuse must wake the old waiter with EBADF, never attach it to a new queue.
    entered = false;
    std::thread deleted([&] {
        entered = true;
        assert(threads.equeue_wait(queue, events, 1, count, 0) == badf);
    });
    while (!entered) std::this_thread::yield();
    std::this_thread::sleep_for(10ms);
    assert(threads.equeue_delete(queue) == ok);
    assert(threads.equeue_create(slot, name) == ok && read(slot) != queue);
    deleted.join();
    assert(threads.user_event_add(queue, 1, true) == badf);
    const uint64_t fresh = read(slot);
    // Event and queue budgets are enforced, and reset invalidates every old handle.
    for (size_t i = 0; i < PS4Threads::event_limit; ++i)
        assert(threads.user_event_add(fresh, int32_t(i), true) == ok);
    assert(threads.user_event_add(fresh, 1000, true) == nomem);
    assert(threads.user_event_add(fresh, 0, false) == ok);
    for (size_t i = 1; i < PS4Threads::equeue_limit; ++i) assert(threads.equeue_create(slot, name) == ok);
    assert(threads.equeue_create(slot, name) == nomem);
    threads.reset();
    assert(threads.equeue_delete(fresh) == badf);
    assert(threads.equeue_create(slot, name) == ok && read(slot) != fresh);
    // The libkernel registry and register dispatch use these same objects and
    // the Orbis return convention. In particular a signed filter stays signed.
#ifndef MAXPS4_THREADS_ONLY
    PS4Libkernel kernel(memory);
    using S = PS4KernelService;
    assert(resolve_libkernel("D0OdFMjp46I", "libkernel", "libkernel", 1) == S::equeue_create);
    assert(resolve_libkernel("fzyMKs9kim0", "libkernel", "libkernel", 1) == S::equeue_wait);
    assert(general_only(S::equeue_wait));
    assert(kernel.invoke(S::equeue_create, {slot, name}) == 0);
    const uint64_t dispatched = read(slot);
    assert(kernel.invoke(S::user_event_add_edge, {dispatched, 17}) == 0);
    assert(kernel.invoke(S::user_event_trigger, {dispatched, 17, 456}) == 0);
    write(timeout, 0, 4);
    assert(kernel.invoke(S::equeue_wait, {dispatched, events, 1, count, timeout}) == 0);
    assert(kernel.invoke(S::event_id, {events}) == 17);
    assert(kernel.invoke(S::event_filter, {events}) == -11);
    assert(kernel.invoke(S::event_user_data, {events}) == 456);
    assert(kernel.invoke(S::event_data, {events}) == 0);
    assert(kernel.invoke(S::event_fflags, {events}) == 0);
    assert(kernel.invoke(S::event_error, {events}) == 0);
    write(events + 12, 22, 4);
    assert(kernel.invoke(S::event_fflags, {events}) == 22);
    assert(kernel.invoke(S::event_error, {events}) == kernel_error(inval));
    assert(kernel.invoke(S::event_id, {UINT64_MAX}) == kernel_error(fault));
    assert(kernel.invoke(S::equeue_wait, {dispatched, events, 1, count, timeout}) == kernel_error(timedout));
    std::thread dispatched_wait([&] {
        assert(kernel.invoke(S::equeue_wait, {dispatched, events, 1, count, 0}) == 0);
    });
    assert(kernel.invoke(S::user_event_trigger, {dispatched, 17, 789}) == 0);
    dispatched_wait.join();
    assert(kernel.invoke(S::event_user_data, {events}) == 789);
    assert(kernel.invoke(S::equeue_delete, {dispatched}) == 0);
    assert(kernel.invoke(S::equeue_delete, {dispatched}) == kernel_error(badf));
#endif
    // Semaphores: counts, polling, a waiter woken by a signal, timeout written
    // back, cancellation and deletion.
    threads.resume();
    constexpr uint64_t handle = slot + 0x400, left = slot + 0x408, result = slot + 0x410;
    assert(threads.sema_create(handle, name, 2, 1) == inval && threads.sema_create(handle, name, 1, 3) == ok);
    const uint64_t sema = read(handle);
    assert(threads.sema_wait(sema, 1, 0, true) == ok && threads.sema_wait(sema, 1, 0, true) == busy);
    assert(threads.sema_signal(sema, 4) == inval && threads.sema_wait(sema + 1, 1, 0, true) == srch);
    std::atomic<bool> taken{false};
    std::thread waiter([&] { assert(threads.sema_wait(sema, 2, 0, false) == ok); taken = true; });
    std::this_thread::sleep_for(20ms);
    assert(!taken && threads.sema_signal(sema, 1) == ok);
    std::this_thread::sleep_for(20ms);
    assert(!taken && threads.sema_signal(sema, 1) == ok);
    waiter.join();
    assert(taken);
    write(left, 20000, 4);
    assert(threads.sema_wait(sema, 1, left, false) == timedout && read(left, 4) == 0);
    std::thread cancelled([&] { assert(threads.sema_wait(sema, 1, 0, false) == canceled); });
    std::this_thread::sleep_for(20ms);
    assert(threads.sema_cancel(sema, 1, result) == ok && read(result, 4) == 1);
    cancelled.join();
    assert(threads.sema_wait(sema, 1, 0, true) == ok); // Cancel set the count to 1.
    std::thread deleted([&] { assert(threads.sema_wait(sema, 1, 0, false) == canceled); });
    std::this_thread::sleep_for(20ms);
    assert(threads.sema_delete(sema) == ok);
    deleted.join();
    assert(threads.sema_delete(sema) == srch);
    // Event flags: and/or waits, clearing modes, result pattern, single waiter.
    assert(threads.flag_create(handle, name, 0x10, 0x1) == ok);
    const uint64_t flag = read(handle);
    assert(threads.flag_wait(flag, 0, 1, result, 0, true) == inval);
    assert(threads.flag_wait(flag, 0x3, 1, result, 0, true) == busy && read(result) == 0x1);
    assert(threads.flag_wait(flag, 0x3, 2, result, 0, true) == ok && read(result) == 0x1);
    std::atomic<bool> woken{false};
    std::thread flagged([&] { assert(threads.flag_wait(flag, 0x6, 1 | 0x20, result, 0, false) == ok); woken = true; });
    std::this_thread::sleep_for(20ms);
    assert(threads.flag_wait(flag, 0x8, 2, 0, 0, false) == perm); // Single-waiter flag.
    assert(threads.flag_set(flag, 0x2, false) == ok);
    std::this_thread::sleep_for(20ms);
    assert(!woken && threads.flag_set(flag, 0x4, false) == ok);
    flagged.join();
    assert(woken && read(result) == 0x7);
    assert(threads.flag_wait(flag, 0x1, 2 | 0x10, result, 0, true) == ok && read(result) == 0x1); // Waited bits cleared, then all.
    assert(threads.flag_wait(flag, 0x1, 2, result, 0, true) == busy && read(result) == 0);
    assert(threads.flag_set(flag, 0xff, false) == ok && threads.flag_set(flag, 0x0f, true) == ok);
    assert(threads.flag_wait(flag, 0xf0, 2, result, 0, true) == busy && read(result) == 0x0f);
    write(left, 10000, 4);
    assert(threads.flag_wait(flag, 0xf0, 2, result, left, false) == timedout && read(left, 4) == 0);
    assert(threads.flag_delete(flag) == ok && threads.flag_delete(flag) == srch);
    std::puts("PASS Orbis event queues: user/edge events, wire ABI, polling, timeout, wakeup, interruption, deletion and bounds");
    std::puts("PASS kernel semaphores and event flags: counts, poll, wakeup, timeout, cancel, delete, modes, single waiter");
}
