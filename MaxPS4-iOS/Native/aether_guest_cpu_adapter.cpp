// MaxPS4 thin ABI adapter for real AetherPS4 guest-CPU abstraction.
// Upstream guest_cpu.{h,cpp}: MIT, Leviidev/AetherPS4 @ 3347fc13...
// This is an integration smoke test, NOT an x86-64 -> ARM64 JIT.
#include "core/guest_cpu/guest_cpu.h"
#include <cerrno>
#include <variant>

// Synthetic executor tests the upstream backend's success/result dispatch.
// It is intentionally not a guest CPU interpreter or a FEXCore JIT.
static Core::GuestExecutionResult TestExecutor(
    const Core::GuestExecutionRequest& request) {
    Core::GuestExecutionState state{};
    state.FirstRip = request.Rip;
    state.Rip = request.Rip + 4;
    state.LastRip = request.Rip;
    state.Rsp = request.Rsp;
    state.Gpr = request.Gpr;
    state.StopReason = Core::GuestStopReason::Returned;
    return state;
}

extern "C" int maxps4_aether_guest_backend_probe() noexcept {
    Core::GuestExecutionRequest request{};
    request.Rip = 0x1000;
    Core::UnavailableGuestCpuBackend unavailable;
    const auto not_supported = unavailable.Run(request);
    const auto* missing = std::get_if<Core::GuestExecutionFailure>(&not_supported);
    if (!missing || missing->Error != ENOTSUP) return 0;

    Core::NativeGuestCpuBackend no_executor(nullptr);
    const auto no_handler = no_executor.Run(request);
    const auto* no_syscall = std::get_if<Core::GuestExecutionFailure>(&no_handler);
    if (!no_syscall || no_syscall->Error != ENOSYS) return 0;
    request.Rsp = 0x2000;
    request.Gpr[0] = 42;
    Core::NativeGuestCpuBackend test_executor(&TestExecutor);
    const auto executed = test_executor.Run(request);
    const auto* state = std::get_if<Core::GuestExecutionState>(&executed);
    if (!state || state->FirstRip != 0x1000 || state->Rip != 0x1004 ||
        state->Rsp != 0x2000 || state->Gpr[0] != 42 ||
        state->StopReason != Core::GuestStopReason::Returned) return 0;
    return 1;
}
