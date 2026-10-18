#pragma once
// Surface addressing for the base PS4 (Liverpool, GCN gfx7) tiled layouts.
//
// Display 2D-thin, 32 bits per element (GB_TILE_MODE index 10, the layout of
// VideoOut "tiled" scan-out buffers and of render targets bound with
// CB_COLOR*_ATTRIB.TILE_MODE_INDEX=10). Parameters of the base console:
// ARRAY_2D_TILED_THIN1, displayable micro tiling, PIPE_CONFIG
// P8_32x32_16x16 (8 pipes), 16 banks, bank width/height 1, macro tile aspect
// 2, 256-byte pipe interleave. The address equations follow AMD's public
// addrlib (SiLib::ComputeSurfaceAddrFromCoordMacroTiled, pipe/bank swizzle
// for P8_32x32_16x16 and 16 banks, displayable 32bpp micro-tile element
// order). Unsupported parameters are rejected rather than approximated.
#include <cstdint>
#include <string>

namespace maxps4::gpu {
struct Display2DThin32 {
  static constexpr unsigned microTile = 8, numPipes = 8, numBanks = 16,
                            pipeBits = 3, bankBits = 4, pipeInterleave = 256,
                            macroWidth = 128, macroHeight = 64,
                            microTileBytes = microTile * microTile * 4;
  uint32_t pitch = 0, paddedHeight = 0;
  // pitch and height in elements; height is padded to whole macro tiles.
  bool init(uint32_t pitchElements, uint32_t height, std::string& error) {
    if (!pitchElements || !height || pitchElements % macroWidth || pitchElements > 16384 || height > 16384) {
      error = "display 2D-thin surface requires a pitch multiple of 128 elements";
      return false;
    }
    pitch = pitchElements;
    paddedHeight = (height + macroHeight - 1) / macroHeight * macroHeight;
    return true;
  }
  uint64_t sizeBytes() const { return uint64_t(pitch) * paddedHeight * 4; }
  static uint32_t bit(uint32_t v, unsigned n) { return (v >> n) & 1; }
  // Displayable micro-tile element index for 32 bpp.
  static uint32_t element(uint32_t x, uint32_t y) {
    return bit(x, 0) | (bit(x, 1) << 1) | (bit(y, 0) << 2) | (bit(x, 2) << 3) |
           (bit(y, 1) << 4) | (bit(y, 2) << 5);
  }
  static uint32_t pipe(uint32_t x, uint32_t y) {
    return (bit(x, 3) ^ bit(y, 3) ^ bit(x, 4)) | ((bit(x, 4) ^ bit(y, 4)) << 1) |
           ((bit(x, 5) ^ bit(y, 5)) << 2);
  }
  static uint32_t bank(uint32_t x, uint32_t y) {
    const uint32_t tx = x / microTile / numPipes, ty = y / microTile; // bank width/height 1
    return (bit(tx, 0) ^ bit(ty, 3)) | ((bit(tx, 1) ^ bit(ty, 2) ^ bit(ty, 3)) << 1) |
           ((bit(tx, 2) ^ bit(ty, 1)) << 2) | ((bit(tx, 3) ^ bit(ty, 0)) << 3);
  }
  // Byte offset of element (x, y) from the surface base (slice 0).
  uint64_t offset(uint32_t x, uint32_t y) const {
    const uint64_t elementOffset = uint64_t(element(x, y)) * 4;
    const uint64_t macroBytes = uint64_t(macroWidth) * macroHeight * 4 / (numPipes * numBanks);
    const uint64_t macroIndex = uint64_t(y / macroHeight) * (pitch / macroWidth) + x / macroWidth;
    // Bank width/height 1: one micro tile per pipe/bank in a macro tile.
    const uint64_t total = macroIndex * macroBytes + elementOffset;
    return (total & (pipeInterleave - 1)) | (uint64_t(pipe(x, y)) << 8) |
           (uint64_t(bank(x, y)) << (8 + pipeBits)) |
           ((total >> 8) << (8 + pipeBits + bankBits));
  }
};
} // namespace maxps4::gpu
