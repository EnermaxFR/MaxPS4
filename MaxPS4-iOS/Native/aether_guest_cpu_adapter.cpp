// MaxPS4 thin ABI adapter for real AetherPS4 guest-CPU abstraction.
// Upstream guest_cpu.{h,cpp}: MIT, Leviidev/AetherPS4 @ 3347fc13...
// This is an integration smoke test, NOT an x86-64 -> ARM64 JIT.
#include "core/guest_cpu/guest_cpu.h"
#include <cerrno>
#include <variant>

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
    return 1;
}
