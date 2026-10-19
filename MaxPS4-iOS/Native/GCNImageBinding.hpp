#pragma once
// Host side of GCNKernelImage.h: turns the T# (and S#) a translated MIMG
// instruction reads from the scalar prepass into a GcnImage descriptor, with
// the guest range the GPU backend must make visible as texel bytes. The
// checks are those of GCNShader::imageAccess; in addition, combinations whose
// GPU result could differ from the interpreter are refused (the draw then
// stays on the interpreter): integer texels with bilinear filtering (the GPU
// may flush the denormal bit patterns the interpreter blends), clamp modes the
// interpreter does not implement, and surfaces of 1 GiB or more.
#include "GCNShader.hpp"
#include "GCNKernel.h"
#include "GCNKernelImage.h"
#include <cstdint>
#include <string>

namespace maxps4::gpu {
struct GCNImageBinding {
  GcnImage image{};
  uint64_t guestBase = 0, bytes = 0;   // resource range to expose at image.base

  // t: 8 T# dwords; smp: 4 S# dwords (ignored when !sampled, i.e. image_load).
  bool describe(const uint32_t* t, const uint32_t* smp, bool sampled, std::string& why) {
    GCNShader::Texture tex;
    if (!GCNShader::decodeTexture(t, tex, why)) return false;
    bytes = uint64_t(tex.pitch) * tex.height * GCNShader::texelBytes(tex.dfmt);
    if (tex.tile == 10) {
      if (GCNShader::texelBytes(tex.dfmt) != 4) { why = "tiled image requires 32-bit texels"; return false; }
      if (!tex.layout.init(tex.pitch, tex.height, why)) return false;
      bytes = tex.layout.sizeBytes();
    }
    if (bytes >= (uint64_t(1) << 30)) { why = "image of 1 GiB or more not translated"; return false; }
    guestBase = tex.base;
    image = GcnImage{};
    image.width = tex.width; image.height = tex.height; image.pitch = tex.pitch;
    image.dfmt = tex.dfmt; image.nfmt = tex.nfmt; image.tile = tex.tile;
    for (unsigned c = 0; c < 4; ++c) image.select |= tex.select[c] << (c * 3);
    if (!sampled) return true;
    const unsigned clampX = smp[0] & 7, clampY = (smp[0] >> 3) & 7;
    const unsigned mag = (smp[2] >> 20) & 3, min = (smp[2] >> 22) & 3;
    if ((smp[0] >> 12) & 7) { why = "depth-compare sampler unsupported"; return false; }
    if (mag != min || mag > 1) { why = "sampler filter combination unsupported (point/bilinear only)"; return false; }
    auto known = [](unsigned m) { return m == 0 || m == 1 || m == 2 || m == 3 || m == 6; };
    if (!known(clampX) || !known(clampY)) { why = "sampler clamp mode unsupported"; return false; }
    if (mag == 1 && tex.nfmt == 4) { why = "integer image with bilinear filtering not translated"; return false; }
    image.clampX = clampX; image.clampY = clampY; image.filter = mag;
    image.forceUnnorm = (smp[0] >> 15) & 1;
    return true;
  }
};
} // namespace maxps4::gpu
