// Exercise public protocol entries with no validated trap handler. A regression
// reaching raw BRK terminates this test; do not install a SIGTRAP handler here.
#include <cstddef>
#include <cstdio>
extern "C" void maxps4_stikdebug_jit26_detach() noexcept;
extern "C" void* maxps4_stikdebug_jit26_prepare_region(void*, std::size_t) noexcept;
int main() {
    if (maxps4_stikdebug_jit26_prepare_region(nullptr, 16384) != nullptr) return 1;
    // Non-null input must not bypass trap validation either.
    int data = 0;
    if (maxps4_stikdebug_jit26_prepare_region(&data, sizeof(data)) != nullptr) return 1;
    maxps4_stikdebug_jit26_detach();
    std::puts("PASS unvalidated trap protocol refused without BRK (not JIT execution)");
}
