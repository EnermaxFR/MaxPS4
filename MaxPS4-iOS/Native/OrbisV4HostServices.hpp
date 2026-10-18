#pragma once
#include "OrbisV4ProcessBatch.hpp"
#include "maxps4_orbis_v4_translation.h"
#include <functional>

namespace maxps4 {
// Mission 50: host side of the service stubs of OrbisV4ProcessBatch. A call
// is accepted only if RIP is exactly the stub's SYSCALL and EAX its id. A
// service is either IMPLEMENTED (documented semantics, real effects through
// the leased guest memory) or a TRAP: execution stops with a diagnostic; no
// value is fabricated. Guest memory is only reached through the lease, so
// every access is bounds/permission checked.
class OrbisV4HostServices {
public:
    struct Registers { uint64_t rax = 0, rdi = 0, rsi = 0, rdx = 0, rcx = 0, r8 = 0, r9 = 0, rsp = 0, rip = 0, fs = 0; };
    // Exit: process exit requested (value = status). ThreadExit: the calling
    // guest thread ends (value = its exit value). Block: the thread waits until
    // ready() holds (scheduler polls it; deadlineNs bounds a timed wait), then
    // wake() produces RAX. Yield: the thread stays runnable but cedes the CPU.
    enum class Outcome { Returned, Trap, Refused, Exit, Deferred, Block, Yield, ThreadExit };
    using Memory = OrbisV4Translation::Lease;
    // Deferred: the runner stops dispatch, releases the lease, calls `outside`
    // (mapping changes), re-leases, then `complete` produces the final result.
    struct Result {
        Result(Outcome o = Outcome::Trap, uint64_t v = 0, std::string d = {}) : outcome(o), value(v), detail(std::move(d)) {}
        Outcome outcome = Outcome::Trap; uint64_t value = 0; std::string detail;
        std::function<int()> outside; std::function<uint64_t(Memory&, int)> complete;
        std::function<bool()> ready; std::function<uint64_t(Memory&)> wake; uint64_t deadlineNs = 0;
    };
    struct Call { std::string name, nid; uint64_t caller = 0; Registers registers; Outcome outcome; uint64_t value; uint64_t thread = 0; };
    uint64_t currentThread = 0; // Set by the runner before dispatching a guest thread.
    void note(uint64_t thread, const std::string& what) { (void)thread; (void)what; }
    using Handler = std::function<Result(const Registers&, Memory&)>;

    explicit OrbisV4HostServices(const OrbisV4ProcessBatch& batch) : batch_(batch) {}
    void implement(const std::string& name, Handler handler) { handlers_[name] = std::move(handler); }
    bool implemented(const std::string& name) const { return handlers_.count(name) != 0; }
    // Most recent calls (a bounded window); calls()[0] is call number droppedCalls().
    const std::vector<Call>& calls() const { return calls_; }
    uint64_t droppedCalls() const { return dropped_; }
    uint64_t totalCalls() const { return dropped_ + calls_.size(); }
    // Records the final value of the last (deferred) call.
    void settle(uint64_t value) { if (!calls_.empty()) calls_.back().value = value; }

    Result dispatch(const Registers& r, Memory& memory) {
        const auto* service = batch_.service(uint32_t(r.rax));
        if (r.rax > UINT32_MAX || !service || service->stub + OrbisV4ServicePRX::Layout::syscallOffset != r.rip)
            return record(nullptr, r, memory, {Outcome::Refused, 0, "SYSCALL outside a service stub or id/RIP mismatch"});
        const auto it = handlers_.find(service->name);
        if (it == handlers_.end())
            return record(service, r, memory, {Outcome::Trap, 0, "unimplemented service " + service->name + " (" + service->library + ")"});
        return record(service, r, memory, it->second(r, memory));
    }
private:
    Result record(const OrbisV4ProcessBatch::Service* service, const Registers& r, Memory& memory, Result result) {
        Call c{service ? service->name : "?", service ? service->nid : "?", 0, r, result.outcome, result.value, currentThread};
        memory.read_data(r.rsp, &c.caller, 8); // Return address of the stub's caller (best effort).
        if (calls_.size() >= 100000) { calls_.erase(calls_.begin(), calls_.begin() + 50000); dropped_ += 50000; }
        calls_.push_back(std::move(c));
        return result;
    }
    const OrbisV4ProcessBatch& batch_;
    std::map<std::string, Handler> handlers_;
    std::vector<Call> calls_;
    uint64_t dropped_ = 0;
};
}
