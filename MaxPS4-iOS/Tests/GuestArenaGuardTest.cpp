#include "../Native/maxps4_guest_elf.h"
#include "PS4ExecutableFixture.h"
#include "PS4OrbisExecutionFixture.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

// Probe the kernel in a child process. Reset sanitizer signal handlers so an
// intentional protection fault cannot be mistaken for a successful read.
static void faults(uintptr_t address, bool write) {
    const pid_t child = ::fork(); CHECK(child >= 0);
    if (child == 0) {
        ::signal(SIGSEGV, SIG_DFL); ::signal(SIGBUS, SIG_DFL);
        auto* byte = reinterpret_cast<volatile uint8_t*>(address);
        if (write) *byte = 1;
        else { const volatile uint8_t observed = *byte; (void)observed; }
        _exit(0);
    }
    int status = 0;
    CHECK(::waitpid(child, &status, 0) == child);
    CHECK(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
}
static void unmapped(uintptr_t address, size_t page) {
#if defined(__APPLE__)
    (void)page;
    mach_vm_address_t next = address;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    const auto status = ::mach_vm_region(mach_task_self(), &next, &size,
        VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info), &count, &object);
    if (object != MACH_PORT_NULL) ::mach_port_deallocate(mach_task_self(), object);
    CHECK(status == KERN_INVALID_ADDRESS || (status == KERN_SUCCESS && next > address));
#else
    unsigned char resident = 0;
    CHECK(::mincore(reinterpret_cast<void*>(address), page, &resident) == -1 && errno == ENOMEM);
#endif
}
static void verify(maxps4::GuestELF& image) {
    const long host_page = ::sysconf(_SC_PAGESIZE); CHECK(host_page > 0);
    const size_t page = static_cast<size_t>(host_page);
    uint64_t begin = UINT64_MAX, end = 0;
    for (const auto& segment : image.segments()) {
        begin = std::min(begin, segment.address / page * page);
        end = std::max(end, (segment.address + segment.memory_size + page - 1) / page * page);
    }
    CHECK(begin < end);
    const auto& first = image.segments().front();
    const auto* pointer = image.translate(first.address, 1, 4); CHECK(pointer);
    const uintptr_t arena = reinterpret_cast<uintptr_t>(pointer) - (first.address - begin);
    const uintptr_t lower = arena - page, upper = arena + (end - begin);
    // Both edges, reads and writes, not just logical permission queries.
    faults(arena - 1, false); faults(lower, true);
    faults(upper, false); faults(upper + page - 1, true);
    CHECK(!image.translate(begin ? begin - 1 : UINT64_MAX, 1, 4));
    CHECK(!image.translate(end, 1, 4));
    // Check a hole between segment pages stays inaccessible after guard insertion.
    for (uint64_t address = begin; address < end; address += page) {
        bool occupied = false;
        for (const auto& segment : image.segments()) {
            const uint64_t low = segment.address / page * page;
            const uint64_t high = (segment.address + segment.memory_size + page - 1) / page * page;
            occupied |= address >= low && address < high;
        }
        if (!occupied) {
            CHECK(!image.translate(address, 1, 4));
            faults(arena + (address - begin), false);
            break;
        }
    }
    image.clear();
    CHECK(image.segments().empty() && !image.entry_pointer());
    unmapped(lower, page); unmapped(arena, page); unmapped(upper, page);
    image.clear(); // Idempotent reservation cleanup.
}
int main() {
    maxps4::GuestELF image;
    const auto elf = guest_test::elf(40, 2, true);
    CHECK(image.load(elf.data(), elf.size(), 3) == 0); verify(image);
    const auto sce = ps4ELF();
    CHECK(image.stage_ps4(sce.data(), sce.size()) == 0); verify(image);
    const auto self = plainSELF();
    CHECK(image.stage_ps4(self.data(), self.size()) == 0); verify(image);
    maxps4::OrbisLinker::Image linked;
    std::string reason;
    CHECK(maxps4::OrbisLinker::link({orbis_execution::input("app.elf", orbis_execution::make(false), false),
        orbis_execution::input("provider.prx", orbis_execution::make(true), true)}, {}, linked, reason));
    CHECK(image.prepare_orbis(linked, "app.elf", 6, reason) == 0); verify(image);
    // A failed replacement must release the whole old reservation as well.
    CHECK(image.load(elf.data(), elf.size(), 3) == 0);
    const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    const uintptr_t arena = reinterpret_cast<uintptr_t>(image.entry_pointer()) / page * page;
    CHECK(image.load(nullptr, 0, 3) == -EINVAL);
    unmapped(arena - page, page); unmapped(arena, page);
    for (unsigned i = 0; i < 64; ++i) {
        CHECK(image.load(elf.data(), elf.size(), 3) == 0);
        CHECK(image.entry_pointer()[1] == 40);
        image.clear();
    }
    std::puts("PASS kernel guard faults and full reservation cleanup: ELF, PS4 ELF/SELF, linked Orbis; no CPU execution claimed");
}
