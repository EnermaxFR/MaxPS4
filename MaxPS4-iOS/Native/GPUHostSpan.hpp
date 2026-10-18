#pragma once
// Host view of one guest span inside a single translated range of a lease
// (which pins mappings while GPU work runs), or null: callers then take the
// checked copy path. Writable views never alias executable memory.
#include <cstdint>
#include <utility>

namespace maxps4::gpu {
template <class Memory>
auto hostSpanOf(const Memory& m, uint64_t guest, uint64_t bytes, uint32_t permissions, int) -> decltype(m.ranges(), (uint8_t*)nullptr) {
  if (!bytes) return nullptr;
  for (const auto& r : m.ranges()) {
    if (guest < r.guest || guest - r.guest >= r.size) continue;
    const uint64_t offset = guest - r.guest;
    if (bytes > r.size - offset || (r.permissions & permissions) != permissions || ((permissions & 2) && (r.permissions & 1))) return nullptr;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(r.host + offset));
  }
  return nullptr;
}
template <class Memory> uint8_t* hostSpanOf(const Memory&, uint64_t, uint64_t, uint32_t, long) { return nullptr; }
template <class Memory> uint8_t* hostSpan(const Memory& m, uint64_t guest, uint64_t bytes, uint32_t permissions) {
  return hostSpanOf(m, guest, bytes, permissions, 0);
}
} // namespace maxps4::gpu
