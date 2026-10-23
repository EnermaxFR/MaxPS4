#include "../Native/maxps4_ps4_libkernel.h"
#include "GuestELFFixture.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

using namespace maxps4;
using namespace maxps4::ps4;
int main() {
    GuestELF image;
    auto file = guest_test::elf(40, 2, true);
    guest_test::put(file, 8192, 160, 8);
    assert(image.load(file.data(), file.size(), 6) == 0);
    PS4GuestMemory memory(image);
    PS4Libkernel kernel(memory);
    auto& threads = kernel.threads();
    using S = PS4KernelService;
    auto call = [&](S service, uint64_t slot = 0x10000, uint64_t thread = 1) {
        return kernel.invoke(service, {slot}, thread);
    };
    const auto second = threads.attach(); assert(second == 2);
    assert(call(S::thread_self) == 1 && call(S::thread_self, 0, second) == int64_t(second));
    assert(call(S::thread_self, 0, 99) == kernel_error(inval));
    assert(call(S::mutex_init, 0x1000) == kernel_error(fault));
    assert(kernel.invoke(S::mutex_init, {0x10000, 1}) == kernel_error(notsup));
    assert(kernel.invoke(S::mutex_init, {0x10000, 0, UINT64_MAX}) == kernel_error(fault));
    assert(call(S::mutex_init) == 0 && call(S::mutex_init) == kernel_error(busy));
    uint8_t wire[8]; assert(memory.read(0x10000, wire, sizeof(wire)));
    const auto token = get_le(wire, 8); assert(token >= 0x10000);
    assert(call(S::mutex_trylock) == 0);
    assert(call(S::mutex_trylock) == kernel_error(busy));
    assert(call(S::mutex_trylock, 0x10000, second) == kernel_error(busy));
    assert(call(S::mutex_unlock, 0x10000, second) == kernel_error(perm));
    assert(call(S::mutex_destroy) == kernel_error(busy));
    assert(call(S::mutex_unlock) == 0);
    assert(call(S::mutex_unlock) == kernel_error(perm));
    assert(call(S::mutex_trylock, 0x10000, second) == 0);
    assert(threads.detach(second) == busy);
    assert(call(S::mutex_unlock, 0x10000, second) == 0);
    assert(threads.detach(second) == ok && threads.detach(second) == inval);
    assert(call(S::mutex_destroy) == 0);
    assert(call(S::mutex_destroy) == kernel_error(inval));
    assert(call(S::mutex_trylock) == kernel_error(inval));
    assert(call(S::mutex_trylock, UINT64_MAX) == kernel_error(fault));
    assert(call(S::mutex_init, 0x10001) == kernel_error(fault));
    assert(call(S::mutex_init) == 0);
    assert(memory.read(0x10000, wire, sizeof(wire)) && get_le(wire, 8) > token);
    // Host workers represent externally scheduled guest IDs; this test exercises
    // actual exclusion but does not execute guest thread entry functions.
    std::vector<std::thread> workers;
    std::atomic<uint64_t> total{0};
    uint64_t protected_counter = 0;
    std::vector<uint64_t> ids;
    for (int i = 0; i < 8; ++i) ids.push_back(threads.attach());
    for (auto id : ids) workers.emplace_back([&, id] {
        for (int n = 0; n < 100; ++n) {
            while (true) {
                const auto error = call(S::mutex_trylock, 0x10000, id);
                if (error == 0) break;
                assert(error == kernel_error(busy)); std::this_thread::yield();
            }
            ++protected_counter;
            assert(call(S::mutex_unlock, 0x10000, id) == 0);
            ++total;
        }
    });
    for (auto& worker : workers) worker.join();
    assert(total == 800 && protected_counter == 800);
    for (auto id : ids) assert(threads.detach(id) == ok);
    assert(call(S::mutex_destroy) == 0);
    for (size_t i = 0; i < PS4Threads::mutex_limit; ++i)
        assert(call(S::mutex_init, 0x10000 + i * 8) == 0);
    assert(call(S::mutex_init, 0x11000) == kernel_error(again));
    while (threads.attach()) {}
    assert(threads.attach() == 0);
    uint8_t stale[8]; assert(memory.read(0x10000, stale, 8));
    kernel.reset();
    assert(call(S::mutex_trylock) == kernel_error(inval));
    assert(call(S::thread_self, 0, ids[0]) == kernel_error(inval));
    assert(call(S::mutex_init) == 0);
    assert(memory.write(0x11000, stale, 8));
    assert(call(S::mutex_trylock, 0x11000) == kernel_error(inval));
    assert(resolve_libkernel("upoVrzMHFeE", "libkernel", "libkernel", 1) == S::mutex_trylock);
    for (const char* unsupported : {"scePthreadDetach", "scePthreadMutexLock", "scePthreadCondWait"})
        assert(resolve_libkernel(unsupported, "libkernel", "libkernel", 1) == S::unresolved);
    std::puts("PS4 guest thread identity and nonblocking mutex checks passed");
}
