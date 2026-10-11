#include "maxps4_guest_elf.h"
#include "GuestELFFixture.h"
#include <cerrno>
#include <cstdio>
#include <csignal>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
    using guest_test::put;
    maxps4::GuestELF image;
    auto file = guest_test::elf();
    CHECK(image.load(nullptr, file.size(), 3) == -EINVAL);
    CHECK(image.load(file.data(), 63, 3) == -EINVAL);
    CHECK(image.load(file.data(), file.size(), 0) == -EINVAL);
    CHECK(image.load(file.data(), file.size(), 1000001) == -EINVAL);
    auto rejects = [&](size_t offset, uint64_t value, size_t width) {
        auto broken = file;
        put(broken, value, offset, width);
        return image.load(broken.data(), broken.size(), 3) != 0 && !image.entry_pointer() && image.segments().empty();
    };
    CHECK(rejects(0, 0, 1)); CHECK(rejects(4, 1, 1)); CHECK(rejects(5, 2, 1));
    CHECK(rejects(6, 0, 1)); CHECK(rejects(16, 3, 2)); CHECK(rejects(18, 183, 2));
    CHECK(rejects(20, 0, 4)); CHECK(rejects(52, 0, 2)); CHECK(rejects(54, 55, 2));
    CHECK(rejects(56, 0, 2)); CHECK(rejects(56, 65, 2)); CHECK(rejects(32, 63, 8));
    CHECK(rejects(32, UINT64_MAX, 8)); CHECK(rejects(40, 64, 8)); CHECK(rejects(60, 1, 2));
    CHECK(rejects(64, 2, 4)); CHECK(rejects(72, UINT64_MAX, 8));
    CHECK(rejects(96, 33, 8)); CHECK(rejects(104, 0, 8));
    CHECK(rejects(80, UINT64_MAX - 10, 8)); CHECK(rejects(104, UINT64_MAX, 8));
    CHECK(rejects(112, 3, 8)); CHECK(rejects(112, 4096, 8));
    CHECK(rejects(68, 7, 4)); CHECK(rejects(68, 1, 4)); CHECK(rejects(68, 13, 4));
    CHECK(rejects(68, 4, 4)); CHECK(rejects(24, 0, 8)); CHECK(rejects(24, 0x100B, 8));
    CHECK(image.load(file.data(), file.size() - 1, 3) == -ENOEXEC);
    CHECK(image.load(file.data(), file.size(), 2) == -E2BIG);
    CHECK(rejects(130, 0xC3, 1)); CHECK(rejects(120, 0xEB, 1));
    CHECK(rejects(120, 0x05, 1)); CHECK(rejects(125, 0xF4, 1));
    const uint8_t truncated[] = {0xB8, 1, 2, 3};
    CHECK(maxps4::validate_guest_program(truncated, sizeof(truncated), 3) == -ENOTSUP);
    const uint8_t syscall[] = {0x0F, 0x05, 0xF4};
    CHECK(maxps4::validate_guest_program(syscall, sizeof(syscall), 3) == -ENOTSUP);
    auto multiple = guest_test::elf(123, 7, true);
    auto overlap = multiple; put(overlap,0x1001,136,8);
    CHECK(image.load(overlap.data(), overlap.size(), 3) == -ENOEXEC);
    auto far = multiple; put(far,0x10000000,136,8);
    CHECK(image.load(far.data(), far.size(), 3) == -E2BIG);
    CHECK(image.load(multiple.data(), multiple.size(), 3) == 0);
    CHECK(image.entry() == 0x1000 && image.segments().size() == 2);
    CHECK(image.entry_pointer()[1] == 123);
    CHECK(image.translate(0x1000, 11, 5)); CHECK(!image.translate(0x1000, 1, 2));
    CHECK(!image.translate(0x10000, 1, 1)); CHECK(!image.translate(0xFFFF, 2, 4));
    CHECK(!image.translate(0x1000, 33, 4)); CHECK(!image.translate(UINT64_MAX, 2, 4));
    CHECK(!image.translate(0x1000, SIZE_MAX, 4));
    CHECK(image.translate(0x100B, 21, 4)[0] == 0);
    auto* data = const_cast<uint8_t*>(image.translate(0x10000, 64, 6));
    CHECK(data && data[0] == 0xAB && data[10] == 0);
    data[0] = 99; CHECK(image.entry_pointer()[1] == 123);
    // Kernel protection, not just the translation permission check.
    const pid_t child = ::fork(); CHECK(child >= 0);
    if (child == 0) {
        ::signal(SIGSEGV, SIG_DFL); ::signal(SIGBUS, SIG_DFL);
        *const_cast<volatile uint8_t*>(image.entry_pointer()) = 0;
        _exit(0);
    }
    int status = 0; CHECK(::waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
    // Guest data never becomes executable host code, including writable pages.
#if defined(__aarch64__) || defined(__arm64__)
    data[0] = 0xC0; data[1] = 0x03; data[2] = 0x5F; data[3] = 0xD6; // ARM64 RET
#elif defined(__x86_64__)
    data[0] = 0xC3; // x86-64 RET
#else
#error Unsupported host architecture for NX test
#endif
    const pid_t nx_child = ::fork(); CHECK(nx_child >= 0);
    if (nx_child == 0) {
        ::signal(SIGSEGV, SIG_DFL); ::signal(SIGBUS, SIG_DFL);
        reinterpret_cast<void(*)()>(data)();
        _exit(0);
    }
    CHECK(::waitpid(nx_child, &status, 0) == nx_child);
    CHECK(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
    const auto address = reinterpret_cast<uintptr_t>(image.entry_pointer());
    const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    image.clear();
    CHECK(!image.entry_pointer() && !image.translate(0x10000, 1, 4) && image.segments().empty());
#if defined(__APPLE__)
    char resident = 0;
#else
    unsigned char resident = 0;
#endif
    CHECK(::mincore(reinterpret_cast<void*>(address / page * page), page,
        &resident) == -1 && errno == ENOMEM);
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(image.load(file.data(), file.size(), 3) == 0);
        CHECK(image.entry_pointer()[1] == 40);
        image.clear();
    }
    std::puts("PASS ELF64 malformed inputs, CPU admission/budget, real guest mappings R/W/X policy and cleanup; no FEX JIT executed");
}
