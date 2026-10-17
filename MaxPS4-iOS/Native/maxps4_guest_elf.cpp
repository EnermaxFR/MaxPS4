#include "maxps4_guest_elf.h"
#include "maxps4_guest_range.h"
#include "maxps4_ps4_guest_memory.h"
#include "PS4ExecutableImage.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <functional>
#include <set>
#include <new>
#include <sys/mman.h>
#include <unistd.h>

namespace maxps4 {
namespace {
uint64_t number(const uint8_t* p, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) value |= uint64_t(p[i]) << (8 * i);
    return value;
}
constexpr uint64_t limit = 32 * 1024 * 1024;
}
static int validateGuestProgram(const uint8_t* bytes, size_t length, uint32_t budget,
    uint64_t guestAddress, const std::vector<GuestELF::Segment>* regions) {
    if (!bytes || !length || length > 65536 || !budget || budget > 1000000) return -EINVAL;
    bool initialized = false;
    uint32_t instructions = 0;
    for (size_t offset = 0; offset < length;) {
        if (++instructions > budget) return -E2BIG;
        const uint8_t op = bytes[offset++];
        if (op == 0xB8 || op == 0x05) {
            if (length - offset < 4 || (op == 0x05 && !initialized)) return -ENOTSUP;
            initialized = true;
            offset += 4;
        } else if (op == 0xC7) {
            // Exact x86-64 MOV dword ptr [RIP+disp32],imm32 (C7 /0, ModRM 05).
            // Admit only a complete 4-byte store to a declared RW, non-X segment.
            // Address proof happens before mapping/execution, never by host dereference.
            if (!regions || length - offset < 9 || bytes[offset] != 0x05 ||
                guestAddress > UINT64_MAX - (offset + 9)) return -ENOTSUP;
            const uint64_t raw = number(bytes + offset + 1, 4);
            const int64_t displacement = raw >= 0x80000000ULL ?
                static_cast<int64_t>(raw) - 0x100000000LL : static_cast<int64_t>(raw);
            const uint64_t next = guestAddress + offset + 9;
            uint64_t target = 0;
            if (displacement < 0) {
                const uint64_t distance = static_cast<uint64_t>(-displacement);
                if (next < distance) return -ENOTSUP;
                target = next - distance;
            } else {
                if (next > UINT64_MAX - static_cast<uint64_t>(displacement)) return -ENOTSUP;
                target = next + static_cast<uint64_t>(displacement);
            }
            bool writable = false;
            for (const auto& region : *regions) {
                if ((region.permissions & 7U) != 6U) continue;
                writable |= guest_range_permitted(target, 4, 2, region.address,
                                                  region.memory_size, region.permissions);
            }
            if (!writable) return -ENOTSUP;
            offset += 9;
        } else if (op == 0x90) {
            // NOP has no architectural side effect.
        } else if (op == 0xF4) {
            return initialized && offset == length ? 0 : -ENOTSUP;
        } else return -ENOTSUP;
    }
    return -ENOTSUP;
}
int validate_guest_program(const uint8_t* bytes, size_t length, uint32_t budget) {
    // Without an ELF address-space proof, every memory operand remains rejected.
    return validateGuestProgram(bytes, length, budget, 0, nullptr);
}
GuestELF::~GuestELF() { clear(); }
void GuestELF::clear() {
    if (reservation) ::munmap(reservation, arena_size);
    reservation = nullptr;
    arena = nullptr;
    arena_size = 0;
    guest_base = guest_entry = 0;
    blockers = 0;
    regions.clear();
}
const uint8_t* GuestELF::translate(uint64_t address, size_t length, uint32_t permissions) const {
    if (!arena) return nullptr;
    for (const auto& segment : regions) {
        if (guest_range_permitted(address, length, permissions, segment.address,
                                  segment.memory_size, segment.permissions))
            return static_cast<const uint8_t*>(arena) + (address - guest_base);
    }
    return nullptr;
}
int GuestELF::load(const uint8_t* file, size_t length, uint32_t budget) {
    clear();
    if (!file || length < 64 || length > limit || !budget || budget > 1000000) return -EINVAL;
    if (std::memcmp(file, "\177ELF\2\1\1", 7) || number(file + 16, 2) != 2 ||
        number(file + 18, 2) != 62 || number(file + 20, 4) != 1 ||
        number(file + 52, 2) != 64 || number(file + 54, 2) != 56 ||
        number(file + 40, 8) || number(file + 60, 2)) return -ENOEXEC;
    const uint64_t table = number(file + 32, 8), count = number(file + 56, 2);
    if (table < 64 || table > length || !count || count > 64 || count > (length - table) / 56) return -ENOEXEC;
    const long host_page = ::sysconf(_SC_PAGESIZE);
    if (host_page <= 0) return -EIO;
    const uint64_t page = static_cast<uint64_t>(host_page);
    const uint64_t entry = number(file + 24, 8);
    uint64_t begin = UINT64_MAX, end = 0;
    size_t code_offset = 0, code_size = 0;
    std::vector<Segment> parsed;
    try {
        for (uint64_t i = 0; i < count; ++i) {
            const uint8_t* ph = file + table + i * 56;
            // Reject dynamic linking, interpreters, TLS and all unhandled metadata.
            if (number(ph, 4) != 1) return -ENOEXEC;
            Segment s {number(ph + 16, 8), number(ph + 8, 8), number(ph + 32, 8),
                       number(ph + 40, 8), static_cast<uint32_t>(number(ph + 4, 4))};
            const uint64_t align = number(ph + 48, 8);
            if (!s.memory_size || s.memory_size > limit || s.file_size > s.memory_size ||
                s.file_offset > length || s.file_size > length - s.file_offset ||
                s.address > UINT64_MAX - s.memory_size ||
                s.address + s.memory_size > UINT64_MAX - (page - 1) ||
                !(s.permissions & 4) || (s.permissions & ~7U) || (s.permissions & 3) == 3 ||
                (align > 1 && ((align & (align - 1)) || s.address % align != s.file_offset % align))) return -ENOEXEC;
            const uint64_t low = s.address / page * page;
            const uint64_t high = (s.address + s.memory_size + page - 1) / page * page;
            // Host pages must not alias two independently protected guest regions.
            for (const auto& old : parsed) {
                const uint64_t old_low = old.address / page * page;
                const uint64_t old_high = (old.address + old.memory_size + page - 1) / page * page;
                if (low < old_high && old_low < high) return -ENOEXEC;
            }
            begin = std::min(begin, low);
            end = std::max(end, high);
            if (end - begin > limit) return -E2BIG;
            if ((s.permissions & 1) && entry >= s.address && entry - s.address < s.file_size) {
                code_offset = static_cast<size_t>(s.file_offset + entry - s.address);
                code_size = static_cast<size_t>(s.file_size - (entry - s.address));
            }
            parsed.push_back(s);
        }
    } catch (const std::bad_alloc&) { return -ENOMEM; }
    if (!code_size) return -ENOEXEC;
    const int admitted = validateGuestProgram(file + code_offset, code_size, budget, entry, &parsed);
    if (admitted) return admitted;
    std::vector<const uint8_t*> sources;
    try {
        for (const auto& segment : parsed) sources.push_back(file + segment.file_offset);
    } catch (const std::bad_alloc&) { return -ENOMEM; }
    return map_segments(std::move(parsed), sources, entry);
}
int GuestELF::interpret(uint32_t budget, Execution& out) {
    out = {};
    if (!arena || blockers || !budget || budget > 1000000) return -EINVAL;
    out.rip = guest_entry;
    bool initialized = false;
    PS4GuestMemory memory(*this);
    while (out.instructions < budget) {
        uint8_t op = 0;
        if (!memory.fetch(out.rip, &op, 1)) return -EFAULT;
        const size_t width = (op == 0xB8 || op == 0x05) ? 5 : op == 0xC7 ? 10 : 1;
        uint8_t code[10] = {};
        if (out.rip > UINT64_MAX - width || !memory.fetch(out.rip, code, width)) return -EFAULT;
        const uint64_t next = out.rip + width;
        if (op == 0xB8 || op == 0x05) {
            if (op == 0x05 && !initialized) return -ENOTSUP;
            const uint32_t immediate = static_cast<uint32_t>(number(code + 1, 4));
            out.rax = op == 0xB8 ? immediate : static_cast<uint32_t>(out.rax + immediate);
            initialized = true;
        } else if (op == 0xC7) {
            if (code[1] != 0x05) return -ENOTSUP;
            const uint64_t raw = number(code + 2, 4);
            const int64_t displacement = raw >= 0x80000000ULL ?
                static_cast<int64_t>(raw) - 0x100000000LL : static_cast<int64_t>(raw);
            if ((displacement < 0 && next < static_cast<uint64_t>(-displacement)) ||
                (displacement >= 0 && next > UINT64_MAX - static_cast<uint64_t>(displacement))) return -EFAULT;
            const uint64_t target = displacement < 0 ? next - static_cast<uint64_t>(-displacement) :
                next + static_cast<uint64_t>(displacement);
            if (!memory.write(target, code + 6, 4)) return -EFAULT;
        } else if (op != 0x90 && op != 0xF4) return -ENOTSUP;
        ++out.instructions;
        out.rip = next;
        if (op == 0xF4) return initialized ? 0 : -ENOTSUP;
    }
    return -E2BIG;
}
int GuestELF::prepare_orbis(const OrbisLinker::Image& linked, const std::string& root,
    uint32_t budget, std::string& diagnostic) {
    clear(); diagnostic.clear();
    auto fail = [&](int error, const char* message) {
        clear(); diagnostic = message; return error;
    };
    if (!budget || budget > 65536) return fail(-EINVAL, "invalid Orbis CPU budget");
    try {
        // Patch only our private copy. Linker's logical relocations stay intact.
        auto copy = linked;
        uint64_t entry = 0;
        for (const auto& module : copy.modules) {
            if (module.image.requiresTLS) return fail(-ENOTSUP, "Orbis TLS execution unavailable");
            if (module.path == root) entry = module.entry;
        }
        if (!entry) return fail(-ENOEXEC, "root has no process entry");
        auto fetch = [&](uint64_t address, size_t count) -> uint8_t* {
            for (auto& module : copy.modules) for (auto& segment : module.image.segments) {
                const uint64_t begin = module.bias + segment.header.address;
                if ((segment.header.flags & 5) == 5 && address >= begin &&
                    address - begin <= segment.header.fileSize &&
                    count <= segment.header.fileSize - (address - begin))
                    return segment.bytes.data() + address - begin;
            }
            return nullptr;
        };
        struct Patch { uint8_t* code; uint32_t displacement; };
        std::vector<Patch> patches;
        std::set<uint64_t> active;
        std::map<uint64_t,size_t> instruction_ranges;
        bool initialized = false;
        bool edx_initialized = false, ebx_initialized = false, esi_initialized = false, ecx_initialized = false, compared = false;
        uint32_t remaining = budget;
        uint64_t refusal_pc = entry;
        uint8_t refusal_opcode = 0;
        std::function<int(uint64_t,bool,size_t)> admit = [&](uint64_t pc, bool function, size_t depth) -> int {
            if (depth > 32 || !active.insert(pc).second) return -ELOOP;
            const uint64_t start = pc;
            struct Erase { std::set<uint64_t>& active; uint64_t start; ~Erase() { active.erase(start); } } erase{active,start};
            while (remaining) {
                --remaining;
                refusal_pc = pc; refusal_opcode = 0;
                auto* code = fetch(pc,1);
                if (!code) return -EFAULT;
                const uint8_t op = code[0];
                refusal_opcode = op;
                size_t width = op == 0xb8 || op == 0xb9 || op == 0xba || op == 0xbb || op == 0x05 || op == 0x25 || op == 0x0d ? 5 :
                    op == 0xff || op == 0x8b || op == 0x81 ? 6 :
                    op == 0xc7 ? 10 : op == 0xc1 || op == 0x0f ? 3 :
                    op == 0x01 || op == 0x29 || op == 0x39 || op == 0x89 ? 2 : 1;
                if (!fetch(pc,width)) return -EFAULT;
                if (op == 0x89 && code[1] != 0xc3 && code[1] != 0xc6) {
                    width = 6;
                    if (!fetch(pc,width)) return -EFAULT;
                }
                // A CALL target may not enter another instruction's operand.
                // Otherwise rewriting one decoded stream could invalidate a
                // different stream that was admitted over overlapping bytes.
                const auto next = instruction_ranges.lower_bound(pc);
                if (next != instruction_ranges.end() && next->first == pc) {
                    if (next->second != width) return -ENOTSUP;
                } else {
                    if (next != instruction_ranges.end() && next->first < pc+width) return -ENOTSUP;
                    if (next != instruction_ranges.begin()) {
                        const auto previous = std::prev(next);
                        if (previous->first+previous->second > pc) return -ENOTSUP;
                    }
                    instruction_ranges.emplace_hint(next,pc,width);
                }
                if (op == 0xb8 || op == 0x05) {
                    width = 5;
                    if (!fetch(pc,width) || (op == 0x05 && !initialized)) return -ENOTSUP;
                    initialized = true;
                    if (op == 0x05) compared = false;
                } else if (op == 0xb9) {
                    ecx_initialized = true;
                } else if (op == 0xba || op == 0xbb) {
                    if (op == 0xba) edx_initialized = true; else ebx_initialized = true;
                } else if (op == 0x89 && code[1] == 0xc6) {
                    if (!initialized) return -ENOTSUP;
                    esi_initialized = true;
                } else if (op == 0x89 && code[1] == 0xc3) {
                    if (!initialized) return -ENOTSUP;
                    ebx_initialized = true; // MOV EBX,EAX
                } else if (op == 0x8b || op == 0x89) {
                    // Only EAX/EDX RIP-relative operands, never register pointers.
                    const bool eax = code[1] == 0x05, edx = code[1] == 0x15;
                    if (!eax && !edx) return -ENOTSUP;
                    const uint64_t target = static_cast<uint64_t>(static_cast<int64_t>(pc+6) +
                        static_cast<int32_t>(number(code+2,4)));
                    if (op == 0x8b) {
                        if (!copy.translate(target,4,4) || copy.translate(target,4,1)) return -EFAULT;
                        if (eax) initialized = true; else edx_initialized = true;
                    } else {
                        if (!(eax ? initialized : edx_initialized)) return -ENOTSUP;
                        if (!copy.translate(target,4,2) || copy.translate(target,4,1)) return -EFAULT;
                    }
                } else if (op == 0x25 || op == 0x0d) {
                    if (!initialized) return -ENOTSUP;
                    compared = false;
                } else if (op == 0xc1) {
                    // SHL/SHR EAX or SHL EDX, with bounded immediate count.
                    if (code[2] > 31 || (code[1] != 0xe0 && code[1] != 0xe8 && code[1] != 0xe2)) return -ENOTSUP;
                    if (!(code[1] == 0xe2 ? edx_initialized : initialized)) return -ENOTSUP;
                    compared = false;
                } else if (op == 0x01 || op == 0x29) {
                    if (code[1] != 0xc2 || !initialized || !edx_initialized) return -ENOTSUP;
                    compared = false; // ADD/SUB EDX,EAX
                } else if (op == 0x39) {
                    if (code[1] != 0xd6 || !esi_initialized || !edx_initialized) return -ENOTSUP;
                    compared = true;
                } else if (op == 0x81) {
                    if (code[1] == 0xfe) {
                        if (!esi_initialized) return -ENOTSUP;
                    } else if (!edx_initialized || (code[1] != 0xfa && code[1] != 0xe2)) return -ENOTSUP;
                    compared = code[1] == 0xfa || code[1] == 0xfe; // CMP or AND EDX,imm32
                } else if (op == 0x0f) {
                    if (code[1] != 0x44 || !compared || !initialized ||
                        !((code[2] == 0xc3 && ebx_initialized) || (code[2] == 0xc1 && ecx_initialized) ||
                          (code[2] == 0xcb && ecx_initialized && ebx_initialized))) return -ENOTSUP;
                    // CMOVE EAX,EBX; MOV does not alter the comparison flags.
                } else if (op == 0xff) {
                    width = 6;
                    if (!fetch(pc,width) || code[1] != 0x15) return -ENOTSUP;
                    const int64_t disp = static_cast<int32_t>(number(code+2,4));
                    const uint64_t slot = static_cast<uint64_t>(static_cast<int64_t>(pc+6)+disp);
                    const auto* pointer = copy.translate(slot,8,4);
                    if (!pointer) return -EFAULT;
                    const uint64_t target = number(pointer,8);
                    if (target >= (uint64_t{1}<<47) || pc >= (uint64_t{1}<<47)) return -EFAULT;
                    const int64_t relative = static_cast<int64_t>(target)-static_cast<int64_t>(pc+5);
                    if (relative < INT32_MIN || relative > INT32_MAX) return -EOVERFLOW;
                    const int error = admit(target,true,depth+1);
                    if (error) return error;
                    patches.push_back({code,static_cast<uint32_t>(relative)});
                } else if (op == 0xc7) {
                    width = 10;
                    if (!fetch(pc,width) || code[1] != 5) return -ENOTSUP;
                    const int64_t disp = static_cast<int32_t>(number(code+2,4));
                    const uint64_t target = static_cast<uint64_t>(static_cast<int64_t>(pc+10)+disp);
                    if (!copy.translate(target,4,2) || copy.translate(target,4,1)) return -EFAULT;
                } else if (op == 0xc3 || op == 0xf4) {
                    if (!initialized || (function ? op != 0xc3 : op != 0xf4)) return -ENOTSUP;
                    return 0;
                } else if (op != 0x90) return -ENOTSUP;
                pc += width;
            }
            return -E2BIG;
        };
        const int admitted = admit(entry,false,0);
        if (admitted) {
            const char* cause = admitted == -EFAULT ? "invalid guest memory range" :
                admitted == -ELOOP ? "recursive call or depth limit" :
                admitted == -E2BIG ? "instruction admission budget exhausted" :
                admitted == -EOVERFLOW ? "CALL displacement overflow" : "unsupported instruction or termination";
            std::ostringstream detail;
            detail << "Orbis CPU admission refused: " << cause << "; guest PC=0x" << std::hex
                   << refusal_pc << "; opcode=0x" << unsigned(refusal_opcode);
            clear(); diagnostic = detail.str(); return admitted;
        }
        for (const auto& patch : patches) {
            patch.code[0] = 0xe8;
            for (size_t i=0;i<4;++i) patch.code[1+i] = uint8_t(patch.displacement >> (8*i));
            patch.code[5] = 0x90;
        }
        std::vector<Segment> parsed;
        std::vector<const uint8_t*> sources;
        for (const auto& module : copy.modules) for (const auto& segment : module.image.segments) {
            const auto& h = segment.header;
            // Copy relocated BSS as well as file bytes; retain file extent for CPU queries.
            parsed.push_back({module.bias+h.address,h.offset,h.memorySize,h.memorySize,h.flags});
            sources.push_back(segment.bytes.data());
        }
        const int mapped = map_segments(std::move(parsed),sources,entry);
        if (mapped) return fail(mapped,"Orbis page mapping refused (page overlap, span or protection)");
        size_t i=0;
        for (const auto& module : copy.modules) for (const auto& segment : module.image.segments)
            regions[i++].file_size = segment.header.fileSize;
        blockers = 4; // Restricted rebasing never establishes general PS4 addressing.
        return 0;
    } catch (const std::bad_alloc&) { clear(); return -ENOMEM; }
}
int GuestELF::stage_ps4(const uint8_t* file, size_t length) {
    clear();
    PS4ExecutableImage image;
    const int error = image.load(file, length);
    if (error) return error;
    // A shared library has no process entry and needs separate linker ownership.
    if (image.type == 0xfe18) return -ENOTSUP;
    try {
        std::vector<Segment> parsed;
        std::vector<const uint8_t*> sources;
        for (const auto& segment : image.segments) {
            const auto& h = segment.header;
            parsed.push_back({h.address, h.offset, h.fileSize, h.memorySize, h.flags});
            sources.push_back(segment.bytes.data());
        }
        const int mapped = map_segments(std::move(parsed), sources, image.entry);
        if (mapped) return mapped;
        // Staging cannot authorize CPU dispatch: rebased absolute addresses,
        // arbitrary instructions and PS4 ABI/services remain unresolved.
        blockers = 4U | (image.requiresLinker ? 1U : 0U) | (image.requiresTLS ? 2U : 0U);
        return 0;
    } catch (const std::bad_alloc&) { clear(); return -ENOMEM; }
}
int GuestELF::admit_staged_cpu(uint32_t budget) const {
    if (!budget || budget > 1000000) return -EINVAL;
    if (!arena) return -EFAULT;
    if (blockers & 3U) return -ENOTSUP; // Linker/TLS are never bypassed.
    for (const auto& s : regions) {
        if (!(s.permissions & 1U) || guest_entry < s.address ||
            guest_entry - s.address >= s.file_size) continue;
        const size_t bytes = static_cast<size_t>(s.file_size - (guest_entry - s.address));
        const auto* code = translate(guest_entry, bytes, 5);
        if (!code) return -EFAULT;
        // No absolute operands, branches or stack accesses: rebasing preserves
        // every admitted RIP-relative store, with a full RW/non-X range proof.
        return validateGuestProgram(code, bytes, budget, guest_entry, &regions);
    }
    return -EFAULT;
}
int GuestELF::map_segments(std::vector<Segment> parsed,
    const std::vector<const uint8_t*>& sources, uint64_t entry) {
    const long host_page = ::sysconf(_SC_PAGESIZE);
    if (host_page <= 0) return -EIO;
    const uint64_t page = static_cast<uint64_t>(host_page);
    uint64_t begin = UINT64_MAX, end = 0;
    for (size_t i = 0; i < parsed.size(); ++i) {
        const auto& s = parsed[i];
        if ((s.permissions & 3U) == 3U || s.address > UINT64_MAX - s.memory_size ||
            s.address + s.memory_size > UINT64_MAX - (page - 1)) return -ENOEXEC;
        const uint64_t low = s.address / page * page;
        const uint64_t high = (s.address + s.memory_size + page - 1) / page * page;
        for (size_t j = 0; j < i; ++j) {
            const auto& old = parsed[j];
            const uint64_t old_low = old.address / page * page;
            const uint64_t old_high = (old.address + old.memory_size + page - 1) / page * page;
            if (low < old_high && old_low < high) return -ENOEXEC;
        }
        begin = std::min(begin, low); end = std::max(end, high);
        if (end - begin > limit) return -E2BIG;
    }
    if (parsed.empty() || sources.size() != parsed.size()) return -ENOEXEC;
    // The guards belong to the same reservation as the arena, so adjacent
    // allocations cannot turn an out-of-range CPU access into valid host data.
    // Holes between segments remain PROT_NONE as before.
    if (page > SIZE_MAX / 2 || end - begin > SIZE_MAX - 2 * page) return -E2BIG;
    arena_size = static_cast<size_t>(end - begin + 2 * page);
    void* mapped = ::mmap(nullptr, arena_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) { arena_size = 0; return -errno; }
    reservation = mapped;
    arena = static_cast<uint8_t*>(mapped) + page;
    guest_base = begin;
    guest_entry = entry;
    regions = std::move(parsed);
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& s = regions[i];
        const size_t low = static_cast<size_t>(s.address / page * page - begin);
        const size_t bytes = static_cast<size_t>((s.address + s.memory_size + page - 1) / page * page - begin - low);
        auto* destination = static_cast<uint8_t*>(arena) + low;
        if (::mprotect(destination, bytes, PROT_READ | PROT_WRITE) != 0) {
            const int error = -errno; clear(); return error;
        }
        std::memcpy(static_cast<uint8_t*>(arena) + (s.address - begin), sources[i], s.file_size);
        const int protection = PROT_READ | ((s.permissions & 2) ? PROT_WRITE : 0);
        if (::mprotect(destination, bytes, protection) != 0) {
            const int error = -errno; clear(); return error;
        }
    }
    return 0;
}
}





