// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/fex/fex_guest_engine.h"
#include "core/guest_cpu/fex_guest_cpu.h"

#include <FEXCore/Core/X86Enums.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <variant>
#include <vector>

namespace {
thread_local std::string g_run_diag = "not run";
std::mutex g_live_diag_mutex;
std::string g_live_diag = "not run";

void SetLiveDiag(const std::string& value) {
    std::lock_guard<std::mutex> lock(g_live_diag_mutex);
    g_live_diag = value;
}

std::string GetLiveDiag() {
    std::lock_guard<std::mutex> lock(g_live_diag_mutex);
    return g_live_diag;
}

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
    bool ExitRequested() const { return exit_requested; }
    int ExitCode() const { return exit_code; }
    AetherPS4::Fex::EngineResult<bool> Invoke(Core::GuestCpu::HleCallFrame& frame) override {
        const uint64_t op = frame.operation;
        auto& gpr = frame.gpr;
        RecordSyscall(op);

        // Small, non-proprietary compatibility shim used only by legal homebrew tests.
        // FreeBSD/Orbis-style syscall numbers used here: exit=1, read=3, write=4.
        if (op == 1) {
            exit_requested = true;
            exit_code = static_cast<int>(gpr[FEXCore::X86State::REG_RDI]);
            SetLiveDiag(std::string("guest requested exit • code=") + std::to_string(exit_code) +
                        " • trace=" + Trace());
            return AetherPS4::Fex::EngineFailure{AetherPS4::Fex::EngineStage::Bridge, ECANCELED};
        }
        // PS4-specific dynlib_dlsym=591 is diagnosed explicitly so the next
        // missing HLE symbol can be identified without guessing.
        if (op == 591) {
            const uint64_t handle = gpr[FEXCore::X86State::REG_RDI];
            const auto symbol_addr = static_cast<uintptr_t>(gpr[FEXCore::X86State::REG_RSI]);
            const auto out_addr = static_cast<uintptr_t>(gpr[FEXCore::X86State::REG_RDX]);
            const std::string symbol = ReadCString(symbol_addr, 160);
            RecordDlsym(handle, symbol);
            SetLiveDiag(std::string("guest running • syscall=591 dynlib_dlsym") +
                        " • handle=" + std::to_string(handle) +
                        " • symbol=" + (symbol.empty() ? "<unreadable>" : symbol) +
                        " • out=0x" + Hex(out_addr) +
                        " • dlsym=" + DlsymTrace() +
                        " • trace=" + Trace());

            if (symbol == "_exit" && exit_veneer != 0 && IsWritable(out_addr, sizeof(uintptr_t))) {
                std::memcpy(reinterpret_cast<void*>(out_addr), &exit_veneer, sizeof(exit_veneer));
                gpr[FEXCore::X86State::REG_RAX] = 0;
                return true;
            }

            last_syscall = op;
            return AetherPS4::Fex::EngineFailure{AetherPS4::Fex::EngineStage::Bridge, ENOSYS};
        }

        if (op == 0x100000001ULL) {
            exit_requested = true;
            exit_code = static_cast<int>(gpr[FEXCore::X86State::REG_RDI]);
            SetLiveDiag(std::string("guest called HLE _exit • code=") + std::to_string(exit_code) +
                        " • dlsym=" + DlsymTrace() +
                        " • trace=" + Trace());
            return AetherPS4::Fex::EngineFailure{AetherPS4::Fex::EngineStage::Bridge, ECANCELED};
        }
        if (op == 4) {
            const int fd = static_cast<int>(gpr[FEXCore::X86State::REG_RDI]);
            const auto addr = static_cast<uintptr_t>(gpr[FEXCore::X86State::REG_RSI]);
            const size_t len = static_cast<size_t>(gpr[FEXCore::X86State::REG_RDX]);
            if ((fd == 1 || fd == 2) && IsReadable(addr, len)) {
                FILE* out = fd == 1 ? stdout : stderr;
                const size_t done = fwrite(reinterpret_cast<const void*>(addr), 1, len, out);
                fflush(out);
                Capture(reinterpret_cast<const char*>(addr), done);
                gpr[FEXCore::X86State::REG_RAX] = done;
                return true;
            }
            return AetherPS4::Fex::EngineFailure{AetherPS4::Fex::EngineStage::Bridge, EFAULT};
        }

        if (op == 3) {
            const int fd = static_cast<int>(gpr[FEXCore::X86State::REG_RDI]);
            const auto addr = static_cast<uintptr_t>(gpr[FEXCore::X86State::REG_RSI]);
            const size_t len = static_cast<size_t>(gpr[FEXCore::X86State::REG_RDX]);
            if (fd != 0 || !IsWritable(addr, len)) {
                return AetherPS4::Fex::EngineFailure{AetherPS4::Fex::EngineStage::Bridge, EFAULT};
            }
            // Deterministic stdin for legal stdio homebrew: one short line, then EOF.
            static constexpr char kInput[] = "MaxPS4\n";
            const size_t available = stdin_sent ? 0 : sizeof(kInput) - 1;
            const size_t done = std::min(len, available);
            if (done != 0) {
                memcpy(reinterpret_cast<void*>(addr), kInput, done);
                stdin_sent = true;
            }
            gpr[FEXCore::X86State::REG_RAX] = done;
            return true;
        }

        // Harmless identity/query calls commonly reached by tiny libc startup paths.
        if (op == 20) { // getpid
            gpr[FEXCore::X86State::REG_RAX] = 1;
            return true;
        }
        if (op == 24 || op == 25 || op == 43 || op == 47) { // getuid/geteuid/getegid/getgid
            gpr[FEXCore::X86State::REG_RAX] = 0;
            return true;
        }

        last_syscall = op;
        return AetherPS4::Fex::EngineFailure{AetherPS4::Fex::EngineStage::Bridge, ENOSYS};
    }

    void SetImage(uintptr_t begin, size_t size) { image_begin = begin; image_size = size; }
    void SetStack(uintptr_t begin, size_t size) { stack_begin = begin; stack_size = size; }
    void SetWritableRanges(std::vector<std::pair<uintptr_t, size_t>> ranges) {
        writable_ranges = std::move(ranges);
    }
    void SetExitVeneer(uintptr_t address) { exit_veneer = address; }
    uint64_t LastSyscall() const { return last_syscall; }
    const std::string& Output() const { return output; }

    std::string Trace() const {
        std::string result;
        const size_t count = std::min(trace_count, trace.size());
        const size_t first = trace_count > trace.size() ? trace_count % trace.size() : 0;
        for (size_t i = 0; i < count; ++i) {
            const size_t index = (first + i) % trace.size();
            if (!result.empty()) result += ",";
            result += std::to_string(trace[index]);
        }
        return result;
    }

    std::string DlsymTrace() const {
        std::string result;
        for (const auto& item : dlsym_requests) {
            if (!result.empty()) result += ",";
            result += item;
        }
        return result;
    }

private:
    static std::string Hex(uintptr_t value) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llx",
                      static_cast<unsigned long long>(value));
        return buf;
    }

    std::string ReadCString(uintptr_t addr, size_t limit) const {
        if (addr == 0 || limit == 0) return {};
        std::string result;
        result.reserve(std::min<size_t>(limit, 64));
        for (size_t i = 0; i < limit; ++i) {
            if (!IsReadable(addr + i, 1)) return {};
            const char c = *reinterpret_cast<const char*>(addr + i);
            if (c == '\0') return result;
            if (static_cast<unsigned char>(c) < 0x20 ||
                static_cast<unsigned char>(c) > 0x7e) {
                result.push_back('?');
            } else {
                result.push_back(c);
            }
        }
        return result + "…";
    }

    bool Contains(uintptr_t begin, size_t span, uintptr_t addr, size_t size) const {
        if (size == 0) return true;
        if (begin == 0 || addr < begin || addr > UINTPTR_MAX - size) return false;
        const uintptr_t end = begin + span;
        return end >= begin && addr + size <= end;
    }

    bool IsReadable(uintptr_t addr, size_t size) const {
        return Contains(image_begin, image_size, addr, size) ||
               Contains(stack_begin, stack_size, addr, size);
    }

    bool IsWritable(uintptr_t addr, size_t size) const {
        if (Contains(stack_begin, stack_size, addr, size)) return true;
        for (const auto& range : writable_ranges) {
            if (Contains(range.first, range.second, addr, size)) return true;
        }
        return false;
    }

    void RecordDlsym(uint64_t handle, const std::string& symbol) {
        std::string item = std::to_string(handle) + ":" +
                           (symbol.empty() ? "<unreadable>" : symbol);
        if (dlsym_requests.size() == 8) dlsym_requests.erase(dlsym_requests.begin());
        dlsym_requests.push_back(std::move(item));
    }

    void Capture(const char* data, size_t size) {
        const size_t remaining = output.size() < 1024 ? 1024 - output.size() : 0;
        if (remaining != 0 && data != nullptr) output.append(data, std::min(size, remaining));
    }

    void RecordSyscall(uint64_t op) {
        trace[trace_count % trace.size()] = op;
        ++trace_count;
        SetLiveDiag(std::string("guest running • syscall=") + std::to_string(op) +
                    " • trace=" + Trace() +
                    (output.empty() ? "" : " • output=" + output));
    }

    uintptr_t image_begin{};
    size_t image_size{};
    uintptr_t stack_begin{};
    size_t stack_size{};
    uintptr_t exit_veneer{};
    std::vector<std::pair<uintptr_t, size_t>> writable_ranges;
    std::vector<std::string> dlsym_requests;
    uint64_t last_syscall{};
    bool stdin_sent{};
    bool exit_requested{};
    int exit_code{};
    std::string output;
    std::array<uint64_t, 12> trace{};
    size_t trace_count{};
};} // namespace

extern "C" const char* maxps4_fex_guest_run_last_error(void) {
    return g_run_diag.c_str();
}

extern "C" void maxps4_fex_guest_run_live_diagnostic(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    const std::string value = GetLiveDiag();
    std::snprintf(out, out_size, "%s", value.c_str());
}

extern "C" int maxps4_fex_guest_run_elf(const char* path) {
    g_run_diag = "handoff start";
    SetLiveDiag("handoff start");
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
    std::vector<std::pair<uintptr_t, size_t>> writable_ranges;
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
        if (writable) writable_ranges.emplace_back(begin, length);

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

    HostMapping veneers(static_cast<size_t>(page));
    if (!veneers.ok()) { g_run_diag = "HLE veneer mmap failed"; return 37; }
    constexpr uint64_t kExitOperation = 0x100000001ULL;
    uint8_t exit_stub[16] = {
        0x49, 0x89, 0xca,             // mov r10, rcx
        0x48, 0xb8,                   // mov rax, imm64
        0,0,0,0,0,0,0,0,
        0x0f, 0x05,                   // syscall
        0xc3                          // ret
    };
    std::memcpy(exit_stub + 5, &kExitOperation, sizeof(kExitOperation));
    std::memcpy(veneers.ptr, exit_stub, sizeof(exit_stub));
    __builtin___clear_cache(reinterpret_cast<char*>(veneers.ptr),
                            reinterpret_cast<char*>(veneers.ptr) + sizeof(exit_stub));
    if (mprotect(veneers.ptr, veneers.size, PROT_READ) != 0) {
        g_run_diag = "HLE veneer mprotect failed";
        return 38;
    }
    const uintptr_t exit_veneer = reinterpret_cast<uintptr_t>(veneers.ptr);

    HomebrewBridge bridge;
    bridge.SetImage(image_begin, image.size);
    bridge.SetStack(stack_begin, stack.size);
    bridge.SetWritableRanges(writable_ranges);
    bridge.SetExitVeneer(exit_veneer);
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
    request.MappedRanges.push_back({exit_veneer, veneers.size, true, false});

    {
        char live[220];
        std::snprintf(live, sizeof(live),
                      "guest entered • entry=0x%llx • rsp=0x%llx • waiting for syscall/HLT",
                      static_cast<unsigned long long>(guest_entry),
                      static_cast<unsigned long long>(stack_top));
        SetLiveDiag(live);
    }
    auto result = backend->Run(request);
    if (const auto* failure = std::get_if<Core::GuestExecutionFailure>(&result)) {
        if (bridge.ExitRequested()) {
            char exitbuf[420];
            const auto& captured = bridge.Output();
            const auto trace = bridge.Trace();
            snprintf(exitbuf, sizeof(exitbuf),
                     "FEX guest exited • code=%d%s%s%s%s • dlsym=%s",
                     bridge.ExitCode(),
                     captured.empty() ? "" : " • output=",
                     captured.empty() ? "" : captured.c_str(),
                     trace.empty() ? "" : " • trace=",
                     trace.empty() ? "" : trace.c_str(),
                     bridge.DlsymTrace().c_str());
            g_run_diag = exitbuf;
            SetLiveDiag(g_run_diag);
            return 0;
        }
        char buf[420];
        const auto& captured = bridge.Output();
        const auto trace = bridge.Trace();
        snprintf(buf, sizeof(buf),
                 "FEX handoff reached guest • stopped stage=%d errno=%d syscall=%llu%s%s%s%s",
                 static_cast<int>(failure->Stage), failure->Error,
                 static_cast<unsigned long long>(bridge.LastSyscall()),
                 captured.empty() ? "" : " • output=",
                 captured.empty() ? "" : captured.c_str(),
                 trace.empty() ? "" : " • trace=",
                 trace.empty() ? "" : trace.c_str());
        g_run_diag = buf;
        SetLiveDiag(g_run_diag);
        return 33;
    }

    const auto& state = std::get<Core::GuestExecutionState>(result);
    char buf[420];
    const auto& captured = bridge.Output();
    const auto trace = bridge.Trace();
    snprintf(buf, sizeof(buf),
             "FEX guest executed • stop=%d first=0x%llx last=0x%llx%s%s%s%s",
             static_cast<int>(state.StopReason),
             static_cast<unsigned long long>(state.FirstRip),
             static_cast<unsigned long long>(state.LastRip),
             captured.empty() ? "" : " • output=",
             captured.empty() ? "" : captured.c_str(),
             trace.empty() ? "" : " • trace=",
             trace.empty() ? "" : trace.c_str());
    g_run_diag = buf;
    SetLiveDiag(g_run_diag);
    return 0;
}
