// Exercise the actual patched upstream types, not replacement test doubles.
#include <FEXCore/Utils/SignalScopeGuards.h>
#include <FEXCore/Utils/WritePriorityMutex.h>
#include <atomic>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

static bool same_mask(const sigset_t& expected) {
    sigset_t current {};
    if (pthread_sigmask(SIG_SETMASK, nullptr, &current) != 0) return false;
    for (int signal = 1; signal < NSIG; ++signal) {
        if (sigismember(&current, signal) != sigismember(&expected, signal)) return false;
    }
    return true;
}

int main() {
    sigset_t initial {};
    if (pthread_sigmask(SIG_SETMASK, nullptr, &initial) != 0) return 1;
    bool masks = true;
    {
        FEXCore::ScopedSignalMasker outer(uint64_t{1} << (SIGUSR1 - 1));
        sigset_t blocked {};
        masks &= pthread_sigmask(SIG_SETMASK, nullptr, &blocked) == 0;
        masks &= sigismember(&blocked, SIGUSR1) == 1;
        {
            FEXCore::ScopedSignalMasker inner(uint64_t{1} << (SIGUSR2 - 1));
            FEXCore::ScopedSignalMasker moved(std::move(inner));
            sigset_t current {};
            masks &= pthread_sigmask(SIG_SETMASK, nullptr, &current) == 0;
            masks &= sigismember(&current, SIGUSR2) == 1;
            masks &= sigismember(&current, SIGUSR1) == 0;
        }
        masks &= same_mask(blocked);
    }
    masks &= same_mask(initial);
    if (!masks) { std::puts("FAIL nested/moved signal-mask restoration"); return 1; }

    FEXCore::Utils::WritePriorityMutex::Mutex mutex;
    unsigned value = 0; // Intentionally non-atomic: access must be protected.
    std::atomic<bool> start {false};
    std::atomic<bool> valid {true};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 8; ++i) {
        threads.emplace_back([&, i] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            unsigned previous = 0;
            for (unsigned j = 0; j < 2000; ++j) {
                if (i < 4) {
                    mutex.lock();
                    ++value;
                    mutex.unlock();
                } else {
                    mutex.lock_shared();
                    if (value < previous) valid.store(false);
                    previous = value;
                    mutex.unlock_shared();
                }
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    if (value != 8000 || !valid.load()) { std::puts("FAIL reader/writer contention"); return 1; }
    std::puts("PASS Darwin signal-mask restoration and reader/writer contention");
    return 0;
}
