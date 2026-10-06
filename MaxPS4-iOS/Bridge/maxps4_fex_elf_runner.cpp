// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/fex/fex_guest_engine.h"
#include "core/guest_cpu/fex_guest_cpu.h"

#include <FEXCore/Core/X86Enums.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <variant>
#include <vector>

namespace {
thread_local std::string g_run_diag = "not run";

static uint16_t U16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
static uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
static uint64_t U64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}
static uint64_t AlignDown(uint64_t v, uint64_t a) { return v & ~(a - 1); }
static uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

struct HostMapping {
    void* ptr{MAP_FAILED};
    size_t size{};
    explicit HostMapping(size_t n) : size(n) {
        ptr = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    ~HostMapping() { if (ptr != MAP_FAILED) munmap(ptr, size); }
    bool ok() const { return ptr != MAP_FAILED; }
};

class HomebrewBridge final : public AetherPS4::Fex::GuestBridge {
public:
    AetherPS4::Fex::EngineResult<bool> Invoke(Core::GuestCpu::HleCallFrame& frame) override {
        const uint64_t op = frame.operation;
        auto& gpr = frame.gpr;
        // Minimal, non-proprietary test-only I/O shim for the GPL hello_stdio sample.
        // FreeBSD/Orbis-style syscall numbers: read=3, write=4.
        if (op == 4) {
            const int fd = static_cast<int>(gpr[FEXCore::X86State::REG_RDI]);
            const auto addr = static_cast<uintptr_t>(gpr[FEXCore::X86State::REG_RSI]);
            const size_t len = static_cast<size_t>(gpr[FEXCore::X86State::REG_RDX]);
            if ((fd == 1 || fd == 2) && IsReadable(addr, len)) {
                FILE* out = fd == 1 ? stdout : stderr;
                const size_t done = fwrite(reinterpret_cast<const void*>(addr), 1, len, out);
                fflush(out);
                gpr[FEXCore::X86State::REG_RAX] = done;
                return true;
            }
            return AetherPS4::Fex::EngineFailure{AetherPS4::Fex::EngineStage::Bridge, EFAULT};
        }
        if (op == 3) {
            // EOF for stdin: enough for the legal hello sample to continue without interactive input.
            gpr[FEXCore::X86State::REG_RAX] = 0;
            return true;
        }
        last_syscall = op;
        return AetherPS4::Fex::EngineFailure{AetherPS4::Fex::EngineStage::Bridge, ENOSYS};
    }

    void SetImage(uintptr_t begin, size_t size) { image_begin = begin; image_size = size; }
    uint64_t LastSyscall() const { return last_syscall; }

private:
    bool IsReadable(uintptr_t addr, size_t size) const {
        if (size == 0) return true;
        if (addr < image_begin || addr > UINTPTR_MAX - size) return false;
        return addr + size <= image_begin + image_size;
    }
    uintptr_t image_begin{};
    size_t image_size{};
    uint64_t last_syscall{};
};
} // namespace

extern "C" const char* maxps4_fex_guest_run_last_error(void) {
    return g_run_diag.c_str();
}

extern "C" int maxps4_fex_guest_run_elf(const char* path) {
    g_run_diag = "handoff start";
    if (!path) { g_run_diag = "ELF path missing"; return 20; }

    FILE* f = fopen(path, "rb");
    if (!f) { g_run_diag = "ELF open failed"; return 21; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); g_run_diag = "ELF seek failed"; return 22; }
    const long end = ftell(f);
    if (end < 64 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); g_run_diag = "ELF too short"; return 23; }
    std::vector<uint8_t> file(static_cast<size_t>(end));
    if (fread(file.data(), 1, file.size(), f) != file.size()) {
        fclose(f); g_run_diag = "ELF read failed"; return 24;
    }
    fclose(f);

    const auto* eh = file.data();
    if (eh[0] != 0x7f || eh[1] != 'E' || eh[2] != 'L' || eh[3] != 'F' ||
        eh[4] != 2 || eh[5] != 1 || U16(eh + 18) != 62) {
        g_run_diag = "runner rejected non-x86_64 ELF";
        return 25;
    }

    const uint64_t entry = U64(eh + 24);
    const uint64_t phoff = U64(eh + 32);
    const uint16_t phentsize = U16(eh + 54);
    const uint16_t phnum = U16(eh + 56);
    if (phentsize < 56 || phnum == 0 || phoff > file.size() ||
        static_cast<uint64_t>(phnum) * phentsize > file.size() - phoff) {
        g_run_diag = "invalid program-header table";
        return 26;
    }

    uint64_t min_vaddr = UINT64_MAX, max_vaddr = 0;
    for (uint16_t i = 0; i < phnum; ++i) {
        const uint8_t* ph = eh + phoff + static_cast<uint64_t>(i) * phentsize;
        if (U32(ph) != 1) continue; // PT_LOAD
        const uint64_t vaddr = U64(ph + 16), memsz = U64(ph + 40);
        if (memsz == 0 || vaddr > UINT64_MAX - memsz) continue;
        min_vaddr = std::min(min_vaddr, vaddr);
        max_vaddr = std::max(max_vaddr, vaddr + memsz);
    }
    if (min_vaddr == UINT64_MAX || max_vaddr <= min_vaddr) {
        g_run_diag = "no loadable ELF segments";
        return 27;
    }

    const uint64_t page = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    const uint64_t min_page = AlignDown(min_vaddr, page);
    const uint64_t max_page = AlignUp(max_vaddr, page);
    if (max_page <= min_page || max_page - min_page > 256ULL * 1024ULL * 1024ULL) {
        g_run_diag = "ELF image span refused";
        return 28;
    }

    HostMapping image(static_cast<size_t>(max_page - min_page));
    HostMapping stack(static_cast<size_t>(std::max<uint64_t>(page * 16, 256 * 1024)));
    if (!image.ok() || !stack.ok()) { g_run_diag = "guest image/stack mmap failed"; return 29; }

    const size_t image_page_count = image.size / static_cast<size_t>(page);
    std::vector<uint32_t> page_flags(image_page_count, 0);

    for (uint16_t i = 0; i < phnum; ++i) {
        const uint8_t* ph = eh + phoff + static_cast<uint64_t>(i) * phentsize;
        if (U32(ph) != 1) continue;
        const uint64_t off = U64(ph + 8), vaddr = U64(ph + 16);
        const uint64_t filesz = U64(ph + 32), memsz = U64(ph + 40);
        if (filesz > memsz || off > file.size() || filesz > file.size() - off ||
            vaddr < min_page || vaddr - min_page > image.size ||
            memsz > image.size - static_cast<size_t>(vaddr - min_page)) {
            g_run_diag = "PT_LOAD bounds invalid";
            return 30;
        }
        memcpy(static_cast<uint8_t*>(image.ptr) + (vaddr - min_page),
               file.data() + off, static_cast<size_t>(filesz));

        const uint32_t flags = U32(ph + 4);
        const uint64_t seg_page_begin = AlignDown(vaddr, page);
        const uint64_t seg_page_end = AlignUp(vaddr + memsz, page);
        for (uint64_t va = seg_page_begin; va < seg_page_end; va += page) {
            const size_t page_index = static_cast<size_t>((va - min_page) / page);
            if (page_index >= page_flags.size()) {
                g_run_diag = "PT_LOAD page index invalid";
                return 31;
            }
            page_flags[page_index] |= flags;
        }
    }

    if (entry < min_page || entry >= max_page) { g_run_diag = "entry outside image"; return 32; }

    const uintptr_t image_begin = reinterpret_cast<uintptr_t>(image.ptr);
    std::vector<Core::GuestExecutionRange> image_ranges;
    bool entry_is_executable = false;

    for (size_t first = 0; first < page_flags.size();) {
        const uint32_t flags = page_flags[first];
        size_t last = first + 1;
        while (last < page_flags.size() && page_flags[last] == flags) ++last;

        const uintptr_t begin = image_begin + first * static_cast<size_t>(page);
        const size_t length = (last - first) * static_cast<size_t>(page);

        if (flags == 0) {
            if (mprotect(reinterpret_cast<void*>(begin), length, PROT_NONE) != 0) {
                g_run_diag = "mprotect(PROT_NONE) failed";
                return 33;
            }
            first = last;
            continue;
        }

        const bool executable = (flags & 0x1u) != 0; // PF_X
        const bool writable = (flags & 0x2u) != 0;   // PF_W
        if (executable && writable) {
            g_run_diag = "ELF host page merges writable+executable PT_LOAD segments";
            return 34;
        }

        // FEX translates guest x86-64 code into its own JIT cache. Guest code must
        // therefore be host-readable but sealed against host writes; guest data stays RW.
        int host_prot = PROT_READ | (writable ? PROT_WRITE : 0);
        if (mprotect(reinterpret_cast<void*>(begin), length, host_prot) != 0) {
            char buf[128];
            snprintf(buf, sizeof(buf), "mprotect ELF segment failed errno=%d", errno);
            g_run_diag = buf;
            return 35;
        }

        image_ranges.push_back({begin, length, executable, writable});

        const uintptr_t translated_entry =
            image_begin + static_cast<uintptr_t>(entry - min_page);
        if (executable && translated_entry >= begin && translated_entry < begin + length) {
            entry_is_executable = true;
        }
        first = last;
    }

    if (!entry_is_executable) {
        g_run_diag = "ELF entry is not inside an executable PT_LOAD range";
        return 36;
    }

    const uintptr_t guest_entry = image_begin + static_cast<uintptr_t>(entry - min_page);
    const uintptr_t stack_begin = reinterpret_cast<uintptr_t>(stack.ptr);
    const uintptr_t stack_top = (stack_begin + stack.size - 32) & ~static_cast<uintptr_t>(0xF);

    HomebrewBridge bridge;
    bridge.SetImage(image_begin, image.size);
    auto backendResult = Core::FexGuestCpuBackend::Create(bridge);
    if (const auto* failure = std::get_if<Core::GuestExecutionFailure>(&backendResult)) {
        char buf[160]; snprintf(buf, sizeof(buf), "FEX create failed stage=%d errno=%d",
                                static_cast<int>(failure->Stage), failure->Error);
        g_run_diag = buf; return 32;
    }
    auto backend = std::move(std::get<std::unique_ptr<Core::FexGuestCpuBackend>>(backendResult));

    Core::GuestExecutionRequest request;
    request.Rip = guest_entry;
    request.Rsp = stack_top;
    request.Rflags = 1U << 1;
    request.MappedRanges = image_ranges;
    request.MappedRanges.push_back({stack_begin, stack.size, false, true});

    auto result = backend->Run(request);
    if (const auto* failure = std::get_if<Core::GuestExecutionFailure>(&result)) {
        char buf[220];
        snprintf(buf, sizeof(buf),
                 "FEX handoff reached guest • stopped stage=%d errno=%d syscall=%llu",
                 static_cast<int>(failure->Stage), failure->Error,
                 static_cast<unsigned long long>(bridge.LastSyscall()));
        g_run_diag = buf;
        return 33;
    }

    const auto& state = std::get<Core::GuestExecutionState>(result);
    char buf[220];
    snprintf(buf, sizeof(buf),
             "FEX guest executed • stop=%d first=0x%llx last=0x%llx",
             static_cast<int>(state.StopReason),
             static_cast<unsigned long long>(state.FirstRip),
             static_cast<unsigned long long>(state.LastRip));
    g_run_diag = buf;
    return 0;
}
