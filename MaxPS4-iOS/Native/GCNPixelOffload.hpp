#pragma once
// Pixel shading offload for GCNRenderer: when a pixel backend is attached and
// the bound pixel shader translates (GCNShaderTranslator), the renderer keeps
// geometry, rasterisation, depth and colour writes, and hands the covered
// fragments of each batch to the backend, which runs the translated shader
// (C++ compiled at run time in tests, Metal compute on Apple devices).
// Anything the translation or the backend does not accept keeps the draw on
// the GCNShader interpreter, with the reason recorded.
//
// The per-fragment entry (gcn_pixel_shade) is generated text in the same
// C++/MSL subset as the translated shader, so every backend runs identical
// code. A backend sees, for one draw:
// * sgprs: the final SGPR file of a one-lane interpreter run (scalar prepass);
// * interp: P0/P10/P20 per (setup, attribute, channel), exactly what the
//   interpreter's interpolation callback returns;
// * images/texels: GcnImage descriptors over a copy of each sampled resource;
// * per fragment: its setup and the initial input VGPRs (barycentrics,
//   position), as the interpreter's wave receives them.
#include "GCNImageBinding.hpp"
#include "GCNShaderTranslator.hpp"
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace maxps4::gpu {

// Layouts shared with generated code (all 32-bit fields, no padding).
constexpr unsigned gcnPixelInputs = 6;   // persp i/j, linear i/j, pos x, pos y at most
struct GcnPixelFragment { uint32_t setup; uint32_t inputs[gcnPixelInputs]; uint32_t reserved; };
struct GcnPixelOutput { uint32_t data[4]; uint32_t exported; uint32_t reserved[3]; };
static_assert(sizeof(GcnPixelFragment) == 32 && sizeof(GcnPixelOutput) == 32, "ABI shared with generated code");

struct PixelDraw {
  uint64_t program = 0;
  uint32_t rsrc1 = 0;
  unsigned vgprs = 0, numInterp = 0;
  GCNShaderTranslator::Result translation;
  std::vector<uint32_t> sgprs;         // 128 entries
  std::vector<float> interp;           // [setup][attr][chan][k], numInterp * 12 floats per setup
  std::vector<GcnImage> images;        // by translation slot; base = offset in texels
  std::vector<uint8_t> texels;
  std::string source;                  // shader + gcn_pixel_shade, C++/MSL subset (see generate)
  uint64_t key = 0;                    // hash of the source (backend cache key)
  void* backendData = nullptr;         // owned by the backend for this draw
};

struct PixelBackend {
  virtual ~PixelBackend() = default;
  // Make the draw's generated code runnable (compile or fetch from a cache).
  // false: the draw stays on the interpreter, `why` says why.
  virtual bool prepare(PixelDraw& draw, std::string& why) = 0;
  // Shade n fragments; called concurrently by the renderer's workers.
  virtual bool shade(const PixelDraw& draw, const GcnPixelFragment* fragments, size_t n, GcnPixelOutput* out, std::string& error) = 0;
  // End of the draw (also after a failed one): per-draw resources go.
  virtual void release(PixelDraw&) {}
};

struct GCNPixelProgram {
  // Source of the translated shader and its per-fragment entry. Backends
  // prepend GCNKernel.h and GCNKernelImage.h (and their own kernel wrapper).
  static std::string generate(const GCNShaderTranslator::Result& r) {
    std::ostringstream s;
    s << "struct GcnPixelFragment { gcn_u32 setup; gcn_u32 inputs[" << gcnPixelInputs << "]; gcn_u32 reserved; };\n"
         "struct GcnPixelOutput { gcn_u32 data[4]; gcn_u32 exported; gcn_u32 reserved[3]; };\n"
         "#ifdef __METAL_VERSION__\n"
         "#define GCN_UNIFORM constant\n"
         "#define GCN_PIXEL_DATA device\n"
         "#else\n"
         "#define GCN_UNIFORM\n"
         "#define GCN_PIXEL_DATA\n"
         "#endif\n"
         "struct GcnPixelEnv { GCN_PIXEL_DATA const float* interp; GCN_PIXEL_DATA const GcnImage* images;"
         " GCN_DEVICE const unsigned char* texels; GCN_THREAD GcnPixelOutput* out; };\n"
         "#define GCN_ENV_PARAM GCN_THREAD GcnPixelEnv& env\n"
         "#define GCN_ENV_INTERP(a, c, k) env.interp[((a) * 4u + (c)) * 3u + (k)]\n"
         "#define GCN_ENV_SAMPLE(k, w0, w1, c, t) ((void)(w1), gcn_image(env.images[k], env.texels, w0, c, t))\n"
         "#define GCN_ENV_EXPORT(tg, en, cp, dn, e) \\\n"
         "  do { if ((tg) == 0u) { for (int q = 0; q < 4; ++q) env.out->data[q] = (e)[q]; env.out->exported = 1u; } } while (0)\n";
    s << GCNShaderTranslator::function("gcn_ps", r);
    s << "GCN_INLINE void gcn_pixel_shade(GCN_UNIFORM const gcn_u32* s, GCN_PIXEL_DATA const float* interp, gcn_u32 numInterp,\n"
         "    GCN_PIXEL_DATA const GcnImage* images, GCN_DEVICE const unsigned char* texels,\n"
         "    GCN_PIXEL_DATA const GcnPixelFragment& f, GCN_THREAD GcnPixelOutput& o) {\n"
         "  gcn_u32 v[" << r.vgprs << "];\n"
         "  for (gcn_u32 k = 0; k < " << r.vgprs << "u; ++k) v[k] = k < " << gcnPixelInputs << "u ? f.inputs[k] : 0u;\n"
         "  for (gcn_u32 k = 0; k < 4u; ++k) o.data[k] = 0u;\n"
         "  o.exported = 0u;\n"
         "  GcnPixelEnv env; env.interp = interp + f.setup * numInterp * 12u; env.images = images; env.texels = texels; env.out = &o;\n"
         "  gcn_ps(s, v, env);\n"
         "}\n";
    return s.str();
  }
  static uint64_t hash(const std::string& text) {
    uint64_t h = 1469598103934665603ull;   // FNV-1a
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
    return h;
  }
};
} // namespace maxps4::gpu
