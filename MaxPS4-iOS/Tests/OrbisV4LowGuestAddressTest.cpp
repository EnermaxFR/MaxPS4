#include "../Native/maxps4_guest_address_space.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cerrno>
int main() {
  maxps4::GuestAddressSpace memory;
  const auto page = memory.page_size();
  const uint64_t guest = 0x400000ULL;
  assert(memory.reserve(guest, 2 * page) == 0);
  const uint8_t code[] = {0xb8,42,0,0,0,0xf4};
  assert(memory.map(guest, page, 5, code, sizeof(code)) == 0);
  assert(memory.map(guest + page, page, 6) == 0);
  const auto* host = memory.translate(guest, sizeof(code), 5);
  assert(host && reinterpret_cast<uintptr_t>(host) != guest);
  uint8_t fetched[sizeof(code)]{};
  assert(memory.read(guest, fetched, sizeof(fetched), true) == 0);
  assert(std::memcmp(fetched, code, sizeof(code)) == 0);
  const uint64_t value = 0x12345678ULL;
  assert(memory.write(guest + page, &value, sizeof(value)) == 0);
  uint64_t actual = 0;
  assert(memory.read(guest + page, &actual, sizeof(actual)) == 0);
  assert(actual == value);
  maxps4::GuestAddressSpace direct(maxps4::GuestAddressSpace::Backend::fex_direct);
  assert(direct.reserve(guest, page) == -ENOTSUP);
  assert(direct.allocated() == 0);
  assert(memory.release(guest) == 0);
  assert(memory.allocated() == 0);
}
