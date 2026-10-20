// GCNKernelImage.h (and GCNImageBinding) against GCNShader::imageAccess: the
// same MIMG instruction, T#, S#, coordinates and guest memory, 64 lanes per
// case; every result channel must be equal bit for bit. Also checks the
// prelude tables and the tile-10 address against their host definitions.
#include "../Native/GCNShader.hpp"
#include "../Native/GCNImageBinding.hpp"
#include "GCNImageCases.hpp"
#include <cstdio>
#include <cstdlib>

using namespace maxps4::gpu;

struct TestMemory {
  uint64_t base; std::vector<uint8_t> bytes;
  int read_data(uint64_t at, void* out, size_t n, bool = false) const {
    if (at < base || at - base + n > bytes.size()) return -1;
    std::memcpy(out, bytes.data() + (at - base), n); return 0;
  }
  int write_data(uint64_t at, const void* in, size_t n) {
    if (at < base || at - base + n > bytes.size()) return -1;
    std::memcpy(bytes.data() + (at - base), in, n); return 0;
  }
};

static bool tables() {
  for (unsigned b = 0; b < 256; ++b) {
    if (gcn_srgb_to_linear[b] != gcnBits(srgbByteToLinear(uint8_t(b)))) { std::fprintf(stderr, "sRGB table %u\n", b); return false; }
    if (gcn_unorm8[b] != gcnBits(b / 255.0f)) { std::fprintf(stderr, "unorm8 table %u\n", b); return false; }
    if (b < 32 && gcn_unorm5[b] != gcnBits(float(b) / 31.0f)) { std::fprintf(stderr, "unorm5 table %u\n", b); return false; }
    if (b < 64 && gcn_unorm6[b] != gcnBits(float(b) / 63.0f)) { std::fprintf(stderr, "unorm6 table %u\n", b); return false; }
  }
  for (uint32_t pitch : {128u, 256u, 1024u, 16384u}) {
    Display2DThin32 layout; std::string why;
    if (!layout.init(pitch, 300, why)) return false;
    for (uint32_t y = 0; y < 300; y += 1 + y / 7) for (uint32_t x = 0; x < pitch; x += 1 + x / 5)
      if (layout.offset(x, y) != gcn_tile10_offset(pitch, x, y)) { std::fprintf(stderr, "tile10 %u %u %u\n", pitch, x, y); return false; }
  }
  return true;
}

int main(int argc, char** argv) {
  if (!tables()) return 1;
  const unsigned count = argc > 1 ? unsigned(std::atoi(argv[1])) : 6000;
  const gcnimg::Suite suite = gcnimg::make(count, 0x9e3779b97f4a7c15ull);
  const uint64_t program = suite.base - 0x1000;
  TestMemory m{program, std::vector<uint8_t>(0x1000)};
  m.bytes.insert(m.bytes.end(), suite.bytes.begin(), suite.bytes.end());
  unsigned compared = 0, refused = 0, perOp[4] = {}, perFilter[2] = {}, tiled = 0;
  for (size_t n = 0; n < suite.cases.size(); ++n) {
    const auto& c = suite.cases[n];
    const unsigned op = (c.w0 >> 18) & 127;
    GCNImageBinding binding; std::string why;
    if (!binding.describe(c.t, c.smp, op != 0, why)) {
      if (why != "integer image with bilinear filtering not translated") { std::fprintf(stderr, "case %zu: refused: %s\n", n, why.c_str()); return 1; }
      ++refused; continue;
    }
    binding.image.base = uint32_t(binding.guestBase - suite.base);
    // Interpreter: one MIMG (rsrc s[0:7], sampler s[8:11], address v0..v2, data v4..v7).
    const uint32_t code[3] = {c.w0, 0u | 4u << 8 | 0u << 16 | 2u << 21, 0xbf810000u};
    std::memcpy(m.bytes.data(), code, sizeof(code));
    GCNShader ref; GCNShader::Wave wave; wave.configure(16, 8); wave.setExec(~0ull);
    for (unsigned k = 0; k < 8; ++k) wave.s[k] = c.t[k];
    for (unsigned k = 0; k < 4; ++k) wave.s[8 + k] = c.smp[k];
    for (unsigned lane = 0; lane < 64; ++lane) { wave.v[0][lane] = c.coords[lane][0]; wave.v[1][lane] = c.coords[lane][1]; wave.v[2][lane] = 0; }
    if (!ref.run(m, program, wave, why)) { std::fprintf(stderr, "case %zu: interpreter: %s\n", n, why.c_str()); return 1; }
    const unsigned char* texels = m.bytes.data() + 0x1000;
    for (unsigned lane = 0; lane < 64; ++lane) {
      const gcn_u32 a[4] = {c.coords[lane][0], c.coords[lane][1], 0u, 0u};
      gcn_u32 t[4];
      gcn_image(binding.image, texels, c.w0, a, t);
      for (unsigned k = 0; k < 4; ++k) {
        if (t[k] != wave.v[4 + k][lane]) {
          std::fprintf(stderr, "case %zu lane %u channel %u: kernel %08x, interpreter %08x (op %u dfmt %u nfmt %u tile %u filter %u clamp %u/%u coords %08x %08x)\n",
                       n, lane, k, t[k], wave.v[4 + k][lane], op, binding.image.dfmt, binding.image.nfmt, binding.image.tile,
                       binding.image.filter, binding.image.clampX, binding.image.clampY, a[0], a[1]);
          return 1;
        }
        ++compared;
      }
    }
    ++perOp[op == 0 ? 0 : op == 32 ? 1 : op == 36 ? 2 : 3];
    if (op) ++perFilter[binding.image.filter];
    tiled += binding.image.tile == 10;
  }
  std::printf("PASS GCN kernel image sampling: %u channels equal to the interpreter over %zu cases "
              "(load %u, sample %u, sample_l %u, sample_lz %u; point %u, bilinear %u; tiled %u; %u refused as integer+bilinear)\n",
              compared, suite.cases.size() - refused, perOp[0], perOp[1], perOp[2], perOp[3], perFilter[0], perFilter[1], tiled, refused);
}
