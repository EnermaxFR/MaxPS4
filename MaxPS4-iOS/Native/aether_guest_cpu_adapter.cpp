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
    if (request.Rsp == 0)
        return Core::GuestExecutionFailure{
            .Stage = Core::GuestExecutionStage::Mapping, .Error = EINVAL};
    // Whitelisted virtual RIP values for two safe synthetic blocks only.
    // No arbitrary guest-pointer dereference, PS4 ELF/SELF loading or FEXCore.
    constexpr std::uint8_t arithmetic[] = {
        0xB8,40,0,0,0, 0x05,2,0,0,0, 0xC3
    };
    constexpr std::uint8_t conditional[] = {
        0xB8,42,0,0,0, 0x3D,42,0,0,0,
        0x74,5, 0xB8,1,0,0,0, 0xC3
    };
    const std::uint8_t* code = nullptr;
    std::size_t length = 0;
    if (request.Rip == 0x1000) {
        code = arithmetic;
        length = sizeof(arithmetic);
    } else if (request.Rip == 0x1100) {
        code = conditional;
        length = sizeof(conditional);
    } else {
        return Core::GuestExecutionFailure{
            .Stage = Core::GuestExecutionStage::Mapping, .Error = EINVAL};
    }
    std::uint64_t result = 0;
    if (maxps4_native_guest_x86_run(code, length, 32, &result) != 1)
        return Core::GuestExecutionFailure{
            .Stage = Core::GuestExecutionStage::Execute, .Error = ENOTSUP};
    Core::GuestExecutionState state{};
    state.FirstRip = request.Rip;
    state.LastRip = request.Rip;
    state.Rip = request.Rip + length;
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

// Multi-case integration regression for the actual AetherPS4 backend API.
// Validates state forwarding, repeatable dispatch, and rejected requests.
// Still uses MaxPS4's restricted interpreter, not FEXCore or a PS4 loader.
extern "C" int maxps4_aether_cpu_state_regression() noexcept {
    Core::NativeGuestCpuBackend backend(&MaxPS4RestrictedExecutor);
    for (std::uint32_t i = 0; i < 8; ++i) {
        Core::GuestExecutionRequest request{};
        request.Rip = 0x1000;
        request.Rsp = 0x2000 + i * 16;
        request.Rflags = 0x202 + (std::uint64_t(i) << 8);
        request.Gpr[0] = 1234;
        request.Gpr[1] = 0xABC000 + i;
        request.Gpr[15] = 0xDEF000 + i;
        const auto outcome = backend.Run(request);
        const auto* state = std::get_if<Core::GuestExecutionState>(&outcome);
        if (!state || state->FirstRip != request.Rip ||
            state->LastRip != request.Rip || state->Rip != 0x100B ||
            state->Rsp != request.Rsp || state->Rflags != request.Rflags ||
            state->Gpr[0] != 42 || state->Gpr[1] != request.Gpr[1] ||
            state->Gpr[15] != request.Gpr[15] ||
            state->StopReason != Core::GuestStopReason::Returned) return 0;
    }
    {
        Core::GuestExecutionRequest request{};
        request.Rip = 0x1000;
        request.Rsp = 0;
        const auto outcome = backend.Run(request);
        const auto* failure = std::get_if<Core::GuestExecutionFailure>(&outcome);
        if (!failure || failure->Error != EINVAL ||
            failure->Stage != Core::GuestExecutionStage::Mapping) return 0;
    }
    return 1;
}

// Confirm that upstream dispatch selects multiple synthetic guest RIP blocks,
// preserves guest state and reports bad mappings without executing pointers.
extern "C" int maxps4_aether_multi_block_dispatch_probe() noexcept {
    Core::NativeGuestCpuBackend backend(&MaxPS4RestrictedExecutor);
    for (const std::uintptr_t rip : {std::uintptr_t(0x1000), std::uintptr_t(0x1100)}) {
        Core::GuestExecutionRequest request{};
        request.Rip = rip;
        request.Rsp = 0x3000;
        request.Gpr[2] = 0xCAFE;
        request.Rflags = 0x202;
        const auto output = backend.Run(request);
        const auto* state = std::get_if<Core::GuestExecutionState>(&output);
        if (!state || state->Gpr[0] != 42 || state->Gpr[2] != 0xCAFE ||
            state->Rflags != 0x202 || state->Rsp != request.Rsp ||
            state->Rip != rip + (rip == 0x1000 ? 11 : 18) ||
            state->StopReason != Core::GuestStopReason::Returned) return 0;
    }
    return 1;
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
