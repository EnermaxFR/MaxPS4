#pragma once
#include <cstddef>
#include <cstdint>

namespace maxps4::ps4 {
// FreeBSD/Orbis errno numbers, independent of the host's errno namespace.
enum Error : uint32_t {
    ok = 0, perm = 1, noent = 2, srch = 3, intr = 4, io = 5, badf = 9,
    deadlk = 11, nomem = 12, access = 13, fault = 14, busy = 16,
    inval = 22, mfile = 24, rofs = 30, again = 35, notsup = 45, timedout = 60,
    loop = 62, nametoolong = 63, nosys = 78, overflow = 84,
    notdir = 20, isdir = 21
};
constexpr int64_t kernel_error(Error e) {
    return e == ok ? 0 : -static_cast<int64_t>(0x7ffe0000U - uint32_t(e));
}
Error host_error(int error);
inline void put_le(uint8_t* bytes, size_t offset, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) bytes[offset + i] = uint8_t(value >> (i * 8));
}
inline uint64_t get_le(const uint8_t* bytes, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) value |= uint64_t(bytes[i]) << (i * 8);
    return value;
}
// Bit-preserving conversion of a guest signed 64-bit register without relying
// on implementation-defined unsigned-to-signed conversion on ARM64.
inline int64_t signed64(uint64_t bits) {
    return bits <= INT64_MAX ? int64_t(bits) : -1 - int64_t(~bits);
}
inline int32_t signed32(uint64_t bits) {
    const uint32_t low = uint32_t(bits);
    return low <= INT32_MAX ? int32_t(low) : -1 - int32_t(~low);
}
} // namespace maxps4::ps4
