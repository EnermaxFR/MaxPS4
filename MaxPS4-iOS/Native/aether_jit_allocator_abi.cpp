// SPDX-License-Identifier: GPL-2.0-or-later
// ABI-only check for the upstream AetherPS4 iOS JIT allocator.
// Does not create pages, execute BRK, or imply executable memory.
#include "core/ios/ios_jit_allocator.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
static_assert(!std::is_copy_constructible_v<Core::DualMappedRegion>);
static_assert(std::is_move_constructible_v<Core::DualMappedRegion>);
static_assert(sizeof(Core::DualMappedRegion) == sizeof(void*) * 2 + sizeof(std::size_t));
extern "C" int maxps4_aether_jit_allocator_abi_probe() noexcept {
    // No DualMappedRegion instance: its destructor requires the actual allocator.
    return sizeof(Core::DualMappedRegion) == 24 ? 1 : 0;
}
#else
extern "C" int maxps4_aether_jit_allocator_abi_probe() noexcept { return 0; }
#endif
