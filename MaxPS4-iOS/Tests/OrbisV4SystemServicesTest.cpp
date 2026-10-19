// Mission 50: host system services without FEX. Guest memory is a real
// OrbisV4Translation lease; every handler is reached through the stub
// dispatcher (exact RIP/EAX), as the runner does.
#include "../Native/OrbisV4SystemServices.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>
using namespace maxps4;
using O = OrbisV4HostServices::Outcome;
namespace {
constexpr uint64_t kernelState = 0x10000000, scratch = 0x20000000;
struct Harness {
    OrbisV4ProcessBatch batch;
    std::unique_ptr<OrbisV4Translation> memory;
    std::unique_ptr<OrbisV4Translation::Lease> lease;
    std::unique_ptr<OrbisV4HostServices> services;
    std::shared_ptr<OrbisV4SystemServices::State> state;
    OrbisLinker::Image image;
    explicit Harness(const std::string& app0 = "") {
        memory = std::make_unique<OrbisV4Translation>([] {});
        assert(!memory->reserve(kernelState, OrbisV4ProcessBatch::kernelState) &&
               !memory->map(kernelState, OrbisV4ProcessBatch::kernelState, 6));
        assert(!memory->reserve(scratch, 0x10000) && !memory->map(scratch, 0x10000, 6));
        batch.stateBase["libkernel.prx"] = kernelState;
        lease = std::make_unique<OrbisV4Translation::Lease>(*memory);
        services = std::make_unique<OrbisV4HostServices>(batch);
        // Process param of a fake executable module, for sceKernelGetProcParam.
        OrbisLinker::Module eboot; eboot.path = "eboot.bin"; eboot.bias = 0x400000;
        eboot.processParam.present = true; eboot.processParam.address = 0x158000;
        image.modules.push_back(eboot);
        state = OrbisV4SystemServices::install(*services, batch, image, nullptr, app0);
        state->thread = 0x1111;
    }
    ~Harness() { lease.reset(); memory->close(); }
    // Registers a service stub for `name` and calls it like the runner.
    OrbisV4HostServices::Result call(const std::string& name, uint64_t rdi = 0, uint64_t rsi = 0, uint64_t rdx = 0, uint64_t rcx = 0, uint64_t r8 = 0, uint64_t r9 = 0) {
        size_t index = batch.services.size();
        for (size_t i = 0; i < batch.services.size(); ++i) if (batch.services[i].name == name) index = i;
        if (index == batch.services.size()) {
            OrbisV4ProcessBatch::Service s; s.id = OrbisV4ProcessBatch::serviceBase + uint32_t(index); s.name = name;
            s.stub = 0x5000000 + index * 16; batch.services.push_back(s);
        }
        OrbisV4HostServices::Registers r;
        r.rax = batch.services[index].id; r.rip = batch.services[index].stub + OrbisV4ServicePRX::Layout::syscallOffset;
        r.rdi = rdi; r.rsi = rsi; r.rdx = rdx; r.rcx = rcx; r.r8 = r8; r.r9 = r9; r.rsp = scratch + 0xff00;
        return services->dispatch(r, *lease);
    }
    uint64_t u64(uint64_t a) { uint64_t v = 0; assert(!lease->memory().read(a, &v, 8)); return v; }
    uint32_t u32(uint64_t a) { uint32_t v = 0; assert(!lease->memory().read(a, &v, 4)); return v; }
    void put(uint64_t a, uint64_t v) { assert(!lease->write_data(a, &v, 8)); }
};
uint64_t sce(ps4::Error e) { return uint64_t(ps4::kernel_error(e)); }
bool returned(const OrbisV4HostServices::Result& r, uint64_t v) { return r.outcome == O::Returned && r.value == v; }
}
int main() {
    {   // Direct physical accounting: alignment, non-overlap, exhaustion,
        // invalid output leaves accounting intact, release and hole reuse.
        Harness h; const uint64_t out = scratch + 0xa00;
        const auto capacity = h.call("sceKernelGetDirectMemorySize").value;
        assert(capacity == OrbisV4SystemServices::directLimit);
        assert(returned(h.call("sceKernelAllocateDirectMemory", 0, capacity, 0x4000, 0x10000, 3, out), 0));
        assert(h.u64(out) == 0);
        assert(returned(h.call("sceKernelAllocateDirectMemory", 0, capacity, 0x4000, 0x10000, 3, out), 0));
        assert(h.u64(out) == 0x10000);
        assert(returned(h.call("sceKernelAllocateDirectMemory", 0, capacity, 0x4000, 0x4000, 3, 0), sce(ps4::inval)));
        assert(h.state->direct.size() == 2);
        assert(returned(h.call("sceKernelAllocateDirectMemory", 0, capacity, capacity, 0, 3, out), sce(ps4::again)));
        assert(returned(h.call("sceKernelAllocateDirectMemory", 0, capacity, 7, 0, 3, out), sce(ps4::inval)));
        assert(returned(h.call("sceKernelReleaseDirectMemory", 0, 0x8000), sce(ps4::inval)));
        assert(returned(h.call("sceKernelReleaseDirectMemory", 0, 0x4000), 0));
        assert(returned(h.call("sceKernelAllocateDirectMemory", 0, capacity, 0x4000, 0, 3, out), 0) && h.u64(out) == 0);
        h.put(out, 0);
        assert(h.call("sceKernelMapDirectMemory", out, 0x4000, 0x33, 0, 0, 0).outcome == O::Trap); // no runtime owner
        assert(returned(h.call("sceKernelMapDirectMemory", out, 0x4000, 7, 0, 0, 0), sce(ps4::inval)));
    }
    {   // Dispatcher: unknown RIP/ID refused; unimplemented service traps.
        Harness h;
        OrbisV4HostServices::Registers bad; bad.rax = 7; bad.rip = 0x1234; bad.rsp = scratch + 0xff00;
        assert(h.services->dispatch(bad, *h.lease).outcome == O::Refused);
        // (sceGnmSubmitDone, used here before, is implemented by the graphics services.)
        auto t = h.call("sceKernelMprotect");
        assert(t.outcome == O::Trap && t.detail.find("sceKernelMprotect") != std::string::npos);
        h.call("scePthreadMutexLock"); // registers the stub
        OrbisV4HostServices::Registers wrong; wrong.rax = h.batch.services.back().id; wrong.rip = h.batch.services.back().stub; wrong.rsp = scratch + 0xff00;
        assert(h.services->dispatch(wrong, *h.lease).outcome == O::Refused);
        assert(returned(h.call("sceKernelGetProcParam"), 0x558000));
        assert(OrbisV4ProcessBatch::overridden("sceVideoOutOpen"));
        assert(returned(h.call("sceVideoOutOpen", 0, 0, 0, 0), 0x80290018));
    }
    {   // Mutexes: guest layout, types, errors, static initialiser, contention blocks.
        Harness h; const uint64_t attr = scratch, mutex = scratch + 8, other = scratch + 16;
        assert(returned(h.call("scePthreadMutexattrInit", attr), 0));
        assert(returned(h.call("scePthreadMutexattrSettype", attr, 2), 0));
        assert(returned(h.call("scePthreadMutexattrSettype", attr, 9), sce(ps4::inval)));
        assert(returned(h.call("scePthreadMutexInit", mutex, attr, 0), 0));
        const uint64_t object = h.u64(mutex);
        assert(object >= kernelState + OrbisV4ProcessBatch::kernelArena && h.u32(object + 0x20) == 2);
        assert(returned(h.call("scePthreadMutexLock", mutex), 0) && returned(h.call("scePthreadMutexLock", mutex), 0));
        assert(h.u32(object) == uint32_t(0x1111) && h.u32(object + 0x24) == 2);
        assert(returned(h.call("scePthreadMutexDestroy", mutex), sce(ps4::busy)));
        h.state->thread = 0x2222;
        assert(returned(h.call("scePthreadMutexUnlock", mutex), sce(ps4::perm)));
        assert(returned(h.call("scePthreadMutexTrylock", mutex), sce(ps4::busy)));
        auto blocked = h.call("scePthreadMutexLock", mutex);
        assert(blocked.outcome == O::Block && !blocked.ready());
        h.state->thread = 0x1111;
        assert(returned(h.call("scePthreadMutexUnlock", mutex), 0) && returned(h.call("scePthreadMutexUnlock", mutex), 0));
        assert(blocked.ready() && blocked.wake(*h.lease) == 0 && h.u32(object) == uint32_t(0x2222));
        // Default (errorcheck) mutex via NULL static initialiser; self-relock -> EDEADLK.
        h.put(other, 0);
        assert(returned(h.call("scePthreadMutexLock", other), 0) && h.u64(other) != 0);
        assert(returned(h.call("scePthreadMutexLock", other), sce(ps4::deadlk)));
        assert(returned(h.call("pthread_mutex_lock", other), uint64_t(ps4::deadlk))); // POSIX: raw errno
        assert(returned(h.call("scePthreadMutexUnlock", other), 0) && returned(h.call("scePthreadMutexDestroy", other), 0));
        assert(h.u64(other) == 2 && returned(h.call("scePthreadMutexLock", other), sce(ps4::inval)));
        assert(returned(h.call("scePthreadMutexInit", 0, 0, 0), sce(ps4::inval)));
    }
    {   // Condition variables: EPERM without the mutex, signal wakes, timeout.
        Harness h; const uint64_t cond = scratch + 0x100, mutex = scratch + 0x108;
        h.put(cond, 0); h.put(mutex, 0);
        assert(returned(h.call("scePthreadCondWait", cond, mutex), sce(ps4::perm)));
        assert(returned(h.call("scePthreadMutexLock", mutex), 0));
        auto wait = h.call("scePthreadCondWait", cond, mutex);
        assert(wait.outcome == O::Block && !wait.ready());
        const uint64_t mx = h.u64(mutex); assert(h.u32(mx) == 0); // released while waiting
        assert(returned(h.call("scePthreadCondSignal", cond), 0) && wait.ready());
        assert(wait.wake(*h.lease) == 0 && h.u32(mx) == uint32_t(0x1111));
        auto timed = h.call("scePthreadCondTimedwait", cond, mutex, 1000);
        assert(timed.outcome == O::Block && timed.deadlineNs);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(timed.ready() && timed.wake(*h.lease) == sce(OrbisV4SystemServices::timedOut));
        assert(returned(h.call("scePthreadCondDestroy", cond), 0));
    }
    {   // Time, errno, sleeps.
        Harness h; const uint64_t ts = scratch + 0x200;
        assert(returned(h.call("clock_gettime", 4, ts), 0) && h.u64(ts) < 100000000000ULL);
        assert(returned(h.call("clock_gettime", 99, ts), uint64_t(-1)));
        const uint64_t err = h.call("__error").value; assert(err && h.u32(err) == 22);
        assert(returned(h.call("sceKernelClockGettime", 99, ts), sce(ps4::inval)));
        auto t0 = h.call("sceKernelGetProcessTime").value; std::this_thread::sleep_for(std::chrono::milliseconds(2));
        assert(h.call("sceKernelGetProcessTime").value > t0);
        auto sleep = h.call("sceKernelUsleep", 1000); assert(sleep.outcome == O::Block && sleep.deadlineNs);
    }
    {   // Heap trace info: exact layout, tracing disabled, kernel-owned words.
        Harness h; const uint64_t info = scratch + 0x300;
        const uint8_t defaults[32] = {0x20, 0, 0, 0, 0, 0, 0, 0, 1};
        assert(!h.lease->write_data(info, defaults, 32));
        assert(returned(h.call("sceLibcHeapGetTraceInfo", info), 0));
        assert(h.u32(info + 0xc) == 0 && h.u64(info + 0x10) && h.u64(info + 0x18) == h.u64(info + 0x10) + 8);
        const uint8_t wrong[16] = {0x10};
        assert(!h.lease->write_data(info, wrong, 16) && h.call("sceLibcHeapGetTraceInfo", info).outcome == O::Trap);
    }
    {   // Virtual pad: init/open order, neutral state layout.
        Harness h; const uint64_t data = scratch + 0x400, info = scratch + 0x500;
        assert(returned(h.call("scePadOpen", 1, 0, 0), 0x80920005));
        assert(returned(h.call("scePadInit"), 0) && returned(h.call("scePadOpen", 2, 0, 0), 0x80920001));
        assert(returned(h.call("scePadOpen", 1, 0, 0), 1));
        assert(returned(h.call("scePadReadState", 1, data), 0));
        uint8_t d[0x78]; assert(!h.lease->memory().read(data, d, sizeof(d)));
        assert(d[4] == 0x80 && d[5] == 0x80 && d[0x4c] == 1 && d[0x68] == 1 && h.u32(data) == 0);
        h.state->pad.buttons = 0x4000; h.state->pad.lx = 0;
        assert(returned(h.call("scePadReadState", 1, data), 0) && h.u32(data) == 0x4000);
        assert(returned(h.call("scePadGetControllerInformation", 1, info), 0));
        assert(returned(h.call("scePadReadState", 7, data), 0x80920003));
        // No system splash exists on the host presenter: hiding it only records the request.
        assert(!h.state->splashHidden && returned(h.call("sceSystemServiceHideSplashScreen"), 0) && h.state->splashHidden);
        // POSIX thread entry points share the objects but report errno values.
        assert(returned(h.call("pthread_join", 0x777, 0), 22) && returned(h.call("scePthreadJoin", 0x777, 0), uint64_t(ps4::kernel_error(ps4::inval))));
        assert(h.call("pthread_create", scratch, 0, 0x1000, 0).outcome == O::Trap); // no process owner in this harness
        assert(h.call("__tls_get_addr", scratch).outcome == O::Trap); // DTV walk needs the TLS runtime (validated in the title)
        // CLOCK_SECOND (libc time()) and UTC <-> local conversion with the host zone.
        { const uint64_t ts = scratch + 0x200; assert(returned(h.call("clock_gettime", 13, ts), 0) && h.u64(ts + 8) == 0 && h.u64(ts) > 1700000000);
          const int64_t utc = int64_t(h.u64(ts)); time_t t = time_t(utc); struct tm lt{}; assert(localtime_r(&t, &lt));
          assert(returned(h.call("sceKernelConvertUtcToLocaltime", uint64_t(utc), ts + 16, ts + 32, 0), 0) && int64_t(h.u64(ts + 16)) == utc + lt.tm_gmtoff);
          assert(returned(h.call("sceKernelConvertLocaltimeToUtc", h.u64(ts + 16), 0, ts + 48, 0, 0), 0) && int64_t(h.u64(ts + 48)) == utc);
          assert(returned(h.call("sceKernelConvertUtcToLocaltime", uint64_t(-1), ts + 16, 0, 0), uint64_t(ps4::kernel_error(ps4::inval)))); }
    }
    {   // Local trophies from a synthetic TRP directory (10 TROPnnn.PNG entries).
        const std::string dir = "/tmp/maxps4-trophy-test-" + std::to_string(::getpid());
        std::system(("mkdir -p '" + dir + "/sce_sys/trophy'").c_str());
        std::vector<uint8_t> trp(0x60 + 12 * 64, 0);
        auto be = [&](size_t o, uint64_t v, size_t n) { for (size_t i = 0; i < n; ++i) trp[o + i] = uint8_t(v >> (8 * (n - 1 - i))); };
        be(0, 0xdca24d00, 4); be(4, 3, 4); be(16, 12, 4); be(20, 64, 4);
        for (int i = 0; i < 12; ++i) {
            char name[32] = {}; std::snprintf(name, sizeof(name), i < 10 ? "TROP%03d.PNG" : "TROP_%02d.ESFM", i);
            std::copy(name, name + 32, trp.begin() + 0x60 + i * 64);
        }
        { std::ofstream f(dir + "/sce_sys/trophy/trophy00.trp", std::ios::binary); f.write(reinterpret_cast<const char*>(trp.data()), trp.size()); }
        Harness h(dir); const uint64_t ctx = scratch + 0x600, handle = scratch + 0x604, flags = scratch + 0x610, count = scratch + 0x620, plat = scratch + 0x624;
        assert(h.state->trophyCount == 10);
        assert(returned(h.call("sceNpTrophyCreateContext", ctx, 1, 0, 0), 0) && returned(h.call("sceNpTrophyCreateHandle", handle), 0));
        const uint32_t c = h.u32(ctx), hd = h.u32(handle);
        assert(returned(h.call("sceNpTrophyRegisterContext", c, hd, 0), 0));
        assert(returned(h.call("sceNpTrophyUnlockTrophy", c, hd, 3, plat), 0) && int32_t(h.u32(plat)) == -1);
        assert(returned(h.call("sceNpTrophyUnlockTrophy", c, hd, 10, plat), 0x8055160a));
        assert(returned(h.call("sceNpTrophyGetTrophyUnlockState", c, hd, flags, count), 0) && h.u32(flags) == 8 && h.u32(count) == 10);
        assert(returned(h.call("sceNpTrophyCreateContext", ctx, 2, 0, 0), 0x80551604));
        assert(returned(h.call("sceNpTrophyDestroyHandle", hd), 0) && returned(h.call("sceNpTrophyDestroyContext", c), 0));
        std::system(("rm -rf '" + dir + "'").c_str());
    }
    {   // Files: /app0 read-only through PS4Libkernel over the lease.
        const std::string dir = "/tmp/maxps4-app0-test-" + std::to_string(::getpid());
        std::system(("mkdir -p '" + dir + "/rsc'").c_str());
        { std::ofstream f(dir + "/rsc/data.bin", std::ios::binary); f << "KeroBlaster!"; }
        Harness h(dir); const uint64_t path = scratch + 0x700, buf = scratch + 0x800, st = scratch + 0x900;
        const char p[] = "/app0/rsc/data.bin"; assert(!h.lease->write_data(path, p, sizeof(p)));
        const auto fd = h.call("sceKernelOpen", path, 0, 0).value; assert(int64_t(fd) >= 3);
        assert(returned(h.call("sceKernelRead", fd, buf, 4), 4));
        assert(returned(h.call("sceKernelPread", fd, buf + 16, 6, 6), 6));
        char got[12] = {}; assert(!h.lease->memory().read(buf + 16, got, 6) && std::string(got, 6) == "aster!");
        assert(returned(h.call("sceKernelRead", fd, buf, 2), 2)); // position unchanged by pread: "Bl"
        assert(!h.lease->memory().read(buf, got, 2) && got[0] == 'B' && got[1] == 'l');
        assert(returned(h.call("sceKernelFstat", fd, st), 0) && h.u64(st + 72) == 12);
        assert(returned(h.call("sceKernelClose", fd), 0) && returned(h.call("sceKernelClose", fd), sce(ps4::badf)));
        assert(returned(h.call("sceKernelOpen", path, 1, 0), sce(ps4::rofs)));
        const char escape[] = "/app0/rsc/../../etc/passwd"; assert(!h.lease->write_data(path, escape, sizeof(escape)));
        assert(int64_t(h.call("sceKernelOpen", path, 0, 0).value) < 0);
        const char msg[] = "hello"; assert(!h.lease->write_data(buf, msg, 5));
        assert(returned(h.call("sceKernelWrite", 1, buf, 5), 5) && h.state->stdoutLog.back() == "hello");
        assert(returned(h.call("sceKernelWrite", 7, buf, 5), sce(ps4::badf)));
        assert(returned(h.call("_write", 1, buf, 5), 5) && h.state->stdoutLog.back() == "hello");
        assert(returned(h.call("_write", 7, buf, 5), uint64_t(-1)));
        assert(h.u32(h.call("__error").value) == uint32_t(ps4::badf));
        std::system(("rm -rf '" + dir + "'").c_str());
    }
    std::cout << "PASS system services: direct physical allocation/alignment/exhaustion/reuse, /app0 files (open/read/pread/fstat/close, read-only, no traversal, stdout/_write/errno), dispatcher refusals/traps, mutex layout/types/contention, conditions, clocks/errno/sleep, heap-trace layout, virtual pad, local trophies (10 from TRP directory)\n";
}
