#pragma once
// Test-only PixelBackend: the draw's generated source (GCNPixelProgram) is
// compiled as C++ by the host compiler into a shared object at run time and
// loaded with dlopen; compiled shaders are cached by source hash. It runs the
// exact text a Metal backend compiles, so renderer tests can compare the
// offloaded path with the interpreter on Linux.
#include "../Native/GCNPixelOffload.hpp"
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace maxps4::gpu {
struct GCNCppJitBackend : PixelBackend {
  using Entry = void (*)(const uint32_t*, const float*, uint32_t, const GcnImage*, const unsigned char*,
                         const GcnPixelFragment*, size_t, GcnPixelOutput*);
  std::string nativeDir, workDir, compiler = "c++", flags = "-std=c++17 -O1 -fPIC -shared -w";
  std::map<uint64_t, Entry> cache;
  std::mutex lock;
  unsigned compiled = 0, failures = 0;

  GCNCppJitBackend(std::string native, std::string work) : nativeDir(std::move(native)), workDir(std::move(work)) {
    if (const char* cxx = std::getenv("CXX")) compiler = cxx;
  }
  bool prepare(PixelDraw& draw, std::string& why) override {
    std::lock_guard<std::mutex> guard(lock);
    auto found = cache.find(draw.key);
    if (found == cache.end()) {
      const std::string stem = workDir + "/ps-" + std::to_string(draw.key);
      {
        std::ofstream src(stem + ".cpp");
        src << "#include \"" << nativeDir << "/GCNKernel.h\"\n#include \"" << nativeDir << "/GCNKernelImage.h\"\n" << draw.source
            << "extern \"C\" void maxps4_jit_shade(const gcn_u32* s, const float* interp, gcn_u32 numInterp, const GcnImage* images,\n"
               "    const unsigned char* texels, const GcnPixelFragment* f, size_t n, GcnPixelOutput* o) {\n"
               "  for (size_t k = 0; k < n; ++k) gcn_pixel_shade(s, interp, numInterp, images, texels, f[k], o[k]);\n}\n";
        if (!src) { why = "JIT: cannot write " + stem + ".cpp"; ++failures; return false; }
      }
      const std::string command = compiler + " " + flags + " -o '" + stem + ".so' '" + stem + ".cpp' > '" + stem + ".log' 2>&1";
      if (std::system(command.c_str()) != 0) { why = "JIT: compilation failed, see " + stem + ".log"; ++failures; return false; }
      void* handle = dlopen((stem + ".so").c_str(), RTLD_NOW | RTLD_LOCAL);
      Entry entry = handle ? reinterpret_cast<Entry>(dlsym(handle, "maxps4_jit_shade")) : nullptr;
      if (!entry) { why = std::string("JIT: dlopen: ") + dlerror(); ++failures; return false; }
      found = cache.emplace(draw.key, entry).first;
      ++compiled;
    }
    draw.backendData = reinterpret_cast<void*>(found->second);
    return true;
  }
  bool shade(const PixelDraw& draw, const GcnPixelFragment* fragments, size_t n, GcnPixelOutput* out, std::string&) override {
    reinterpret_cast<Entry>(draw.backendData)(draw.sgprs.data(), draw.interp.data(), draw.numInterp, draw.images.data(),
                                              draw.texels.data(), fragments, n, out);
    return true;
  }
};
} // namespace maxps4::gpu
