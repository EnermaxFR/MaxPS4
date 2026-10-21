#pragma once
#include <cstdio>
#include "OrbisV4HostServices.hpp"
#include "OrbisV4TLSRuntime.hpp"
#include "OrbisV4GraphicsServices.hpp"
#include "OrbisV4PngDecService.hpp"
#include "OrbisV4AudioOutService.hpp"
#include "maxps4_ps4_abi.h"
#include "maxps4_ps4_libkernel.h"
#include <cctype>
#include <ctime>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>

namespace maxps4 {
// Mission 50: PS4 system functions implemented on the host for the service
// stubs. Each handler documents its contract; anything not listed here stays
// a TRAP (diagnostic stop). No handler returns success without performing the
// documented effect. Error conventions: sce* functions return SCE kernel
// errors (0x80020000 | FreeBSD errno), POSIX functions return the errno.
struct OrbisV4SystemServices {
    using Registers = OrbisV4HostServices::Registers;
    using Memory = OrbisV4HostServices::Memory;
    using Result = OrbisV4HostServices::Result;
    using Outcome = OrbisV4HostServices::Outcome;
    // Guest kernel objects. libc dereferences ScePthreadMutex (it ORs
    // PMUTEX_FLAG_PRIVATE=0x100 into +0x20 after scePthreadMutexInit, libc
    // +0x5bbea), so objects are real guest memory laid out like FreeBSD libthr:
    //   pthread_mutex: +0x00 struct umutex (m_owner u32, ...), +0x20 int m_flags
    //   (type in low byte), +0x24 int m_count. pthread_mutex_attr: +0 type, +4 protocol.
    // The host keeps the authoritative owner/depth; guest fields mirror them.
    static constexpr uint64_t mutexSize = 0x80, attrSize = 0x20;
    struct MutexAttr { int type = 1; int protocol = 0; };
    struct Mutex { int type = 1; uint64_t owner = 0; uint32_t depth = 0; std::string name; };
    struct State {
        std::shared_ptr<OrbisV4GraphicsServices> graphics;
        uint64_t heapApi = 0;   // Table registered by libc (rtld keeps the pointer only).
        uint64_t thread = 1;    // Running guest thread id (single guest thread so far).
        uint64_t arena = 0, arenaEnd = 0, next = 0;
        std::map<uint64_t, std::vector<uint64_t>> freed; // size -> addresses
        std::map<uint64_t, MutexAttr> attrs; std::map<uint64_t, Mutex> mutexes;
        // Process memory mapped by kernel services (flexible/direct), above all modules.
        struct Mapping { uint64_t size; uint32_t permissions; std::string kind, name; };
        uint64_t processNext = 0x200000000ULL;
        std::map<uint64_t, Mapping> mappings;
        uint64_t flexibleUsed = 0;
        struct Direct { uint64_t size = 0, mapped = 0; int type = 0; };
        std::map<uint64_t, Direct> direct;
        // Threads: guest objects (zeroed, opaque to us) addressed by ScePthread;
        // attributes are host-authoritative, guest objects mirror nothing yet.
        struct ThreadAttr { uint64_t stackSize = 0x40000, affinity = 0x3f; int detach = 0, inherit = 4, policy = 1, priority = 700; };
        std::map<uint64_t, ThreadAttr> threadAttrs;
        std::map<uint64_t, ThreadAttr> threads; // ScePthread -> its attributes
        uint64_t mainThread = 0;
        // Scheduler-visible thread records (ScePthread guest object address).
        struct Guest {
            uint64_t entry = 0, arg = 0, fs = 0, stackBase = 0, stackSize = 0, rsp = 0;
            OrbisV4TLSRuntime::Thread tls; std::string name;
            bool exited = false, detached = false, joined = false; uint64_t exitValue = 0;
        };
        std::map<uint64_t, Guest> guests;
        std::vector<uint64_t> spawned; // created, FEX thread not yet attached by the runner
        uint64_t gate = 0;             // TLS runtime return gate (HLT) used as outer return address
        // Virtual controller (MaxPS4): one DualShock-class pad for the local user.
        // Host input (touch/keyboard) writes `pad`; a neutral state is "connected,
        // no button pressed, sticks centred".
        struct Pad { uint32_t buttons = 0; uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80, l2 = 0, r2 = 0; };
        Pad pad; bool padInitialized = false; int padHandle = 0;
        bool splashHidden = false; // sceSystemServiceHideSplashScreen was called
        std::shared_ptr<OrbisV4PngDecService::State> png;
        std::shared_ptr<OrbisV4AudioOutService::State> audio;
        // Local trophies: definitions counted from the title's TRP directory
        // (TROPnnn.PNG entries, no decryption); unlocks kept by MaxPS4.
        uint32_t trophyCount = 0; uint32_t trophyFlags[4] = {}; std::vector<std::string> trophyLog;
        int nextTrophyContext = 1, nextTrophyHandle = 1;
        std::map<int, bool> trophyContexts, trophyHandles; // registered flag / live
        // Condition variables: guest object (zeroed) + host waiter queue.
        struct Waiter { uint64_t thread; bool signaled = false; };
        struct Cond { std::vector<std::shared_ptr<Waiter>> waiters; std::string name; };
        std::map<uint64_t, Cond> conds;
        uint64_t startNs = 0;                  // process start (monotonic)
        std::string appRoot;
        // Files: the existing PS4Libkernel (read-only /app0, symlink-safe walk,
        // explicit stat encoding) over the current lease.
        std::unique_ptr<PS4GuestMemory> files; std::unique_ptr<PS4Libkernel> kernel;
        std::vector<std::string> stdoutLog;
        std::map<uint64_t, uint64_t> errnoSlot; // thread -> guest int errno
    };
    static constexpr uint64_t condSize = 0x40;
    static constexpr ps4::Error timedOut = ps4::Error(60); // FreeBSD ETIMEDOUT
    // MaxPS4 platform configuration (not values read from a console): six
    // usable CPUs for the title, 256 KiB default thread stack.
    static constexpr uint64_t threadAttrSize = 0x40, threadObjectSize = 0x400;
    static constexpr uint64_t ps4Page = 0x4000;
    static constexpr uint64_t flexibleLimit = 448ULL * 1024 * 1024; // MaxPS4 budget for flexible memory.
    static constexpr uint64_t directLimit = 2560ULL * 1024 * 1024; // Backable MaxPS4 physical arena, not PS4 hardware capacity.
    // x86 page tables have no write-only data page: the CPU write bit also
    // allows reads. Keep this PS4 mapping rule out of generic ELF permissions.
    static bool directPermissions(uint64_t prot,uint32_t& out) {
        if(prot & ~uint64_t{0x33})return false;
        uint64_t cpu=prot&3;if(cpu&2)cpu|=1;
        return guestPermissions(cpu,out);
    }
    // PS4 SceKernelProt: CPU read/write/exec = 0x1/0x2/0x4 (GPU bits refused here).
    static bool guestPermissions(uint64_t prot, uint32_t& out) {
        if (prot & ~uint64_t{7}) return false;
        out = ((prot & 1) ? 4U : 0U) | ((prot & 2) ? 2U : 0U) | ((prot & 4) ? 1U : 0U);
        return out && (out & 3U) != 3U && (!(out & 1U) || (out & 4U));
    }
    static Result ok(uint64_t v = 0) { return {Outcome::Returned, v, {}}; }
    static Result sce(ps4::Error e) { return {Outcome::Returned, uint64_t(ps4::kernel_error(e)), {}}; }
    static Result posix(ps4::Error e) { return {Outcome::Returned, uint64_t(e), {}}; }
    static Result trap(const std::string& why) { return {Outcome::Trap, 0, why}; }
    static uint64_t allocate(State& s, Memory& m, uint64_t size) {
        uint64_t at = 0;
        auto& list = s.freed[size];
        if (!list.empty()) { at = list.back(); list.pop_back(); }
        else if (s.next && size <= s.arenaEnd - s.next) { at = s.next; s.next += size; }
        if (!at) return 0;
        std::vector<uint8_t> zero(size_t(size), 0);
        if (m.write_data(at, zero.data(), zero.size())) return 0;
        return at;
    }
    static void release(State& s, uint64_t at, uint64_t size) { s.freed[size].push_back(at); }
    static void mirror(Memory& m, uint64_t object, const Mutex& mx) {
        const uint32_t owner = uint32_t(mx.owner), count = mx.depth;
        m.write_data(object, &owner, 4); m.write_data(object + 0x24, &count, 4);
    }
    // --- pthread mutex (single running guest thread; contention traps) ---
    static ps4::Error attrInit(State& s, Memory& m, uint64_t slot) {
        if (!slot) return ps4::inval;
        const uint64_t at = allocate(s, m, attrSize);
        if (!at) return ps4::nomem;
        const int32_t fields[2] = {1, 0};
        if (m.write_data(at, fields, 8) || m.write_data(slot, &at, 8)) { release(s, at, attrSize); return ps4::inval; }
        s.attrs[at] = {};
        return ps4::ok;
    }
    static MutexAttr* attrAt(State& s, Memory& m, uint64_t slot, uint64_t* address = nullptr) {
        uint64_t h = 0; if (!slot || m.read_data(slot, &h, 8)) return nullptr;
        auto it = s.attrs.find(h); if (it == s.attrs.end()) return nullptr;
        if (address) *address = h;
        return &it->second;
    }
    static ps4::Error mutexInit(State& s, Memory& m, uint64_t slot, uint64_t attrSlot, uint64_t name) {
        if (!slot) return ps4::inval;
        Mutex mx;
        if (attrSlot) { const auto* a = attrAt(s, m, attrSlot); if (!a) return ps4::inval; mx.type = a->type; }
        for (size_t i = 0; name && i < 31; ++i) { char c = 0; if (m.read_data(name + i, &c, 1) || !c) break; mx.name += c; }
        const uint64_t at = allocate(s, m, mutexSize);
        if (!at) return ps4::nomem;
        const int32_t flags = mx.type;
        if (m.write_data(at + 0x20, &flags, 4) || m.write_data(slot, &at, 8)) { release(s, at, mutexSize); return ps4::inval; }
        s.mutexes[at] = mx;
        return ps4::ok;
    }
    // FreeBSD libthr static initializers: NULL (normal init) and 1 (adaptive) are
    // initialized on first use; 2 is THR_MUTEX_DESTROYED.
    static Mutex* mutexAt(State& s, Memory& m, uint64_t slot, ps4::Error& error, uint64_t* address = nullptr) {
        uint64_t h = 0;
        if (!slot || m.read_data(slot, &h, 8)) { error = ps4::inval; return nullptr; }
        if (h == 0 || h == 1) {
            if ((error = mutexInit(s, m, slot, 0, 0))) return nullptr;
            const uint64_t adaptive = h;
            m.read_data(slot, &h, 8);
            if (adaptive == 1) { s.mutexes[h].type = 4; const int32_t f = 4; m.write_data(h + 0x20, &f, 4); }
        }
        auto it = s.mutexes.find(h);
        error = it != s.mutexes.end() ? ps4::ok : ps4::inval;
        if (address) *address = h;
        return it != s.mutexes.end() ? &it->second : nullptr;
    }
    static Result lock(State& s, Memory& m, uint64_t slot, bool tryOnly, bool sceCodes) {
        ps4::Error e; uint64_t at = 0; auto* mx = mutexAt(s, m, slot, e, &at);
        auto fail = [&](ps4::Error x) { return sceCodes ? sce(x) : posix(x); };
        if (!mx) return fail(e);
        if (mx->owner && mx->owner != s.thread) {
            if (tryOnly) return fail(ps4::busy);
            // Block until released, then acquire for this thread.
            const uint64_t me = s.thread; State* sp = &s;
            Result r{Outcome::Block, 0, "mutex contended"};
            r.ready = [sp, at] { auto it = sp->mutexes.find(at); return it == sp->mutexes.end() || !it->second.owner; };
            r.wake = [sp, at, me, sceCodes](Memory& mem) -> uint64_t {
                auto it = sp->mutexes.find(at);
                if (it == sp->mutexes.end()) return sceCodes ? uint64_t(ps4::kernel_error(ps4::inval)) : uint64_t(ps4::inval);
                it->second.owner = me; it->second.depth = 1; mirror(mem, at, it->second); return 0;
            };
            return r;
        }
        if (mx->owner == s.thread) {
            if (mx->type == 2) { if (mx->depth == UINT32_MAX) return fail(ps4::again); ++mx->depth; mirror(m, at, *mx); return ok(); }
            if (tryOnly) return fail(ps4::busy);
            if (mx->type == 1) return fail(ps4::deadlk);
            return trap("self-deadlock on a non-errorcheck mutex (would block forever)");
        }
        mx->owner = s.thread; mx->depth = 1; mirror(m, at, *mx); return ok();
    }
    static Result unlock(State& s, Memory& m, uint64_t slot, bool sceCodes) {
        ps4::Error e; uint64_t at = 0; auto* mx = mutexAt(s, m, slot, e, &at);
        auto fail = [&](ps4::Error x) { return sceCodes ? sce(x) : posix(x); };
        if (!mx) return fail(e);
        if (mx->owner != s.thread) return fail(ps4::perm);
        if (--mx->depth == 0) mx->owner = 0;
        mirror(m, at, *mx); return ok();
    }
    static Result destroy(State& s, Memory& m, uint64_t slot, bool sceCodes) {
        uint64_t h = 0;
        auto fail = [&](ps4::Error x) { return sceCodes ? sce(x) : posix(x); };
        if (!slot || m.read_data(slot, &h, 8)) return fail(ps4::inval);
        if (h == 0 || h == 1) return ok(); // Never-used static initializer: nothing to release.
        auto it = s.mutexes.find(h);
        if (it == s.mutexes.end()) return fail(ps4::inval);
        if (it->second.owner) return fail(ps4::busy);
        s.mutexes.erase(it); release(s, h, mutexSize);
        const uint64_t destroyed = 2; m.write_data(slot, &destroyed, 8);
        return ok();
    }

    static uint64_t nowNs() {
        return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    // Reserve [stackSize] of process memory for a thread stack. Called with no
    // lease held (mapping change); returns the base or 0.
    static uint64_t mapStack(State& s, OrbisV4TLSRuntime& runtime, uint64_t size) {
        size = (size + ps4Page - 1) / ps4Page * ps4Page;
        const uint64_t base = s.processNext;
        if (runtime.map_anonymous(base, size, 6)) return 0;
        s.processNext = base + size + 0x10000;
        return base;
    }
    // Main thread record: pthread object, its own 1 MiB stack (TLS runtime keeps
    // only TCB/DTV). Called by the runner before execution, without a lease for
    // the mapping and with a lease for the guest writes.
    static constexpr uint64_t mainStackSize = 1ULL << 20;
    static bool prepareMainStack(State& s, OrbisV4TLSRuntime& runtime) {
        const uint64_t base = mapStack(s, runtime, mainStackSize);
        if (!base) return false;
        s.guests[0].stackBase = base; s.guests[0].stackSize = mainStackSize;
        return true;
    }
    static bool attachMain(State& s, Memory& m, uint64_t fs, uint64_t gate, uint64_t& rsp) {
        s.gate = gate;
        auto record = s.guests[0]; s.guests.erase(0);
        const uint64_t object = allocate(s, m, threadObjectSize);
        if (!object) return false;
        record.fs = fs; record.name = "main";
        rsp = record.stackBase + record.stackSize - 8;
        if (m.write_data(rsp, &gate, 8)) return false;
        record.rsp = rsp;
        s.guests[object] = record; s.mainThread = object; s.threads[object] = {}; s.threads[object].stackSize = mainStackSize;
        s.thread = object;
        return true;
    }
    // --- condition variables (FreeBSD libthr semantics, cooperative scheduler) ---
    // NULL handle = THR_COND_INITIALIZER (initialised on first use). Wait releases
    // the (owned) mutex completely, blocks until signalled (or the timeout for
    // TimedWait, SCE ETIMEDOUT), then re-acquires it with its previous depth.
    static uint64_t condAt(State& s, Memory& m, uint64_t slot) {
        uint64_t h = 0;
        if (!slot || m.read_data(slot, &h, 8)) return 0;
        if (!h) {
            h = allocate(s, m, condSize);
            if (!h || m.write_data(slot, &h, 8)) return 0;
            s.conds[h] = {};
        }
        return s.conds.count(h) ? h : 0;
    }
    static Result condWait(std::shared_ptr<State> st, Memory& m, uint64_t condSlot, uint64_t mutexSlot, uint64_t timeoutUs, bool timed, bool sceCodes) {
        auto fail = [&](ps4::Error x) { return sceCodes ? sce(x) : posix(x); };
        const uint64_t c = condAt(*st, m, condSlot);
        ps4::Error e; uint64_t mx = 0; auto* mutex = mutexAt(*st, m, mutexSlot, e, &mx);
        if (!c || !mutex) return fail(ps4::inval);
        if (mutex->owner != st->thread) return fail(ps4::perm);
        const uint32_t depth = mutex->depth; mutex->owner = 0; mutex->depth = 0; mirror(m, mx, *mutex);
        auto waiter = std::make_shared<State::Waiter>(); waiter->thread = st->thread;
        st->conds[c].waiters.push_back(waiter);
        const uint64_t deadline = timed ? nowNs() + timeoutUs * 1000 : 0, me = st->thread;
        Result b{Outcome::Block, 0, timed ? "cond timedwait" : "cond wait"}; b.deadlineNs = deadline;
        b.ready = [st, mx, waiter, deadline] {
            auto it = st->mutexes.find(mx);
            const bool free = it == st->mutexes.end() || !it->second.owner;
            return free && (waiter->signaled || (deadline && nowNs() >= deadline));
        };
        b.wake = [st, c, mx, waiter, depth, me, sceCodes](Memory& mem) -> uint64_t {
            auto& list = st->conds[c].waiters;
            list.erase(std::remove(list.begin(), list.end(), waiter), list.end());
            auto it = st->mutexes.find(mx);
            if (it != st->mutexes.end()) { it->second.owner = me; it->second.depth = depth; mirror(mem, mx, it->second); }
            if (waiter->signaled) return 0;
            return sceCodes ? uint64_t(ps4::kernel_error(timedOut)) : uint64_t(timedOut);
        };
        return b;
    }
    static void installConditions(OrbisV4HostServices& services, std::shared_ptr<State> st) {
        services.implement("scePthreadCondInit", [st](const Registers& r, Memory& m) {
            if (!r.rdi) return sce(ps4::inval);
            const uint64_t h = allocate(*st, m, condSize); if (!h) return sce(ps4::nomem);
            if (m.write_data(r.rdi, &h, 8)) { release(*st, h, condSize); return sce(ps4::inval); }
            st->conds[h] = {}; return ok(); });
        services.implement("scePthreadCondDestroy", [st](const Registers& r, Memory& m) {
            uint64_t h = 0; if (!r.rdi || m.read_data(r.rdi, &h, 8)) return sce(ps4::inval);
            if (!h) return ok();
            auto it = st->conds.find(h); if (it == st->conds.end()) return sce(ps4::inval);
            if (!it->second.waiters.empty()) return sce(ps4::busy);
            st->conds.erase(it); release(*st, h, condSize); const uint64_t destroyed = 1; m.write_data(r.rdi, &destroyed, 8); return ok(); });
        services.implement("scePthreadCondWait", [st](const Registers& r, Memory& m) { return condWait(st, m, r.rdi, r.rsi, 0, false, true); });
        services.implement("scePthreadCondTimedwait", [st](const Registers& r, Memory& m) { return condWait(st, m, r.rdi, r.rsi, uint32_t(r.rdx), true, true); });
        auto signal = [st](const Registers& r, Memory& m, bool all) {
            const uint64_t c = condAt(*st, m, r.rdi); if (!c) return sce(ps4::inval);
            for (auto& w : st->conds[c].waiters) if (!w->signaled) { w->signaled = true; if (!all) break; }
            return ok(); };
        services.implement("scePthreadCondSignal", [signal](const Registers& r, Memory& m) { return signal(r, m, false); });
        services.implement("scePthreadCondBroadcast", [signal](const Registers& r, Memory& m) { return signal(r, m, true); });
    }
    // --- time (host clocks; FreeBSD clock ids) ---
    static bool clockNs(uint64_t id, uint64_t start, uint64_t& ns) {
        using namespace std::chrono;
        switch (id) {
        case 0: case 9: case 10: // CLOCK_REALTIME(_PRECISE/_FAST)
            ns = uint64_t(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count()); return true;
        case 13: // CLOCK_SECOND: realtime with whole-second resolution (libc time())
            ns = uint64_t(duration_cast<seconds>(system_clock::now().time_since_epoch()).count()) * 1000000000ull; return true;
        case 4: case 11: case 12: case 5: case 7: case 8: // MONOTONIC*, UPTIME*
            ns = nowNs(); return true;
        case 15: case 14: // CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID: elapsed process time
            ns = nowNs() - start; return true;
        default: return false;
        }
    }
    static void installTime(OrbisV4HostServices& services, std::shared_ptr<State> st) {
        st->startNs = nowNs();
        // sceKernelGetProcessTime(void) -> microseconds since process start.
        services.implement("sceKernelGetProcessTime", [st](const Registers&, Memory&) { return ok((nowNs() - st->startNs) / 1000); });
        auto gettime = [st](const Registers& r, Memory& m, bool sceCodes) {
            uint64_t ns = 0;
            auto fail = [&](ps4::Error e) { return sceCodes ? sce(e) : Result{Outcome::Returned, uint64_t(-1), "errno"}; };
            if (!clockNs(r.rdi, st->startNs, ns)) return fail(ps4::inval);
            const int64_t ts[2] = {int64_t(ns / 1000000000), int64_t(ns % 1000000000)};
            if (!r.rsi || m.write_data(r.rsi, ts, sizeof(ts))) return fail(ps4::fault);
            return ok(); };
        services.implement("sceKernelClockGettime", [gettime](const Registers& r, Memory& m) { return gettime(r, m, true); });
        services.implement("clock_gettime", [gettime, st](const Registers& r, Memory& m) {
            auto res = gettime(r, m, false); if (res.value == uint64_t(-1)) setErrno(*st, m, ps4::inval); return res; });
        auto timeofday = [](const Registers& r, Memory& m) {
            using namespace std::chrono;
            const uint64_t us = uint64_t(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
            const int64_t tv[2] = {int64_t(us / 1000000), int64_t(us % 1000000)};
            return r.rdi && !m.write_data(r.rdi, tv, sizeof(tv)); };
        // Host time zone: localtime_r gives the offset including DST.
        // sceKernelConvertUtcToLocaltime(time_t utc, time_t* local, SceKernelTimesec* {t, westsec, dstsec}, time_t* dstsec)
        auto zone = [](int64_t utc, int64_t& west, int64_t& dst) {
            const time_t t = time_t(utc); struct tm local{};
            if (!localtime_r(&t, &local)) return false;
            dst = local.tm_isdst > 0 ? 3600 : 0; west = int64_t(local.tm_gmtoff) - dst; return true; };
        services.implement("sceKernelConvertUtcToLocaltime", [zone](const Registers& r, Memory& m) {
            const int64_t utc = int64_t(r.rdi); int64_t west = 0, dst = 0;
            if (utc < 0 || !zone(utc, west, dst)) return sce(ps4::inval);
            const int64_t local = utc + west + dst;
            if (r.rsi && m.write_data(r.rsi, &local, 8)) return sce(ps4::fault);
            if (r.rdx) { uint8_t tsec[16] = {}; const uint32_t w = uint32_t(int32_t(west)), d = uint32_t(int32_t(dst));
                std::memcpy(tsec, &utc, 8); std::memcpy(tsec + 8, &w, 4); std::memcpy(tsec + 12, &d, 4);
                if (m.write_data(r.rdx, tsec, sizeof(tsec))) return sce(ps4::fault); }
            if (r.rcx && m.write_data(r.rcx, &dst, 8)) return sce(ps4::fault);
            return ok(); });
        // sceKernelConvertLocaltimeToUtc(time_t local, int64_t, time_t* utc, SceKernelTimezone* {minuteswest, dsttime}, int* dstsec)
        services.implement("sceKernelConvertLocaltimeToUtc", [zone](const Registers& r, Memory& m) {
            const int64_t local = int64_t(r.rdi); int64_t west = 0, dst = 0;
            if (local < 0 || !zone(local, west, dst)) return sce(ps4::inval);
            const int64_t utc = local - west - dst;
            if (r.rdx && m.write_data(r.rdx, &utc, 8)) return sce(ps4::fault);
            if (r.rcx) { const int32_t tz[2] = {int32_t(-west / 60), dst ? 1 : 0}; if (m.write_data(r.rcx, tz, sizeof(tz))) return sce(ps4::fault); }
            if (r.r8) { const int32_t d = int32_t(dst); if (m.write_data(r.r8, &d, 4)) return sce(ps4::fault); }
            return ok(); });
        services.implement("sceKernelGettimeofday", [timeofday](const Registers& r, Memory& m) { return timeofday(r, m) ? ok() : sce(ps4::fault); });
        services.implement("gettimeofday", [timeofday](const Registers& r, Memory& m) { return ok(timeofday(r, m) ? 0 : uint64_t(-1)); });
        // nanosleep(const timespec* req, timespec* rem): blocks the thread.
        services.implement("nanosleep", [](const Registers& r, Memory& m) {
            int64_t ts[2] = {}; if (!r.rdi || m.read_data(r.rdi, ts, sizeof(ts)) || ts[0] < 0 || ts[1] < 0 || ts[1] >= 1000000000)
                return ok(uint64_t(-1));
            const uint64_t deadline = nowNs() + uint64_t(ts[0]) * 1000000000ULL + uint64_t(ts[1]), rem = r.rsi;
            Result b{Outcome::Block, 0, "nanosleep"}; b.deadlineNs = deadline;
            b.ready = [deadline] { return nowNs() >= deadline; };
            b.wake = [rem](Memory& mem) -> uint64_t { if (rem) { const int64_t z[2] = {}; mem.write_data(rem, z, sizeof(z)); } return 0; };
            return b; });
        // __error(void) -> int* errno of the calling thread (guest storage).
        services.implement("__error", [st](const Registers&, Memory& m) { return ok(errnoAddress(*st, m)); });
    }
    static uint64_t errnoAddress(State& s, Memory& m) {
        auto it = s.errnoSlot.find(s.thread);
        if (it != s.errnoSlot.end()) return it->second;
        const uint64_t at = allocate(s, m, 16);
        if (at) s.errnoSlot[s.thread] = at;
        return at;
    }
    static void setErrno(State& s, Memory& m, ps4::Error e) {
        const uint64_t at = errnoAddress(s, m); const int32_t v = int32_t(e); if (at) m.write_data(at, &v, 4);
    }
    // --- files (/app0 read-only through PS4Libkernel) ---
    static void installFiles(OrbisV4HostServices& services, std::shared_ptr<State> st, const std::string& app0) {
        auto kernel = [st](Memory& m) -> PS4Libkernel* {
            if (!st->kernel) return nullptr;
            st->files->rebind(m); return st->kernel.get();
        };
        services.implement("__mount_app0", [](const Registers&, Memory&) { return Result{Outcome::Trap, 0, "internal"}; });
        st->appRoot = app0;
        auto bridge = [st, kernel](PS4KernelService service) {
            return [st, kernel, service](const Registers& r, Memory& m) {
                auto* k = kernel(m);
                if (!k) {
                    // Lazily mount on first use: needs a lease-backed adapter.
                    st->files = std::make_unique<PS4GuestMemory>(m);
                    st->kernel = std::make_unique<PS4Libkernel>(*st->files);
                    if (st->appRoot.empty() || st->kernel->mount_app0(st->appRoot.c_str())) return sce(ps4::noent);
                    k = st->kernel.get();
                }
                const PS4KernelCall call{r.rdi, r.rsi, r.rdx, r.rcx, r.r8, r.r9, r.rsp};
                return ok(uint64_t(k->invoke(service, call, 1)));
            };
        };
        services.implement("sceKernelOpen", bridge(PS4KernelService::open));
        services.implement("sceKernelRead", bridge(PS4KernelService::read));
        services.implement("sceKernelClose", bridge(PS4KernelService::close));
        services.implement("sceKernelLseek", bridge(PS4KernelService::lseek));
        services.implement("sceKernelStat", bridge(PS4KernelService::stat));
        services.implement("sceKernelFstat", bridge(PS4KernelService::fstat));
        // sceKernelPread(fd, buf, n, offset): read at offset, file position unchanged.
        auto lseek = bridge(PS4KernelService::lseek); auto read = bridge(PS4KernelService::read);
        services.implement("sceKernelPread", [lseek, read](const Registers& r, Memory& m) {
            Registers q = r; q.rsi = 0; q.rdx = 1; // SEEK_CUR
            const auto current = lseek(q, m).value; if (int64_t(current) < 0) return ok(current);
            q.rsi = r.rcx; q.rdx = 0; const auto moved = lseek(q, m).value; if (int64_t(moved) < 0) return ok(moved);
            Registers rd = r; const auto got = read(rd, m).value;
            q.rsi = current; q.rdx = 0; lseek(q, m);
            return ok(got); });
        // sceKernelWrite: descriptors 1/2 go to the runner log; files are read-only.
        auto write = [st](const Registers& r, Memory& m) {
            if (r.rdi != 1 && r.rdi != 2) return sce(ps4::badf);
            const size_t n = size_t(std::min<uint64_t>(r.rdx, 4096)); std::string text(n, '\0');
            if (n && m.read_data(r.rsi, text.data(), n)) return sce(ps4::fault);
            st->stdoutLog.push_back(text);
            if (n) { std::fwrite(text.data(), 1, n, stdout); std::fflush(stdout); }
            return ok(n); };
        services.implement("sceKernelWrite", write);
        services.implement("_write", [st, write](const Registers& r, Memory& m) {
            auto result = write(r, m);
            if (int64_t(result.value) < 0) {
                setErrno(*st, m, ps4::Error(uint32_t(result.value) & 0xffff));
                result.value = uint64_t(-1);
            }
            return result;
        });
    }
    // --- virtual controller (libScePad) ---
    // Layouts verified against the title's reads: ScePadData buttons@+0,
    // left stick x/y@+4/+5 (centre 0x80), connected@+0x4c, timestamp@+0x50,
    // connectedCount@+0x68 (0x78 bytes); ScePadControllerInformation
    // deadZoneLeft@+8 (0x1c bytes). Error codes from the original provider.
    static void installPad(OrbisV4HostServices& services, std::shared_ptr<State> st) {
        constexpr uint32_t notInitialized = 0x80920005, invalidArg = 0x80920001, invalidHandle = 0x80920003;
        services.implement("scePadInit", [st](const Registers&, Memory&) { st->padInitialized = true; return ok(); });
        // scePadOpen(userId, type, index, const ScePadOpenParam*): standard pad of the local user.
        services.implement("scePadOpen", [st](const Registers& r, Memory&) {
            if (!st->padInitialized) return ok(notInitialized);
            if (int32_t(r.rdi) != OrbisV4LocalServices::localUser || r.rsi != 0 || r.rdx != 0) return ok(invalidArg);
            st->padHandle = 1; return ok(1); });
        services.implement("scePadClose", [st](const Registers& r, Memory&) {
            if (!st->padHandle || int32_t(r.rdi) != st->padHandle) return ok(invalidHandle);
            st->padHandle = 0;
            return ok(); });
        services.implement("scePadGetControllerInformation", [st](const Registers& r, Memory& m) {
            if (!st->padHandle || int32_t(r.rdi) != st->padHandle) return ok(invalidHandle);
            uint8_t info[0x1c] = {}; const float density = 44.86f; std::memcpy(info, &density, 4);
            const uint16_t resolution[2] = {1920, 943}; std::memcpy(info + 4, resolution, 4);
            info[8] = 0; info[9] = 0; info[10] = 0; info[11] = 1; info[12] = 1; // dead zones 0, local, 1 connected, connected
            if (!r.rsi || m.write_data(r.rsi, info, sizeof(info))) return ok(invalidArg);
            return ok(); });
        auto read = [st](const Registers& r, Memory& m) {
            if (!st->padHandle || int32_t(r.rdi) != st->padHandle) return ok(invalidHandle);
            uint8_t data[0x78] = {};
            std::memcpy(data, &st->pad.buttons, 4);
            data[4] = st->pad.lx; data[5] = st->pad.ly; data[6] = st->pad.rx; data[7] = st->pad.ry; data[8] = st->pad.l2; data[9] = st->pad.r2;
            const float identity = 1.0f; std::memcpy(data + 0x18, &identity, 4); // orientation quaternion w
            data[0x4c] = 1; const uint64_t stamp = nowNs() / 1000; std::memcpy(data + 0x50, &stamp, 8); data[0x68] = 1;
            if (!r.rsi || m.write_data(r.rsi, data, sizeof(data))) return ok(invalidArg);
            return ok(); };
        services.implement("scePadReadState", read);
        services.implement("scePadRead", [read](const Registers& r, Memory& m) { // scePadRead(handle, data*, num): one sample
            if (r.rdx < 1) return ok(invalidArg);
            auto res = read(r, m);
            return res.value ? res : ok(1); });
    }
    // --- local trophies (libSceNpTrophy) ---
    static void installTrophies(OrbisV4HostServices& services, std::shared_ptr<State> st, const std::string& app0) {
        constexpr uint32_t invalidArgument = 0x80551604, invalidHandle = 0x80551608, invalidContext = 0x80551609, invalidTrophy = 0x8055160a;
        // The title's trophy set: the first trophyNN.trp present (Kero Blaster 00, Sonic Mania 01).
        for (int n = 0; n < 100 && !st->trophyCount; ++n) {
            char file[32]; std::snprintf(file, sizeof file, "/sce_sys/trophy/trophy%02d.trp", n);
            st->trophyCount = countTrophies(app0 + file);
        }
        services.implement("sceNpTrophyCreateContext", [st](const Registers& r, Memory& m) {
            if (!r.rdi || r.rcx || int32_t(r.rsi) != OrbisV4LocalServices::localUser) return ok(invalidArgument);
            if (!st->trophyCount) return ok(0x80551622); // no trophy set for this title
            const int32_t context = st->nextTrophyContext++;
            if (m.write_data(r.rdi, &context, 4)) return ok(invalidArgument);
            st->trophyContexts[context] = false; return ok(); });
        services.implement("sceNpTrophyDestroyContext", [st](const Registers& r, Memory&) {
            return st->trophyContexts.erase(int32_t(r.rdi)) ? ok() : ok(invalidContext); });
        services.implement("sceNpTrophyCreateHandle", [st](const Registers& r, Memory& m) {
            if (!r.rdi) return ok(invalidArgument);
            const int32_t handle = st->nextTrophyHandle++;
            if (m.write_data(r.rdi, &handle, 4)) return ok(invalidArgument);
            st->trophyHandles[handle] = true; return ok(); });
        services.implement("sceNpTrophyDestroyHandle", [st](const Registers& r, Memory&) {
            return st->trophyHandles.erase(int32_t(r.rdi)) ? ok() : ok(invalidHandle); });
        services.implement("sceNpTrophyAbortHandle", [st](const Registers& r, Memory&) {
            return st->trophyHandles.count(int32_t(r.rdi)) ? ok() : ok(invalidHandle); });
        services.implement("sceNpTrophyRegisterContext", [st](const Registers& r, Memory&) {
            if (r.rdx) return ok(invalidArgument);
            auto c = st->trophyContexts.find(int32_t(r.rdi)); if (c == st->trophyContexts.end()) return ok(invalidContext);
            if (!st->trophyHandles.count(int32_t(r.rsi))) return ok(invalidHandle);
            c->second = true; return ok(); });
        // GetTrophyUnlockState(context, handle, SceNpTrophyFlagArray* (128 bits), uint32_t* count)
        services.implement("sceNpTrophyGetTrophyUnlockState", [st](const Registers& r, Memory& m) {
            if (!r.rdx || !r.rcx) return ok(invalidArgument);
            auto c = st->trophyContexts.find(int32_t(r.rdi)); if (c == st->trophyContexts.end()) return ok(invalidContext);
            if (!st->trophyHandles.count(int32_t(r.rsi))) return ok(invalidHandle);
            if (m.write_data(r.rdx, st->trophyFlags, 16) || m.write_data(r.rcx, &st->trophyCount, 4)) return ok(invalidArgument);
            return ok(); });
        // UnlockTrophy(context, handle, trophyId, int* platinumId): records the unlock.
        // Re-unlocking an unlocked trophy is reported as success (behaviour not verified).
        services.implement("sceNpTrophyUnlockTrophy", [st](const Registers& r, Memory& m) {
            const int32_t id = int32_t(r.rdx);
            auto c = st->trophyContexts.find(int32_t(r.rdi)); if (c == st->trophyContexts.end()) return ok(invalidContext);
            if (!st->trophyHandles.count(int32_t(r.rsi))) return ok(invalidHandle);
            if (id < 0 || uint32_t(id) >= st->trophyCount) return ok(invalidTrophy);
            if (!r.rcx) return ok(invalidArgument);
            const int32_t noPlatinum = -1;
            if (m.write_data(r.rcx, &noPlatinum, 4)) return ok(invalidArgument);
            st->trophyFlags[id / 32] |= 1u << (id % 32); st->trophyLog.push_back("TROPHY.UNLOCK id=" + std::to_string(id));
            return ok(); });
    }
    static uint32_t countTrophies(const std::string& path) {
        std::ifstream f(path, std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
        auto be = [&](size_t o, size_t n) { uint64_t v = 0; for (size_t i = 0; i < n && o + i < b.size(); ++i) v = v << 8 | b[o + i]; return v; };
        if (b.size() < 0x60 || be(0, 4) != 0xdca24d00) return 0;
        const uint64_t count = be(16, 4), size = be(20, 4); uint32_t trophies = 0;
        for (uint64_t i = 0; i < count && size >= 32 && 0x60 + (i + 1) * size <= b.size(); ++i) {
            std::string name(reinterpret_cast<const char*>(&b[0x60 + i * size]), 32); name = name.substr(0, name.find('\0'));
            if (name.size() == 11 && name.rfind("TROP", 0) == 0 && name.substr(7) == ".PNG" &&
                std::isdigit(uint8_t(name[4])) && std::isdigit(uint8_t(name[5])) && std::isdigit(uint8_t(name[6]))) ++trophies;
        }
        return trophies;
    }
    static std::shared_ptr<State> install(OrbisV4HostServices& services, const OrbisV4ProcessBatch& batch,
                        const OrbisLinker::Image& image, OrbisV4TLSRuntime* runtime, const std::string& app0) {
        (void)app0;
        auto st = std::make_shared<State>();
        services.implement("sceKernelIsNeoMode",[](const Registers&,Memory&){return ok(0);}); // Base PS4 profile.
        const uint64_t kernelState = batch.stateBase.at("libkernel.prx");
        st->arena = st->next = kernelState + OrbisV4ProcessBatch::kernelArena;
        st->arenaEnd = kernelState + OrbisV4ProcessBatch::kernelState;
        // sceKernelGetProcParam(void) -> const SceProcParam* of the main executable,
        // i.e. the validated PT_SCE_PROCPARAM view (libc checks size>=0x40 and
        // follows +0x38), or NULL when the process has none.
        uint64_t procParam = 0;
        for (const auto& m : image.modules) if (m.processParam.present) procParam = m.bias + m.processParam.address;
        services.implement("sceKernelGetProcParam", [procParam](const Registers&, Memory&) { return ok(procParam); });
        // _sceKernelRtldSetApplicationHeapAPI(const void* table): libc registers its
        // heap API table; the caller ignores RAX (libc+0x55e9a tests a global).
        // This rtld only records the pointer; it never allocates through it.
        services.implement("_sceKernelRtldSetApplicationHeapAPI", [st](const Registers& r, Memory&) { st->heapApi = r.rdi; return ok(); });
        services.implement("scePthreadMutexattrInit", [st](const Registers& r, Memory& m) { auto e = attrInit(*st, m, r.rdi); return e ? sce(e) : ok(); });
        services.implement("scePthreadMutexattrDestroy", [st](const Registers& r, Memory& m) {
            uint64_t at = 0; if (!attrAt(*st, m, r.rdi, &at)) return sce(ps4::inval);
            st->attrs.erase(at); release(*st, at, attrSize); return ok(); });
        services.implement("scePthreadMutexattrSettype", [st](const Registers& r, Memory& m) {
            uint64_t at = 0; auto* a = attrAt(*st, m, r.rdi, &at); if (!a || r.rsi < 1 || r.rsi > 4) return sce(ps4::inval);
            a->type = int(r.rsi); const int32_t v = a->type; m.write_data(at, &v, 4); return ok(); });
        services.implement("scePthreadMutexattrSetprotocol", [st](const Registers& r, Memory& m) {
            uint64_t at = 0; auto* a = attrAt(*st, m, r.rdi, &at); if (!a || r.rsi > 2) return sce(ps4::inval);
            a->protocol = int(r.rsi); const int32_t v = a->protocol; m.write_data(at + 4, &v, 4); return ok(); });
        services.implement("scePthreadMutexInit", [st](const Registers& r, Memory& m) { auto e = mutexInit(*st, m, r.rdi, r.rsi, r.rdx); return e ? sce(e) : ok(); });
        services.implement("scePthreadMutexLock", [st](const Registers& r, Memory& m) { return lock(*st, m, r.rdi, false, true); });
        services.implement("scePthreadMutexTrylock", [st](const Registers& r, Memory& m) { return lock(*st, m, r.rdi, true, true); });
        services.implement("scePthreadMutexUnlock", [st](const Registers& r, Memory& m) { return unlock(*st, m, r.rdi, true); });
        services.implement("scePthreadMutexDestroy", [st](const Registers& r, Memory& m) { return destroy(*st, m, r.rdi, true); });
        services.implement("pthread_mutex_init", [st](const Registers& r, Memory& m) { auto e = mutexInit(*st, m, r.rdi, r.rsi, 0); return e ? posix(e) : ok(); });
        services.implement("pthread_mutex_lock", [st](const Registers& r, Memory& m) { return lock(*st, m, r.rdi, false, false); });
        services.implement("pthread_mutex_trylock", [st](const Registers& r, Memory& m) { return lock(*st, m, r.rdi, true, false); });
        services.implement("pthread_mutex_unlock", [st](const Registers& r, Memory& m) { return unlock(*st, m, r.rdi, false); });
        // sceLibcHeapGetTraceInfo(info*) (libSceLibcInternalExt). Contract read from
        // libc+0x5bd68..0x5be22: info = {u64 size=0x20; u32 version=1; u32 enabled;
        // u64* wordA; u64* wordB}. libc always stores 0 through wordA, uses wordB only
        // when enabled. MaxPS4 provides no heap tracing: enabled=0, both words are
        // kernel-owned zeroed storage. RAX is ignored by the caller.
        services.implement("sceLibcHeapGetTraceInfo", [st](const Registers& r, Memory& m) {
            uint64_t size = 0; uint32_t version = 0;
            if (m.read_data(r.rdi, &size, 8) || m.read_data(r.rdi + 8, &version, 4) || size != 0x20 || version != 1)
                return trap("sceLibcHeapGetTraceInfo: unexpected info layout");
            const uint64_t words = allocate(*st, m, 16);
            const uint32_t disabled = 0; const uint64_t a = words, b = words + 8;
            if (!words || m.write_data(r.rdi + 0xc, &disabled, 4) || m.write_data(r.rdi + 0x10, &a, 8) || m.write_data(r.rdi + 0x18, &b, 8))
                return trap("sceLibcHeapGetTraceInfo: info not writable");
            return ok();
        });
        // sceKernelMapNamedFlexibleMemory(void** addr, size_t len, int prot, int flags,
        // const char* name): maps zero-filled CPU memory; *addr is an in/out hint
        // (exact with MAP_FIXED=0x10). Deferred: the mapping changes while no guest
        // code runs, then *addr receives the address. Errors are SCE kernel errors.
        services.implement("sceKernelMapNamedFlexibleMemory", [st, runtime](const Registers& r, Memory& m) {
            uint64_t hint = 0; uint32_t permissions = 0;
            if (!runtime) return trap("no process memory owner");
            if (!r.rdi || m.read_data(r.rdi, &hint, 8) || !m.permitted(r.rdi, 8, 2)) return sce(ps4::inval);
            if (!r.rsi || r.rsi % ps4Page || !guestPermissions(r.rdx, permissions)) return sce(ps4::inval);
            if (r.rcx & ~uint64_t{0x10}) return trap("sceKernelMapNamedFlexibleMemory: unsupported flags");
            if (r.rsi > flexibleLimit - st->flexibleUsed) return sce(ps4::nomem);
            uint64_t address = 0;
            if (r.rcx & 0x10) { if (!hint || hint % ps4Page) return sce(ps4::inval); address = hint; }
            else { address = st->processNext; }
            std::string name;
            for (size_t i = 0; r.r8 && i < 31; ++i) { char c = 0; if (m.read_data(r.r8 + i, &c, 1) || !c) break; name += c; }
            const uint64_t out = r.rdi, size = r.rsi;
            Result deferred{Outcome::Deferred, 0, "map flexible memory"};
            deferred.outside = [runtime, address, size, permissions] { return runtime->map_anonymous(address, size, permissions); };
            deferred.complete = [st, out, address, size, permissions, name](Memory& mem, int status) -> uint64_t {
                if (status) return uint64_t(ps4::kernel_error(status == -E2BIG ? ps4::nomem : status == -EEXIST ? ps4::busy : ps4::inval));
                if (mem.write_data(out, &address, 8)) return uint64_t(ps4::kernel_error(ps4::fault));
                st->mappings[address] = {size, permissions, "flexible", name};
                st->flexibleUsed += size;
                if (address + size + 0x10000 > st->processNext) st->processNext = (address + size + 0x10000 + ps4Page - 1) / ps4Page * ps4Page;
                return 0;
            };
            return deferred;
        });
        // Direct physical offsets are allocated separately from guest virtual
        // addresses. A first mapping gets real zero-filled CPU backing through
        // the runtime. Aliases/partial mappings require shared backing support
        // and stop explicitly rather than returning independent fake copies.
        services.implement("sceKernelGetDirectMemorySize", [](const Registers&, Memory&) { return ok(directLimit); });
        services.implement("sceKernelAllocateDirectMemory", [st](const Registers& r, Memory& m) {
            const uint64_t align = r.rcx ? r.rcx : ps4Page, end = std::min(r.rsi, directLimit);
            if (int64_t(r.rdi) < 0 || int64_t(r.rsi) < 0 || !r.rdx || r.rdx % ps4Page ||
                align % ps4Page || (align & (align - 1)) || r.r8 > 10 || !r.r9 || !m.permitted(r.r9, 8, 2)) return sce(ps4::inval);
            if (r.rdi >= end || r.rdx > end - r.rdi) return sce(ps4::again);
            uint64_t at = (r.rdi + align - 1) & ~(align - 1);
            for (const auto& [base, allocation] : st->direct) {
                if (base + allocation.size <= at) continue;
                if (at <= base && r.rdx <= base - at) break;
                at = (base + allocation.size + align - 1) & ~(align - 1);
            }
            if (at > end || r.rdx > end - at) return sce(ps4::again);
            if (m.write_data(r.r9, &at, 8)) return sce(ps4::fault);
            st->direct[at] = {r.rdx, 0, int(r.r8)};
            return ok();
        });
        services.implement("sceKernelMapDirectMemory", [st, runtime](const Registers& r, Memory& m) {
            uint64_t hint = 0; uint32_t permissions = 0;
            if (!r.rdi || m.read_data(r.rdi, &hint, 8) || !m.permitted(r.rdi, 8, 2)) return sce(ps4::fault);
            const uint64_t align = r.r9 ? r.r9 : ps4Page;
            if (!r.rsi || r.rsi % ps4Page || r.r8 % ps4Page || align % ps4Page || (align & (align - 1)) ||
                !directPermissions(r.rdx, permissions)) return sce(ps4::inval);
            if (r.rcx & ~uint64_t{0x10}) return trap("direct mapping: unsupported flags");
            auto allocation = st->direct.find(r.r8);
            if (allocation == st->direct.end() || r.rsi > allocation->second.size) return sce(ps4::inval);
            if (allocation->second.mapped || r.rsi != allocation->second.size) return trap("direct mapping: aliases/partial backing unsupported");
            if (!runtime) return trap("direct mapping: no process memory owner");
            if (hint > UINT64_MAX - (align - 1) || st->processNext > UINT64_MAX - (align - 1)) return sce(ps4::inval);
            const uint64_t address = r.rcx & 0x10 ? hint : (std::max(hint, st->processNext) + align - 1) & ~(align - 1);
            if (!address || address % align || address > UINT64_MAX - r.rsi - 0x10000) return sce(ps4::inval);
            Result result{Outcome::Deferred, 0, "map direct CPU backing (GPU access awaits renderer)"};
            result.outside = [runtime, address, size=r.rsi, permissions] { return runtime->map_anonymous(address, size, permissions); };
            result.complete = [st, address, r, permissions](Memory& mem, int status) -> uint64_t {
                if (status) return uint64_t(ps4::kernel_error(status == -E2BIG || status == -ENOMEM ? ps4::nomem : ps4::inval));
                if (mem.write_data(r.rdi, &address, 8)) return uint64_t(ps4::kernel_error(ps4::fault));
                st->direct.at(r.r8).mapped = address;
                st->mappings[address] = {r.rsi, permissions, "direct", "CPU backing"};
                st->processNext = std::max(st->processNext, address + r.rsi + 0x10000);
                return 0;
            };
            return result;
        });
        services.implement("sceKernelReleaseDirectMemory", [st](const Registers& r, Memory&) {
            const auto it = st->direct.find(r.rdi);
            if (it == st->direct.end() || it->second.size != r.rsi) return sce(ps4::inval);
            if (it->second.mapped) return trap("release mapped direct memory requires unmap first");
            st->direct.erase(it); return ok();
        });
        // Preserve the local provider's real capability refusal. The host
        // runner has no VideoOut presentation device or GNM renderer; no
        // successful handle/frame is fabricated. Routing this through the
        // service boundary exposes the failure preceding title cleanup.
        services.implement("sceVideoOutOpen", [](const Registers&, Memory&) {
            return ok(0x80290018);
        });
        // --- thread attributes / identity ---
        auto attrOf = [st](Memory& m, uint64_t slot) -> State::ThreadAttr* {
            uint64_t h = 0; if (!slot || m.read_data(slot, &h, 8)) return nullptr;
            auto it = st->threadAttrs.find(h); return it == st->threadAttrs.end() ? nullptr : &it->second;
        };
        auto self = [st](Memory&) -> uint64_t { return st->thread; };
        services.implement("scePthreadAttrInit", [st](const Registers& r, Memory& m) {
            if (!r.rdi) return sce(ps4::inval);
            const uint64_t at = allocate(*st, m, threadAttrSize); if (!at) return sce(ps4::nomem);
            if (m.write_data(r.rdi, &at, 8)) { release(*st, at, threadAttrSize); return sce(ps4::inval); }
            st->threadAttrs[at] = {}; return ok(); });
        services.implement("scePthreadAttrDestroy", [st](const Registers& r, Memory& m) {
            uint64_t h = 0; if (!r.rdi || m.read_data(r.rdi, &h, 8) || !st->threadAttrs.erase(h)) return sce(ps4::inval);
            release(*st, h, threadAttrSize); return ok(); });
        // scePthreadSelf(void) -> ScePthread of the running thread (main thread object).
        services.implement("scePthreadSelf", [self](const Registers&, Memory& m) { return ok(self(m)); });
        // scePthreadAttrGet(ScePthread, attr*): copy that thread's attributes.
        services.implement("scePthreadAttrGet", [st, attrOf](const Registers& r, Memory& m) {
            auto* a = attrOf(m, r.rsi); auto it = st->threads.find(r.rdi);
            if (!a || it == st->threads.end()) return sce(ps4::inval);
            *a = it->second; return ok(); });
        services.implement("scePthreadAttrGetaffinity", [attrOf](const Registers& r, Memory& m) {
            auto* a = attrOf(m, r.rdi); if (!a || !r.rsi || m.write_data(r.rsi, &a->affinity, 8)) return sce(ps4::inval); return ok(); });
        services.implement("scePthreadAttrSetaffinity", [attrOf](const Registers& r, Memory& m) {
            auto* a = attrOf(m, r.rdi); if (!a || !r.rsi || (r.rsi & ~uint64_t{0x3f})) return sce(ps4::inval); a->affinity = r.rsi; return ok(); });
        services.implement("scePthreadAttrSetstacksize", [attrOf](const Registers& r, Memory& m) {
            auto* a = attrOf(m, r.rdi); if (!a || r.rsi < 0x4000 || r.rsi > (64ULL << 20)) return sce(ps4::inval); a->stackSize = r.rsi; return ok(); });
        services.implement("scePthreadAttrSetdetachstate", [attrOf](const Registers& r, Memory& m) {
            auto* a = attrOf(m, r.rdi); if (!a || r.rsi > 1) return sce(ps4::inval); a->detach = int(r.rsi); return ok(); });
        services.implement("scePthreadAttrSetinheritsched", [attrOf](const Registers& r, Memory& m) {
            auto* a = attrOf(m, r.rdi); if (!a || (r.rsi != 0 && r.rsi != 4)) return sce(ps4::inval); a->inherit = int(r.rsi); return ok(); });
        services.implement("scePthreadAttrSetschedparam", [attrOf](const Registers& r, Memory& m) {
            int32_t priority = 0; auto* a = attrOf(m, r.rdi);
            if (!a || !r.rsi || m.read_data(r.rsi, &priority, 4)) return sce(ps4::inval);
            a->priority = priority;
            return ok(); });
        // --- thread lifetime (cooperative scheduler in the runner) ---
        // scePthreadCreate(ScePthread* thread, const attr*, void*(*entry)(void*), void* arg,
        // const char* name): new guest thread with its own TCB/DTV (TLS runtime) and
        // its own stack (attr stack size). Deferred: TLS and stack mappings change
        // with no lease held; the runner then attaches a FEX thread (st->spawned).
        // pthread_create(pthread_t*, const pthread_attr_t*, entry, arg) shares the
        // objects; it has no name argument and reports POSIX errno values.
        auto creator = [st, runtime, attrOf](bool sceCodes) { return [st, runtime, attrOf, sceCodes](const Registers& r, Memory& m) {
            auto fail = [sceCodes](ps4::Error x) { return sceCodes ? sce(x) : posix(x); };
            auto failValue = [sceCodes](ps4::Error x) { return sceCodes ? uint64_t(ps4::kernel_error(x)) : uint64_t(x); };
            if (!runtime) return trap("no process owner");
            if (!r.rdi || !r.rdx || !m.permitted(r.rdi, 8, 2)) return fail(ps4::inval);
            State::ThreadAttr attr;
            if (r.rsi) { auto* a = attrOf(m, r.rsi); if (!a) return fail(ps4::inval); attr = *a; }
            const uint64_t object = allocate(*st, m, threadObjectSize);
            if (!object) return fail(ps4::again);
            std::string name = sceCodes ? "" : "pthread";
            for (size_t i = 0; sceCodes && r.r8 && i < 31; ++i) { char c = 0; if (m.read_data(r.r8 + i, &c, 1) || !c) break; name += c; }
            auto record = std::make_shared<State::Guest>();
            record->entry = r.rdx; record->arg = r.rcx; record->name = name; record->detached = attr.detach == 1;
            record->stackSize = (std::max<uint64_t>(attr.stackSize, 0x4000) + ps4Page - 1) / ps4Page * ps4Page;
            const uint64_t out = r.rdi;
            Result d{Outcome::Deferred, 0, "create thread " + name};
            d.outside = [st, runtime, record] {
                if (int status = runtime->create_thread(record->tls)) return status;
                { auto lease = runtime->lease(record->tls); record->fs = lease.fs_base(); }
                record->stackBase = mapStack(*st, *runtime, record->stackSize);
                if (!record->stackBase) { runtime->release_thread(record->tls); return -ENOMEM; }
                return 0;
            };
            d.complete = [st, record, object, out, attr, failValue](Memory& mem, int status) -> uint64_t {
                if (status) { release(*st, object, threadObjectSize); return failValue(status == -EAGAIN ? ps4::again : ps4::nomem); }
                record->rsp = record->stackBase + record->stackSize - 8;
                if (mem.write_data(record->rsp, &st->gate, 8) || mem.write_data(out, &object, 8)) return failValue(ps4::fault);
                st->guests[object] = *record; st->threads[object] = attr; st->spawned.push_back(object);
                return 0;
            };
            return d;
        }; };
        // __tls_get_addr(const tls_index* {module, offset}) for dynamically
        // linked TLS (libc's localtime buffer): the running thread's DTV at
        // FS:[8] holds {generation 1, count, block[1..count]}. Bounds follow
        // the module's TLS memory size; anything else traps.
        services.implement("__tls_get_addr", [runtime](const Registers& r, Memory& m) {
            if (!runtime) return trap("no process owner");
            uint64_t index[2] = {}, dtv = 0, header[2] = {}, block = 0;
            if (!r.rdi || m.read_data(r.rdi, index, sizeof(index))) return trap("__tls_get_addr: tls_index unreadable");
            if (!r.fs || m.read_data(r.fs + 8, &dtv, 8) || !dtv || m.read_data(dtv, header, sizeof(header))) return trap("__tls_get_addr: DTV unreadable");
            if (header[0] != 1 || !index[0] || index[0] > header[1]) return trap("__tls_get_addr: module outside the DTV");
            if (m.read_data(dtv + 8 + index[0] * 8, &block, 8) || !block) return trap("__tls_get_addr: module block absent");
            const size_t size = runtime->module_size(index[0]);
            if (!size || index[1] >= size) return trap("__tls_get_addr: offset outside the module's TLS block");
            return ok(block + index[1]); });
        services.implement("scePthreadCreate", creator(true));
        services.implement("pthread_create", creator(false));
        // scePthreadJoin(ScePthread, void** value): waits for termination.
        auto joiner = [st](bool sceCodes) { return [st, sceCodes](const Registers& r, Memory&) {
            auto fail = [sceCodes](ps4::Error x) { return sceCodes ? sce(x) : posix(x); };
            auto it = st->guests.find(r.rdi);
            if (it == st->guests.end() || it->second.detached || it->second.joined) return fail(ps4::inval);
            if (r.rdi == st->thread) return fail(ps4::deadlk);
            const uint64_t target = r.rdi, out = r.rsi; State* sp = st.get();
            Result b{Outcome::Block, 0, "join"};
            b.ready = [sp, target] { return sp->guests[target].exited; };
            b.wake = [sp, target, out, sceCodes](Memory& mem) -> uint64_t {
                auto& g = sp->guests[target]; g.joined = true;
                if (out && mem.write_data(out, &g.exitValue, 8)) return sceCodes ? uint64_t(ps4::kernel_error(ps4::fault)) : uint64_t(ps4::fault);
                return 0;
            };
            return b;
        }; };
        services.implement("scePthreadJoin", joiner(true));
        services.implement("pthread_join", joiner(false));
        services.implement("scePthreadDetach", [st](const Registers& r, Memory&) {
            auto it = st->guests.find(r.rdi); if (it == st->guests.end() || it->second.detached) return sce(ps4::inval);
            it->second.detached = true; return ok(); });
        services.implement("scePthreadExit", [](const Registers& r, Memory&) { return Result{Outcome::ThreadExit, r.rdi, "scePthreadExit"}; });
        services.implement("pthread_exit", [](const Registers& r, Memory&) { return Result{Outcome::ThreadExit, r.rdi, "pthread_exit"}; });
        auto yield = [](const Registers&, Memory&) { return Result{Outcome::Yield, 0, "yield"}; };
        services.implement("scePthreadYield", yield);
        services.implement("sched_yield", yield);
        // Sleeps block the thread until the deadline (host monotonic clock).
        auto sleepFor = [](uint64_t ns, bool sceCodes) {
            const uint64_t deadline = nowNs() + ns;
            Result b{Outcome::Block, 0, "sleep"}; b.deadlineNs = deadline;
            b.ready = [deadline] { return nowNs() >= deadline; };
            b.wake = [sceCodes](Memory&) -> uint64_t { (void)sceCodes; return 0; };
            return b;
        };
        services.implement("sceKernelUsleep", [sleepFor](const Registers& r, Memory&) { return sleepFor(uint64_t(uint32_t(r.rdi)) * 1000, true); });
        services.implement("sceKernelSleep", [sleepFor](const Registers& r, Memory&) { return sleepFor(uint64_t(uint32_t(r.rdi)) * 1000000000ULL, true); });
        services.implement("pthread_mutex_destroy", [st](const Registers& r, Memory& m) { return destroy(*st, m, r.rdi, false); });
        installConditions(services, st);
        installTime(services, st);
        installFiles(services, st, app0);
        installPad(services, st);
        // The host presenter never shows a system splash screen; hiding it is
        // the state the title asks for. Only the request is recorded.
        services.implement("sceSystemServiceHideSplashScreen", [st](const Registers&, Memory&) { st->splashHidden = true; return ok(); });
        // HLE memcpy (OrbisV4ProcessBatch redirects the title's libc memcpy):
        // void* memcpy(void* dst, const void* src, size_t n), with memmove
        // semantics, in 1 MiB host chunks through the guest memory checks. An
        // unreadable or unwritable range stops the thread, as a fault would.
        services.implement("maxps4_memcpy", [](const Registers& r, Memory& m) {
            const uint64_t dst = r.rdi, src = r.rsi, n = r.rdx;
            if (!n || dst == src) return ok(dst);
            constexpr uint64_t chunk = 1 << 20;
            std::vector<uint8_t> buffer(size_t(std::min<uint64_t>(n, chunk)));
            const bool backward = dst > src && dst - src < n;   // overlapping, destination after source
            for (uint64_t done = 0; done < n;) {
                const uint64_t len = std::min<uint64_t>(chunk, n - done);
                const uint64_t off = backward ? n - done - len : done;
                if (m.read_data(src + off, buffer.data(), size_t(len)) || m.write_data(dst + off, buffer.data(), size_t(len)))
                    return trap("maxps4_memcpy: guest range not accessible");
                done += len;
            }
            return ok(dst); });
        // HLE atan2f (OrbisV4ProcessBatch): float atan2f(float y, float x), the
        // operands' bits in edi/esi (moved from xmm0/xmm1 by the patched entry),
        // the result's bits in eax. Host libm: may differ from the PS4 libc in
        // the last bit (MAXPS4_HLE_MATH=0 keeps the guest implementation).
        services.implement("maxps4_atan2f", [](const Registers& r, Memory&) {
            float y, x; const uint32_t yb = uint32_t(r.rdi), xb = uint32_t(r.rsi);
            std::memcpy(&y, &yb, 4); std::memcpy(&x, &xb, 4);
            const float v = std::atan2(y, x); uint32_t vb; std::memcpy(&vb, &v, 4);
            return ok(vb); });
        // HLE RSDK FillScreen loop (OrbisV4ProcessBatch): rdi RGB565 pixels, edx
        // count, r9 blendLookupTable (u16[256][32]); rcx/rsi/r8 = (channel colour
        // << 16) | alpha for R/G/B. Same 16-bit arithmetic as the guest loop.
        services.implement("maxps4_rsdk_fill", [](const Registers& r, Memory& m) {
            const uint64_t pixels = r.rdi, count = uint32_t(r.rdx), table = r.r9;
            const uint32_t alpha[3] = {uint32_t(r.rcx & 0xffff), uint32_t(r.rsi & 0xffff), uint32_t(r.r8 & 0xffff)};
            const uint32_t colour[3] = {uint32_t(r.rcx >> 16 & 0xffff), uint32_t(r.rsi >> 16 & 0xffff), uint32_t(r.r8 >> 16 & 0xffff)};
            size_t entries = 255 * 32 + 32;
            for (int c = 0; c < 3; ++c) entries = std::max<size_t>(entries, colour[c] + alpha[c] * 32 + 1);
            std::vector<uint16_t> blend(entries), px(static_cast<size_t>(count));
            if (m.read_data(table, blend.data(), entries * 2) || m.read_data(pixels, px.data(), px.size() * 2))
                return trap("maxps4_rsdk_fill: guest range not accessible");
            const uint16_t* keep[3]; uint16_t add[3];
            for (int c = 0; c < 3; ++c) { keep[c] = blend.data() + (255 - alpha[c]) * 32; add[c] = blend[colour[c] + alpha[c] * 32]; }
            for (auto& p : px) {
                const uint16_t red = uint16_t(keep[0][p >> 11] + add[0]), green = uint16_t(keep[1][(p >> 6) & 31] + add[1]),
                               blue = uint16_t(keep[2][p & 31] + add[2]);
                p = uint16_t(blue | uint32_t(green) << 6 | uint32_t(red) << 11);
            }
            if (m.write_data(pixels, px.data(), px.size() * 2)) return trap("maxps4_rsdk_fill: guest range not accessible");
            return ok(0); });
        // HLE RSDK DrawLayerHScroll (OrbisV4ProcessBatch): rdi TileLayer, rsi
        // &currentScreen, rdx line palette bytes, rcx tileset (u8 [tile][16][16]),
        // r8 palettes (u16 [bank][256]), r9 &scanline array (16 bytes per line).
        // Mirrors the guest code step by step, including the scanline x wrap it
        // writes back and its lazily reset column counter; pixel 0 is transparent.
        services.implement("maxps4_rsdk_hscroll", [](const Registers& r, Memory& m) {
            const OrbisV4Translation::Range* last = nullptr;
            auto at = [&](uint64_t guest, uint64_t bytes, uint32_t perm) -> uint8_t* {
                if (last && guest >= last->guest && guest - last->guest < last->size && bytes <= last->size - (guest - last->guest))
                    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(last->host + (guest - last->guest)));
                for (const auto& range : m.ranges())
                    if (guest >= range.guest && guest - range.guest < range.size) {
                        if (bytes > range.size - (guest - range.guest) || (range.permissions & perm) != perm || ((perm & 2) && (range.permissions & 1))) return nullptr;
                        last = &range;
                        return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(range.host + (guest - range.guest)));
                    }
                return nullptr;
            };
            auto u64 = [&](uint64_t g, uint64_t& v) { const uint8_t* p = at(g, 8, 4); if (p) std::memcpy(&v, p, 8); return p != nullptr; };
            uint64_t screen = 0, scanBase = 0, layout = 0;
            const uint8_t* layer = at(r.rdi, 0x3450, 4);
            const uint8_t* screenTail = nullptr;
            if (!layer || !u64(r.rsi, screen) || !u64(r.r9, scanBase) || !(screenTail = at(screen + 0x96018, 0x14, 4)))
                return trap("maxps4_rsdk_hscroll: guest range not accessible");
            int32_t clipY1, clipY2; uint32_t pitch;
            std::memcpy(&pitch, screenTail, 4); std::memcpy(&clipY1, screenTail + 8, 4); std::memcpy(&clipY2, screenTail + 0x10, 4);
            if (clipY1 >= clipY2) return ok(0);
            const uint8_t shift = layer[5]; uint16_t width; std::memcpy(&width, layer + 8, 2); std::memcpy(&layout, layer + 0x3448, 8);
            const int32_t tilesPerLine = int32_t(pitch) >> 4;
            uint8_t* tiles = at(r.rcx, 0x100000, 4);
            const uint8_t* palettes = at(r.r8, 256 * 512, 4);
            const uint8_t* banks = at(r.rdx + int64_t(clipY1), uint64_t(clipY2 - clipY1), 4);
            uint8_t* scan = at(scanBase + int64_t(clipY1) * 16, uint64_t(clipY2 - clipY1) * 16, 6);
            uint8_t* frame = at(screen + uint64_t(int64_t(int32_t(uint32_t(clipY1) * pitch)) * 2), uint64_t(clipY2 - clipY1) * pitch * 2, 6);
            if (!tiles || !palettes || !banks || !scan || !frame) return trap("maxps4_rsdk_hscroll: guest range not accessible");
            uint16_t* dst = reinterpret_cast<uint16_t*>(frame);
            auto tileAt = [&](int64_t entry, uint16_t& t) {   // entry: index into the layout (u16)
                const uint8_t* p = at(layout + uint64_t(entry * 2), 2, 4); if (p) std::memcpy(&t, p, 2); return p != nullptr; };
            for (int32_t y = clipY1; y < clipY2; ++y, scan += 16) {
                const uint16_t* palette = reinterpret_cast<const uint16_t*>(palettes) + size_t(banks[y - clipY1]) * 256;
                int16_t sx; std::memcpy(&sx, scan + 2, 2);
                int32_t px = sx; const int32_t span = int32_t(width) << 4;
                if (px >= 0) { const int32_t d = px - span; if (d >= 0) { px = d & 0xffff; const int16_t w = int16_t(d); std::memcpy(scan + 2, &w, 2); } }
                else { px += span; const uint16_t w = uint16_t(px); std::memcpy(scan + 2, &w, 2); }
                uint16_t sy; int32_t posX, posY; std::memcpy(&sy, scan + 6, 2); std::memcpy(&posX, scan, 4); std::memcpy(&posY, scan + 4, 4);
                const uint32_t rowOffset = uint32_t(sy & 0xf) << 4;
                int32_t column = posX >> 20;
                const int64_t entryBase = int32_t((uint32_t(posY >> 20) << (shift & 31)) + uint32_t(column));
                const int32_t first = px & 0xf, count = 16 - first;
                auto draw = [&](uint16_t t, uint32_t offset, int32_t n) {
                    const uint8_t* src = tiles + ((((uint32_t(t) << 8) & 0xfff00) + offset) & 0xfffff);
                    for (int32_t i = 0; i < n; ++i) if (src[i]) dst[i] = palette[src[i]];
                };
                uint16_t t;
                if (!tileAt(entryBase, t)) return trap("maxps4_rsdk_hscroll: layout not accessible");
                if (t == 0xffff) dst += count;
                else if (count) { draw(t, rowOffset | uint32_t(first), count); dst += count; }
                int64_t entry = entryBase + 1;
                ++column; bool wrapped = column == int32_t(width);
                int64_t cursor = wrapped ? entry - width : entry;
                for (int32_t k = 1; k < tilesPerLine; ++k) {
                    if (!tileAt(cursor, t)) return trap("maxps4_rsdk_hscroll: layout not accessible");
                    if (t != 0xffff) draw(t, rowOffset, 16);
                    dst += 16;
                    const int64_t next = cursor + 1;
                    ++column; if (wrapped) column = 1;
                    wrapped = column == int32_t(width);
                    cursor = wrapped ? next - width : next;
                }
                if (!tileAt(cursor, t)) return trap("maxps4_rsdk_hscroll: layout not accessible");
                if (t == 0xffff) dst += first;
                else if (first) { draw(t, rowOffset, first); dst += first; }
            }
            return ok(0); });
        // sceRtcGetCurrentTick(SceRtcTick*): UTC microseconds since 0001-01-01.
        services.implement("sceRtcGetCurrentTick", [](const Registers& r, Memory& m) {
            using namespace std::chrono;
            const uint64_t tick = uint64_t(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()) + 62135596800ull * 1000000ull;
            if (!r.rdi || m.write_data(r.rdi, &tick, 8)) return ok(0x80b50002u);   // SCE_RTC_ERROR_INVALID_POINTER
            return ok(); });
        // Sonic Mania: an offline console with one local user. Local settings
        // succeed; anything that needs PSN reports SCE_NP_ERROR_SIGNED_OUT.
        constexpr uint64_t signedOut = 0x80550006;
        auto requests = std::make_shared<std::atomic<uint32_t>>(0x10000);
        services.implement("sceNpSetNpTitleId", [](const Registers&, Memory&) { return ok(); });
        services.implement("sceNpSetContentRestriction", [](const Registers&, Memory&) { return ok(); });
        services.implement("sceNpCreateAsyncRequest", [requests](const Registers&, Memory&) { return ok(++*requests); });
        services.implement("sceNpDeleteRequest", [](const Registers&, Memory&) { return ok(); });
        services.implement("sceNpCheckNpAvailabilityA", [](const Registers&, Memory&) { return ok(signedOut); });
        services.implement("sceNpGetAccountIdA", [](const Registers&, Memory&) { return ok(signedOut); });
        services.implement("sceNpGetUserIdByAccountId", [](const Registers&, Memory&) { return ok(signedOut); });
        // sceNpPollAsync(reqId, int* result): finished (0), with the signed-out result.
        services.implement("sceNpPollAsync", [](const Registers& r, Memory& m) {
            const uint32_t result = uint32_t(signedOut);
            if (r.rsi && m.write_data(r.rsi, &result, 4)) return ok(0x80550003u);   // SCE_NP_ERROR_INVALID_ARGUMENT
            return ok(); });
        // sceSystemServiceGetStatus: no event, no overlay, foreground (fields only, reserved bytes untouched).
        services.implement("sceSystemServiceGetStatus", [](const Registers& r, Memory& m) {
            const uint8_t status[9] = {};
            if (!r.rdi || m.write_data(r.rdi, status, sizeof status)) return ok(0x80a10002u);   // SCE_SYSTEM_SERVICE_ERROR_PARAMETER
            return ok(); });
        services.implement("sceSystemServiceReceiveEvent", [](const Registers&, Memory&) { return ok(0x80a10003u); });   // NO_EVENT
        // sceUserServiceGetLoginUserIdList: the local user, then invalid ids.
        services.implement("sceUserServiceGetLoginUserIdList", [](const Registers& r, Memory& m) {
            const int32_t list[4] = {OrbisV4LocalServices::localUser, -1, -1, -1};
            if (!r.rdi || m.write_data(r.rdi, list, sizeof list)) return ok(0x80960005u);   // SCE_USER_SERVICE_ERROR_INVALID_ARGUMENT
            return ok(); });
        services.implement("sceUserServiceGetEvent", [](const Registers&, Memory&) { return ok(0x80960007u); });   // NO_EVENT
        installTrophies(services, st, app0);
        st->png = OrbisV4PngDecService::install(services);
        st->audio = OrbisV4AudioOutService::install(services);
        st->graphics=OrbisV4GraphicsServices::install(services,[weak=std::weak_ptr<State>(st)](Memory& m,size_t bytes){
            auto state=weak.lock();return state?allocate(*state,m,bytes):0;
        });
        // Pixel shading on the host GPU when the program links a backend (Metal on Apple hosts).
        if (&gpu::hostPixelBackend) st->graphics->engine.renderer.pixelBackend = gpu::hostPixelBackend();
        return st;
    }
};
}
