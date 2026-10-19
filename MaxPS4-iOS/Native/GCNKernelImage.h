// Dual-language (C++17 / Metal) image sampling for translated GCN shaders,
// equal bit for bit to GCNShader::imageAccess for the resources that
// GCNImageBinding accepts: 2D single-level images, linear-aligned (tile 8)
// or display 2D-thin 32 bpp (tile 10), data formats 8_8_8_8, 8, 32 and
// 32_32_32_32, point or bilinear filtering, wrap/mirror/clamp/border modes.
// The texels are read from a byte buffer (the guest resource as is, no
// conversion upload): `img.base` is the byte offset of the resource in it.
// Requires GCNKernel.h. Conversions use exact host tables (sRGB, unorm8) so
// that no GPU division or pow is involved.
#ifndef MAXPS4_GCN_KERNEL_IMAGE_H
#define MAXPS4_GCN_KERNEL_IMAGE_H

#ifdef __METAL_VERSION__
#define GCN_DEVICE device
#define GCN_TABLE constant
#else
#define GCN_DEVICE
#define GCN_TABLE static const
#endif

// Uniform for a draw; built on the host by GCNImageBinding::describe. Passed by
// value, so it may live in any address space (constant or device buffer).
struct GcnImage {
  gcn_u32 base, width, height, pitch, dfmt, nfmt, tile, select;   // select: 4 x 3 bits (T# dst_sel)
  gcn_u32 clampX, clampY, filter, forceUnnorm;                       // filter: 0 point, 1 bilinear
};

// srgbByteToLinear(b) of the host (GCNShader.hpp), as bit patterns.
GCN_TABLE gcn_u32 gcn_srgb_to_linear[256] = {
  0x00000000u, 0x399f22b4u, 0x3a1f22b4u, 0x3a6eb40eu, 0x3a9f22b4u, 0x3ac6eb61u, 0x3aeeb40eu, 0x3b0b3e5du,
  0x3b1f22b4u, 0x3b33070bu, 0x3b46eb61u, 0x3b5b518du, 0x3b70f18du, 0x3b83e1c6u, 0x3b8fe616u, 0x3b9c87fdu,
  0x3ba9c9b7u, 0x3bb7ad6fu, 0x3bc63549u, 0x3bd56361u, 0x3be539c1u, 0x3bf5ba70u, 0x3c0373b5u, 0x3c0c6152u,
  0x3c15a703u, 0x3c1f45beu, 0x3c293e6bu, 0x3c3391f7u, 0x3c3e4149u, 0x3c494d43u, 0x3c54b6c7u, 0x3c607eb1u,
  0x3c6ca5dfu, 0x3c792d22u, 0x3c830aa8u, 0x3c89af9fu, 0x3c9085dbu, 0x3c978dc5u, 0x3c9ec7c2u, 0x3ca63433u,
  0x3cadd37du, 0x3cb5a601u, 0x3cbdac20u, 0x3cc5e639u, 0x3cce54abu, 0x3cd6f7d5u, 0x3cdfd010u, 0x3ce8ddb9u,
  0x3cf2212cu, 0x3cfb9ac1u, 0x3d02a569u, 0x3d0798dcu, 0x3d0ca7e6u, 0x3d11d2afu, 0x3d171963u, 0x3d1c7c2eu,
  0x3d21fb3cu, 0x3d2796b2u, 0x3d2d4ebbu, 0x3d332380u, 0x3d39152bu, 0x3d3f23e3u, 0x3d454fd1u, 0x3d4b991cu,
  0x3d51ffefu, 0x3d58846au, 0x3d5f26b7u, 0x3d65e6feu, 0x3d6cc564u, 0x3d73c20fu, 0x3d7add29u, 0x3d810b67u,
  0x3d84b795u, 0x3d887330u, 0x3d8c3e4au, 0x3d9018f6u, 0x3d940345u, 0x3d97fd4au, 0x3d9c0716u, 0x3da020bbu,
  0x3da44a4bu, 0x3da883d7u, 0x3daccd70u, 0x3db12728u, 0x3db59112u, 0x3dba0b3bu, 0x3dbe95b5u, 0x3dc33092u,
  0x3dc7dbe2u, 0x3dcc97b6u, 0x3dd1641fu, 0x3dd6412cu, 0x3ddb2eefu, 0x3de02d77u, 0x3de53cd5u, 0x3dea5d19u,
  0x3def8e52u, 0x3df4d091u, 0x3dfa23e8u, 0x3dff8861u, 0x3e027f07u, 0x3e054280u, 0x3e080ea3u, 0x3e0ae378u,
  0x3e0dc105u, 0x3e10a754u, 0x3e13966bu, 0x3e168e52u, 0x3e198f10u, 0x3e1c98adu, 0x3e1fab30u, 0x3e22c6a3u,
  0x3e25eb09u, 0x3e29186cu, 0x3e2c4ed0u, 0x3e2f8e41u, 0x3e32d6c4u, 0x3e362861u, 0x3e39831eu, 0x3e3ce703u,
  0x3e405416u, 0x3e43ca5fu, 0x3e4749e4u, 0x3e4ad2aeu, 0x3e4e64c2u, 0x3e520027u, 0x3e55a4e6u, 0x3e595303u,
  0x3e5d0a8bu, 0x3e60cb7cu, 0x3e6495e0u, 0x3e6869bfu, 0x3e6c4720u, 0x3e702e0cu, 0x3e741e84u, 0x3e781890u,
  0x3e7c1c38u, 0x3e8014c2u, 0x3e82203cu, 0x3e84308du, 0x3e8645bau, 0x3e885fc5u, 0x3e8a7eb2u, 0x3e8ca283u,
  0x3e8ecb3du, 0x3e90f8e1u, 0x3e932b74u, 0x3e9562f8u, 0x3e979f71u, 0x3e99e0e2u, 0x3e9c274eu, 0x3e9e72b7u,
  0x3ea0c322u, 0x3ea31892u, 0x3ea57308u, 0x3ea7d289u, 0x3eaa3718u, 0x3eaca0b7u, 0x3eaf0f69u, 0x3eb18333u,
  0x3eb3fc18u, 0x3eb67a18u, 0x3eb8fd37u, 0x3ebb8579u, 0x3ebe12e1u, 0x3ec0a571u, 0x3ec33d2du, 0x3ec5da17u,
  0x3ec87c33u, 0x3ecb2383u, 0x3ecdd00bu, 0x3ed081cdu, 0x3ed338ccu, 0x3ed5f50bu, 0x3ed8b68du, 0x3edb7d54u,
  0x3ede4965u, 0x3ee11ac1u, 0x3ee3f16bu, 0x3ee6cd67u, 0x3ee9aeb6u, 0x3eec955du, 0x3eef815du, 0x3ef272bau,
  0x3ef56976u, 0x3ef86594u, 0x3efb6717u, 0x3efe6e02u, 0x3f00bd2du, 0x3f02460eu, 0x3f03d1a7u, 0x3f055ff9u,
  0x3f06f106u, 0x3f0884cfu, 0x3f0a1b56u, 0x3f0bb49bu, 0x3f0d50a0u, 0x3f0eef67u, 0x3f1090f1u, 0x3f12353eu,
  0x3f13dc51u, 0x3f15862bu, 0x3f1732cdu, 0x3f18e239u, 0x3f1a946fu, 0x3f1c4971u, 0x3f1e0141u, 0x3f1fbbdfu,
  0x3f21794eu, 0x3f23398eu, 0x3f24fca0u, 0x3f26c286u, 0x3f288b41u, 0x3f2a56d3u, 0x3f2c253du, 0x3f2df680u,
  0x3f2fca9eu, 0x3f31a197u, 0x3f337b6cu, 0x3f355820u, 0x3f3737b3u, 0x3f391a26u, 0x3f3aff7cu, 0x3f3ce7b5u,
  0x3f3ed2d2u, 0x3f40c0d4u, 0x3f42b1beu, 0x3f44a590u, 0x3f469c4bu, 0x3f4895f1u, 0x3f4a9282u, 0x3f4c9201u,
  0x3f4e946eu, 0x3f5099cbu, 0x3f52a218u, 0x3f54ad57u, 0x3f56bb8au, 0x3f58ccb0u, 0x3f5ae0cdu, 0x3f5cf7e0u,
  0x3f5f11ecu, 0x3f612eeeu, 0x3f634eefu, 0x3f6571e9u, 0x3f6797e3u, 0x3f69c0d6u, 0x3f6beccdu, 0x3f6e1bbfu,
  0x3f704db8u, 0x3f7282afu, 0x3f74baaeu, 0x3f76f5aeu, 0x3f7933b9u, 0x3f7b74c6u, 0x3f7db8e0u, 0x3f800000u
};
// float(b) / 255.0f, correctly rounded, as bit patterns.
GCN_TABLE gcn_u32 gcn_unorm8[256] = {
  0x00000000u, 0x3b808081u, 0x3c008081u, 0x3c40c0c1u, 0x3c808081u, 0x3ca0a0a1u, 0x3cc0c0c1u, 0x3ce0e0e1u,
  0x3d008081u, 0x3d109091u, 0x3d20a0a1u, 0x3d30b0b1u, 0x3d40c0c1u, 0x3d50d0d1u, 0x3d60e0e1u, 0x3d70f0f1u,
  0x3d808081u, 0x3d888889u, 0x3d909091u, 0x3d989899u, 0x3da0a0a1u, 0x3da8a8a9u, 0x3db0b0b1u, 0x3db8b8b9u,
  0x3dc0c0c1u, 0x3dc8c8c9u, 0x3dd0d0d1u, 0x3dd8d8d9u, 0x3de0e0e1u, 0x3de8e8e9u, 0x3df0f0f1u, 0x3df8f8f9u,
  0x3e008081u, 0x3e048485u, 0x3e088889u, 0x3e0c8c8du, 0x3e109091u, 0x3e149495u, 0x3e189899u, 0x3e1c9c9du,
  0x3e20a0a1u, 0x3e24a4a5u, 0x3e28a8a9u, 0x3e2cacadu, 0x3e30b0b1u, 0x3e34b4b5u, 0x3e38b8b9u, 0x3e3cbcbdu,
  0x3e40c0c1u, 0x3e44c4c5u, 0x3e48c8c9u, 0x3e4ccccdu, 0x3e50d0d1u, 0x3e54d4d5u, 0x3e58d8d9u, 0x3e5cdcddu,
  0x3e60e0e1u, 0x3e64e4e5u, 0x3e68e8e9u, 0x3e6cecedu, 0x3e70f0f1u, 0x3e74f4f5u, 0x3e78f8f9u, 0x3e7cfcfdu,
  0x3e808081u, 0x3e828283u, 0x3e848485u, 0x3e868687u, 0x3e888889u, 0x3e8a8a8bu, 0x3e8c8c8du, 0x3e8e8e8fu,
  0x3e909091u, 0x3e929293u, 0x3e949495u, 0x3e969697u, 0x3e989899u, 0x3e9a9a9bu, 0x3e9c9c9du, 0x3e9e9e9fu,
  0x3ea0a0a1u, 0x3ea2a2a3u, 0x3ea4a4a5u, 0x3ea6a6a7u, 0x3ea8a8a9u, 0x3eaaaaabu, 0x3eacacadu, 0x3eaeaeafu,
  0x3eb0b0b1u, 0x3eb2b2b3u, 0x3eb4b4b5u, 0x3eb6b6b7u, 0x3eb8b8b9u, 0x3ebababbu, 0x3ebcbcbdu, 0x3ebebebfu,
  0x3ec0c0c1u, 0x3ec2c2c3u, 0x3ec4c4c5u, 0x3ec6c6c7u, 0x3ec8c8c9u, 0x3ecacacbu, 0x3ecccccdu, 0x3ecececfu,
  0x3ed0d0d1u, 0x3ed2d2d3u, 0x3ed4d4d5u, 0x3ed6d6d7u, 0x3ed8d8d9u, 0x3edadadbu, 0x3edcdcddu, 0x3edededfu,
  0x3ee0e0e1u, 0x3ee2e2e3u, 0x3ee4e4e5u, 0x3ee6e6e7u, 0x3ee8e8e9u, 0x3eeaeaebu, 0x3eececedu, 0x3eeeeeefu,
  0x3ef0f0f1u, 0x3ef2f2f3u, 0x3ef4f4f5u, 0x3ef6f6f7u, 0x3ef8f8f9u, 0x3efafafbu, 0x3efcfcfdu, 0x3efefeffu,
  0x3f008081u, 0x3f018182u, 0x3f028283u, 0x3f038384u, 0x3f048485u, 0x3f058586u, 0x3f068687u, 0x3f078788u,
  0x3f088889u, 0x3f09898au, 0x3f0a8a8bu, 0x3f0b8b8cu, 0x3f0c8c8du, 0x3f0d8d8eu, 0x3f0e8e8fu, 0x3f0f8f90u,
  0x3f109091u, 0x3f119192u, 0x3f129293u, 0x3f139394u, 0x3f149495u, 0x3f159596u, 0x3f169697u, 0x3f179798u,
  0x3f189899u, 0x3f19999au, 0x3f1a9a9bu, 0x3f1b9b9cu, 0x3f1c9c9du, 0x3f1d9d9eu, 0x3f1e9e9fu, 0x3f1f9fa0u,
  0x3f20a0a1u, 0x3f21a1a2u, 0x3f22a2a3u, 0x3f23a3a4u, 0x3f24a4a5u, 0x3f25a5a6u, 0x3f26a6a7u, 0x3f27a7a8u,
  0x3f28a8a9u, 0x3f29a9aau, 0x3f2aaaabu, 0x3f2babacu, 0x3f2cacadu, 0x3f2dadaeu, 0x3f2eaeafu, 0x3f2fafb0u,
  0x3f30b0b1u, 0x3f31b1b2u, 0x3f32b2b3u, 0x3f33b3b4u, 0x3f34b4b5u, 0x3f35b5b6u, 0x3f36b6b7u, 0x3f37b7b8u,
  0x3f38b8b9u, 0x3f39b9bau, 0x3f3ababbu, 0x3f3bbbbcu, 0x3f3cbcbdu, 0x3f3dbdbeu, 0x3f3ebebfu, 0x3f3fbfc0u,
  0x3f40c0c1u, 0x3f41c1c2u, 0x3f42c2c3u, 0x3f43c3c4u, 0x3f44c4c5u, 0x3f45c5c6u, 0x3f46c6c7u, 0x3f47c7c8u,
  0x3f48c8c9u, 0x3f49c9cau, 0x3f4acacbu, 0x3f4bcbccu, 0x3f4ccccdu, 0x3f4dcdceu, 0x3f4ececfu, 0x3f4fcfd0u,
  0x3f50d0d1u, 0x3f51d1d2u, 0x3f52d2d3u, 0x3f53d3d4u, 0x3f54d4d5u, 0x3f55d5d6u, 0x3f56d6d7u, 0x3f57d7d8u,
  0x3f58d8d9u, 0x3f59d9dau, 0x3f5adadbu, 0x3f5bdbdcu, 0x3f5cdcddu, 0x3f5ddddeu, 0x3f5ededfu, 0x3f5fdfe0u,
  0x3f60e0e1u, 0x3f61e1e2u, 0x3f62e2e3u, 0x3f63e3e4u, 0x3f64e4e5u, 0x3f65e5e6u, 0x3f66e6e7u, 0x3f67e7e8u,
  0x3f68e8e9u, 0x3f69e9eau, 0x3f6aeaebu, 0x3f6bebecu, 0x3f6cecedu, 0x3f6dedeeu, 0x3f6eeeefu, 0x3f6feff0u,
  0x3f70f0f1u, 0x3f71f1f2u, 0x3f72f2f3u, 0x3f73f3f4u, 0x3f74f4f5u, 0x3f75f5f6u, 0x3f76f6f7u, 0x3f77f7f8u,
  0x3f78f8f9u, 0x3f79f9fau, 0x3f7afafbu, 0x3f7bfbfcu, 0x3f7cfcfdu, 0x3f7dfdfeu, 0x3f7efeffu, 0x3f800000u
};

GCN_INLINE gcn_u32 gcn_bitof(gcn_u32 v, gcn_u32 n) { return (v >> n) & 1u; }
// Display2DThin32::offset (GCNTiling.hpp) with 32-bit arithmetic (surfaces <= 1 GiB).
GCN_INLINE gcn_u32 gcn_tile10_offset(gcn_u32 pitch, gcn_u32 x, gcn_u32 y) {
  const gcn_u32 element = gcn_bitof(x, 0) | (gcn_bitof(x, 1) << 1) | (gcn_bitof(y, 0) << 2) | (gcn_bitof(x, 2) << 3) |
                          (gcn_bitof(y, 1) << 4) | (gcn_bitof(y, 2) << 5);
  const gcn_u32 pipe = (gcn_bitof(x, 3) ^ gcn_bitof(y, 3) ^ gcn_bitof(x, 4)) | ((gcn_bitof(x, 4) ^ gcn_bitof(y, 4)) << 1) |
                       ((gcn_bitof(x, 5) ^ gcn_bitof(y, 5)) << 2);
  const gcn_u32 tx = x / 64u, ty = y / 8u;
  const gcn_u32 bank = (gcn_bitof(tx, 0) ^ gcn_bitof(ty, 3)) | ((gcn_bitof(tx, 1) ^ gcn_bitof(ty, 2) ^ gcn_bitof(ty, 3)) << 1) |
                       ((gcn_bitof(tx, 2) ^ gcn_bitof(ty, 1)) << 2) | ((gcn_bitof(tx, 3) ^ gcn_bitof(ty, 0)) << 3);
  const gcn_u32 total = ((y / 64u) * (pitch / 128u) + x / 128u) * 256u + element * 4u;
  return (total & 255u) | (pipe << 8) | (bank << 11) | ((total >> 8) << 15);
}

GCN_INLINE gcn_u32 gcn_texel_word(GCN_DEVICE const unsigned char* p) {
  return gcn_u32(p[0]) | (gcn_u32(p[1]) << 8) | (gcn_u32(p[2]) << 16) | (gcn_u32(p[3]) << 24);
}
// GCNShader::texel: decoded, swizzled channels as float bit patterns.
GCN_INLINE void gcn_texel(const GcnImage img, GCN_DEVICE const unsigned char* texels, gcn_u32 x, gcn_u32 y, GCN_THREAD gcn_u32* out) {
  const gcn_u32 bpp = img.dfmt == 1u ? 1u : img.dfmt == 14u ? 16u : 4u;
  const gcn_u32 offset = img.tile == 8u ? (y * img.pitch + x) * bpp : gcn_tile10_offset(img.pitch, x, y);
  GCN_DEVICE const unsigned char* p = texels + img.base + offset;
  gcn_u32 c[4] = {0u, 0u, 0u, 0x3f800000u};
  if (img.dfmt == 10u) {
    for (gcn_u32 k = 0; k < 4u; ++k)
      c[k] = img.nfmt == 4u ? gcn_u32(p[k]) : (img.nfmt == 9u && k < 3u) ? gcn_srgb_to_linear[p[k]] : gcn_unorm8[p[k]];
  } else if (img.dfmt == 1u) {
    c[0] = img.nfmt == 4u ? gcn_u32(p[0]) : gcn_unorm8[p[0]];
  } else if (img.dfmt == 4u) {
    const gcn_u32 v = gcn_texel_word(p);
    c[0] = (img.nfmt == 7u || img.nfmt == 4u) ? v : gcn_b(float(v) * 2.3283064365386963e-10f); // / 4294967295.0f == / 2^32
  } else {
    for (gcn_u32 k = 0; k < 4u; ++k) c[k] = gcn_texel_word(p + 4u * k);
  }
  for (gcn_u32 k = 0; k < 4u; ++k) {
    const gcn_u32 sel = (img.select >> (k * 3u)) & 7u;
    out[k] = sel >= 4u ? c[sel - 4u] : sel == 1u ? 0x3f800000u : 0u;
  }
}
// GCNShader::wrapCoordinate for the modes GCNImageBinding accepts (0, 1, 2, 3, 6).
GCN_INLINE int gcn_wrap(int v, int size, gcn_u32 mode, GCN_THREAD bool& border) {
  border = false;
  if (mode == 0u) { const int m = v % size; return m < 0 ? m + size : m; }
  if (mode == 1u) { const int period = 2 * size; int m = v % period; if (m < 0) m += period; return m < size ? m : period - 1 - m; }
  if (mode == 3u) { const int m = v < 0 ? -v - 1 : v; return m < size - 1 ? m : size - 1; }
  if (mode == 6u && (v < 0 || v >= size)) border = true;
  return v < 0 ? 0 : (size - 1 < v ? size - 1 : v);
}
// One lane of GCNShader::imageAccess for image_load (0), image_sample (32),
// image_sample_l (36, single level) and image_sample_lz (39). `w0` is the
// first MIMG word (opcode and UNORM bit), `c` the address VGPRs, `t` the four
// result channels before the DMASK selection.
GCN_INLINE void gcn_image(const GcnImage img, GCN_DEVICE const unsigned char* texels, gcn_u32 w0,
                          GCN_THREAD const gcn_u32* c, GCN_THREAD gcn_u32* t) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
  for (gcn_u32 k = 0; k < 4u; ++k) t[k] = 0u;
  if (((w0 >> 18) & 127u) == 0u) {
    if (c[0] < img.width && c[1] < img.height) gcn_texel(img, texels, c[0], c[1], t);
    return;
  }
  float u = gcn_f(c[0]), v = gcn_f(c[1]);
  if (!(((w0 >> 12) & 1u) != 0u || img.forceUnnorm != 0u)) { u = u * float(img.width); v = v * float(img.height); }
  if (!(gcn_isfinite(u) && gcn_isfinite(v))) { u = 0.0f; v = 0.0f; }
  u = u < -1.0e8f ? -1.0e8f : u; u = 1.0e8f < u ? 1.0e8f : u;   // std::min(std::max(u, -1e8f), 1e8f)
  v = v < -1.0e8f ? -1.0e8f : v; v = 1.0e8f < v ? 1.0e8f : v;
  const int w = int(img.width), h = int(img.height);
  if (img.filter == 0u) {
    bool bx, by;
    const int cx = gcn_wrap(int(gcn_floor(u)), w, img.clampX, bx), cy = gcn_wrap(int(gcn_floor(v)), h, img.clampY, by);
    if (!(bx || by)) gcn_texel(img, texels, gcn_u32(cx), gcn_u32(cy), t);
    return;
  }
  const float fu = u - 0.5f, fv = v - 0.5f;
  const int x0 = int(gcn_floor(fu)), y0 = int(gcn_floor(fv));
  const float ax = fu - float(x0), ay = fv - float(y0);
  float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
    bool bx, by;
    const int cx = gcn_wrap(x0 + dx, w, img.clampX, bx), cy = gcn_wrap(y0 + dy, h, img.clampY, by);
    gcn_u32 texel[4] = {0u, 0u, 0u, 0u};
    if (!(bx || by)) gcn_texel(img, texels, gcn_u32(cx), gcn_u32(cy), texel);
    const float weight = (dx ? ax : 1.0f - ax) * (dy ? ay : 1.0f - ay);
    for (gcn_u32 k = 0; k < 4u; ++k) { const float term = gcn_f(texel[k]) * weight; color[k] = color[k] + term; }
  }
  for (gcn_u32 k = 0; k < 4u; ++k) t[k] = gcn_b(color[k]);
}
#endif
