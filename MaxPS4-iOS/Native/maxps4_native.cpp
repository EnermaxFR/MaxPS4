#include "maxps4_native.h"
#include <cerrno>
#include <cstring>

// A genuine iOS static library and C ABI. Do not expose a game launch
// capability until FEXCore, PS4 HLE and GPU are linked and tested.
extern "C" uint32_t maxps4_native_abi_version(void) { return 1u; }
extern "C" uint32_t maxps4_native_capabilities(void) { return 0u; }
// Actual guest CPU adapter already built from pinned AetherPS4 sources.
extern "C" int maxps4_aether_restricted_cpu_dispatch_probe() noexcept;
extern "C" int32_t maxps4_native_aether_cpu_probe(void) {
    return maxps4_aether_restricted_cpu_dispatch_probe() == 1 ? 1 : 0;
}
extern "C" int32_t maxps4_native_launch(const char *path) {
    if (path == nullptr || *path == '\0') return -22; // invalid argument
    // A real game runner cannot be called until the backend reports capability.
    if ((maxps4_native_capabilities() & 1u) == 0u) return -38;
    return -38; // execution is not implemented, never falsely report success
}
