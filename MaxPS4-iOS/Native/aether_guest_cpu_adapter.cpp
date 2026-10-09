// MaxPS4 thin ABI adapter for real AetherPS4 guest-CPU abstraction.
// Upstream guest_cpu.{h,cpp}: MIT, Leviidev/AetherPS4 @ 3347fc13...
// This is an integration smoke test, NOT an x86-64 -> ARM64 JIT.
#include "core/guest_cpu/guest_cpu.h"
#include <cerrno>
#include <variant>

// Real MaxPS4 restricted x86 interpreter reached through AetherPS4's
// unmodified NativeGuestCpuBackend dispatch. This is NOT FEXCore.
extern "C" int maxps4_native_guest_x86_run(
    const std::uint8_t*, std::size_t, std::uint32_t, std::uint64_t*) noexcept;
static Core::GuestExecutionResult MaxPS4RestrictedExecutor(
    const Core::GuestExecutionRequest& request) {
    if (request.Rip != 0x1000 || request.Rsp == 0)
        return Core::GuestExecutionFailure{
            .Stage = Core::GuestExecutionStage::Mapping, .Error = EINVAL};
    // Synthetic in-memory guest block: MOV EAX,40; ADD EAX,2; RET.
    // No PS4 ELF/SELF loading, no mapped external guest pointers.
    constexpr std::uint8_t code[] = {
        0xB8,40,0,0,0, 0x05,2,0,0,0, 0xC3
    };
    std::uint64_t result = 0;
    if (maxps4_native_guest_x86_run(code, sizeof(code), 16, &result) != 1)
        return Core::GuestExecutionFailure{
            .Stage = Core::GuestExecutionStage::Execute, .Error = ENOTSUP};
    Core::GuestExecutionState state{};
    state.FirstRip = request.Rip;
    state.LastRip = request.Rip;
    state.Rip = request.Rip + sizeof(code);
    state.Rsp = request.Rsp;
    state.Gpr = request.Gpr;
    state.Gpr[0] = result;
    state.Rflags = request.Rflags;
    state.StopReason = Core::GuestStopReason::Returned;
    return state;
}

extern "C" int maxps4_aether_restricted_cpu_dispatch_probe() noexcept {
    Core::GuestExecutionRequest request{};
    request.Rip = 0x1000;
    request.Rsp = 0x2000;
    request.Gpr[1] = 99;
    request.Rflags = 0x202;
    Core::NativeGuestCpuBackend backend(&MaxPS4RestrictedExecutor);
    const auto output = backend.Run(request);
    const auto* state = std::get_if<Core::GuestExecutionState>(&output);
    if (!state || state->Gpr[0] != 42 || state->Gpr[1] != 99 ||
        state->Rflags != 0x202 || state->Rip != 0x100B ||
        state->Rsp != request.Rsp ||
        state->StopReason != Core::GuestStopReason::Returned) return 0;
    request.Rip = 0xDEAD;
    const auto bad = backend.Run(request);
    const auto* failure = std::get_if<Core::GuestExecutionFailure>(&bad);
    return failure && failure->Stage == Core::GuestExecutionStage::Mapping &&
           failure->Error == EINVAL ? 1 : 0;
}

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
    // A failing executor must preserve the upstream error/stage contract.
    Core::NativeGuestCpuBackend failing_executor(
        +[](const Core::GuestExecutionRequest&) -> Core::GuestExecutionResult {
            return Core::GuestExecutionFailure{
                .Stage = Core::GuestExecutionStage::Mapping,
                .Error = EFAULT
            };
        });
    const auto failure = failing_executor.Run(request);
    const auto* fault = std::get_if<Core::GuestExecutionFailure>(&failure);
    if (!fault || fault->Stage != Core::GuestExecutionStage::Mapping ||
        fault->Error != EFAULT) return 0;

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
