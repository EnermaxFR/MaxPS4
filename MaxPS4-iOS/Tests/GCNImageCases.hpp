#pragma once
// Image sampling cases shared by GCNKernelImageTest (C++ vs the GCNShader
// interpreter) and GCNTranslatorMetal (GPU vs C++): random T#/S# within what
// GCNImageBinding accepts, over a pool of random guest resources, each with
// 64 lane coordinates (edges, out of range, NaN/inf, huge values; no
// denormal coordinates, see GCNKernelImage.h).
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gcnimg {
struct Case {
  uint32_t t[8] = {}, smp[4] = {}, w0 = 0;
  std::array<std::array<uint32_t, 2>, 64> coords{};
};
struct Suite {
  uint64_t base = 0;                 // guest address of bytes[0]
  std::vector<uint8_t> bytes;        // resource pool
  std::vector<Case> cases;
};

constexpr unsigned regions = 12, regionBytes = 256 * 1024;

inline Suite make(unsigned count, uint64_t seed, uint64_t base = 0x10000000) {
  Suite s; s.base = base; s.bytes.resize(size_t(regions) * regionBytes);
  uint64_t state = seed;
  auto next = [&] { state = state * 6364136223846793005ull + 1442695040888963407ull; return uint32_t(state >> 32); };
  auto bits = [](float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; };
  // Bytes: uniform random, with runs of zeros, 0xff and float-looking words.
  for (size_t k = 0; k + 4 <= s.bytes.size(); k += 4) {
    uint32_t w = next();
    switch (next() % 8) {
    case 0: w = 0; break;
    case 1: w = 0xffffffffu; break;
    case 2: w = bits(float(int(next() % 20001) - 10000) / 997.0f); break;
    case 3: w = (w & 0x807fffffu) | ((96u + next() % 64u) << 23); break;   // normal floats of varied magnitude
    default: break;
    }
    std::memcpy(s.bytes.data() + k, &w, 4);
  }
  for (unsigned n = 0; n < count; ++n) {
    Case c;
    const unsigned region = next() % regions;
    const uint64_t address = base + uint64_t(region) * regionBytes;
    static const unsigned dfmts[4] = {10, 1, 4, 14};
    unsigned dfmt = dfmts[next() % 4], nfmt;
    const unsigned r = next() % 4;
    if (dfmt == 4 || dfmt == 14) nfmt = r == 0 ? 0 : r == 1 ? 4 : 7;
    else nfmt = r == 0 ? 4 : r == 1 ? 9 : 0;
    if (dfmt == 14) nfmt = r == 0 ? 0 : r == 1 ? 4 : 7;   // 32_32_32_32: raw words whatever the number format
    const bool tiled = (dfmt == 10 || dfmt == 4) && next() % 3 == 0;
    unsigned width = 1 + next() % 70, height = 1 + next() % 70, pitch;
    if (tiled) { pitch = 128 * (1 + next() % 2); width = 1 + next() % pitch; height = 1 + next() % 120; }
    else pitch = width + next() % 26;
    unsigned select = 0;
    for (unsigned k = 0; k < 4; ++k) {
      const unsigned roll = next() % 8;
      select |= (roll < 5 ? 4 + (roll < 4 ? k : next() % 4) : roll == 5 ? 1 : 0) << (3 * k);
    }
    c.t[0] = uint32_t(address >> 8); c.t[1] = uint32_t(address >> 40) | dfmt << 20 | nfmt << 26;
    c.t[2] = (width - 1) | (height - 1) << 14;
    c.t[3] = select | (tiled ? 10u : 8u) << 20 | 9u << 28;
    c.t[4] = (pitch - 1) << 13;
    static const unsigned ops[4] = {0, 32, 36, 39};
    const unsigned op = ops[next() % 4];
    static const unsigned modes[5] = {0, 1, 2, 3, 6};
    const unsigned filter = next() % 2, forceUnnorm = next() % 6 == 0, unorm = op != 0 && next() % 3 == 0;
    c.smp[0] = modes[next() % 5] | modes[next() % 5] << 3 | forceUnnorm << 15;
    c.smp[2] = filter << 20 | filter << 22;
    c.w0 = 0xf0000000u | op << 18 | unorm << 12 | 15u << 8;
    const bool unnormalized = unorm || forceUnnorm;
    for (unsigned lane = 0; lane < 64; ++lane) {
      for (unsigned axis = 0; axis < 2; ++axis) {
        const unsigned size = axis ? height : width;
        uint32_t value;
        if (op == 0) {
          value = next() % 16 == 0 ? next() : next() % (size + 4);
        } else {
          const unsigned kind = next() % 16;
          float f;
          if (kind == 0) { static const uint32_t special[6] = {0x7fc00000u, 0x7f800000u, 0xff800000u, 0x80000000u, 0x4e6e6b28u, 0xce6e6b28u}; value = special[next() % 6]; c.coords[lane][axis] = value; continue; }
          if (kind == 1) f = unnormalized ? float(next() % (size + 1)) + 0.5f * float(next() % 2) : (float(next() % (size + 1)) + 0.5f) / float(size);
          else if (unnormalized) f = float(int(next() % (5 * size * 64 + 1)) - int(2 * size * 64)) / 64.0f;
          else f = float(int(next() % 40001) - 15000) / 10000.0f;
          value = bits(f);
        }
        c.coords[lane][axis] = value;
      }
    }
    s.cases.push_back(c);
  }
  return s;
}
} // namespace gcnimg
