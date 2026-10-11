// Calls the real libc-backed upstream allocator. No JIT/RX allocation or BRK.
#include <FEXCore/Utils/AllocatorHooks.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <unistd.h>

int main() {
    namespace Alloc = FEXCore::Allocator;
    for (std::size_t alignment : {std::size_t{1}, std::size_t{8}, std::size_t{64}}) {
        void* ptr = Alloc::memalign(alignment, 97);
        if (!ptr || reinterpret_cast<std::uintptr_t>(ptr) % alignment != 0) return 1;
        std::memset(ptr, 0x5A, 97);
        if (Alloc::malloc_usable_size(ptr) < 97) return 1;
        Alloc::free(ptr);
    }
    if (Alloc::memalign(0, 97) != nullptr || Alloc::memalign(3, 97) != nullptr) return 1;
    void* ptr = Alloc::valloc(31);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0 || !ptr || reinterpret_cast<std::uintptr_t>(ptr) % page_size != 0) return 1;
    Alloc::free(ptr);
    auto* bytes = static_cast<unsigned char*>(Alloc::calloc(4, 8));
    if (!bytes) return 1;
    for (unsigned i = 0; i < 32; ++i) if (bytes[i] != 0) return 1;
    Alloc::free(bytes);
    std::puts("PASS actual Darwin allocator alignment, size and zero initialization");
    return 0;
}
