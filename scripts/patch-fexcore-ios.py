#!/usr/bin/env python3
"""Adapt the pinned FEXCore checkout before compiling any iOS JIT unit.

Only modifies the supplied checkout. Linux/Windows implementations remain intact.
This compile port does not provide executable-memory allocation or activate JIT.
"""
import argparse
from pathlib import Path

TAG = "// MaxPS4 pinned Darwin adaptation"


def replace_once(code, old, new):
    if code.count(old) != 1:
        raise ValueError(f"Expected exactly one pinned source fragment: {old!r}")
    return code.replace(old, new, 1)


def adapt(root):
    changes = {}
    def edit(relative, transform):
        path = root / relative
        code = path.read_text()
        if TAG in code:
            return
        changes[path] = TAG + "\n" + transform(code)

    def mutex(code):
        code = replace_once(code, "#if !defined(_WIN32)\n#include <linux/futex.h>",
                            "#if !defined(_WIN32) && !defined(__APPLE__)\n#include <linux/futex.h>")
        code = replace_once(code, "#else\n#include <synchapi.h>",
                            "#elif defined(_WIN32)\n#include <synchapi.h>")
        return replace_once(code, "private:\n\n#if !defined(_WIN32)\n  void FutexWaitForWriteAvailable", """private:

#if defined(__APPLE__)
  void FutexWaitForWriteAvailable(uint32_t Expected) {
    std::atomic_ref<uint32_t>(Futex).wait(Expected, std::memory_order_relaxed);
  }
  void FutexWaitForReadAvailable(uint32_t Expected) {
    std::atomic_ref<uint32_t>(Futex).wait(Expected, std::memory_order_relaxed);
  }
  // Readers and writers wait on the same word: wake all so that waking
  // a reader cannot consume the only notification intended for a writer.
  void FutexWakeWriter() { std::atomic_ref<uint32_t>(Futex).notify_all(); }
  void FutexWakeReaders() { std::atomic_ref<uint32_t>(Futex).notify_all(); }
#elif !defined(_WIN32)
  void FutexWaitForWriteAvailable""")
    edit("FEXCore/include/FEXCore/Utils/WritePriorityMutex.h", mutex)

    def signals(code):
        start = code.index("class ScopedSignalMasker final {")
        stop = code.index("\n/**", start)
        original = code[start:stop]
        apple = """class ScopedSignalMasker final {
public:
  explicit ScopedSignalMasker(uint64_t Mask) {
    sigset_t Requested {};
    sigemptyset(&Requested);
    for (int Number = 1; Number < NSIG && Number <= 64; ++Number) {
      if (Mask & (uint64_t{1} << (Number - 1))) sigaddset(&Requested, Number);
    }
    Active = pthread_sigmask(SIG_SETMASK, &Requested, &OriginalMask) == 0;
  }
  ScopedSignalMasker(const ScopedSignalMasker&) = delete;
  ScopedSignalMasker& operator=(const ScopedSignalMasker&) = delete;
  ScopedSignalMasker(ScopedSignalMasker&& Other)
    : OriginalMask(Other.OriginalMask), Active(Other.Active) { Other.Active = false; }
  ~ScopedSignalMasker() {
    if (Active) pthread_sigmask(SIG_SETMASK, &OriginalMask, nullptr);
  }
private:
  sigset_t OriginalMask {};
  bool Active = false;
};
"""
        code = code[:start] + "#if defined(__APPLE__)\n" + apple + "#else\n" + original + "#endif\n" + code[stop:]
        return replace_once(code, "#include <signal.h>", "#include <signal.h>\n#include <pthread.h>")
    edit("FEXCore/include/FEXCore/Utils/SignalScopeGuards.h", signals)

    def processor(code):
        # Select Darwin before the TPIDRRO fast path: Darwin does not promise
        # that this register contains the Linux-style CPU index either.
        marker = "DEF_OP(ProcessorID) {"
        start = code.index(marker) + len(marker)
        stop = code.index("\n}", start)
        return code[:start] + """
#if defined(__APPLE__)
  // One stable virtual processor; no Linux getcpu SVC or host topology.
  LoadConstant(ARMEmitter::Size::i64Bit, GetReg(Node), 0);
#else""" + code[start:stop] + "\n#endif" + code[stop:]
    edit("FEXCore/Source/Interface/Core/JIT/MiscOps.cpp", processor)

    def prctl(code):
        for header in ("sys/user.h", "sys/prctl.h"):
            code = replace_once(code, f"#include <{header}>",
                                f"#if !defined(__APPLE__)\n#include <{header}>\n#endif")
        return code
    edit("FEXCore/include/FEXCore/Utils/PrctlUtils.h", prctl)

    def backend(code):
        code = replace_once(code, "#ifndef _WIN32\n#include <sys/prctl.h>",
                            "#if !defined(_WIN32) && !defined(__APPLE__)\n#include <sys/prctl.h>")
        # Header guards alone leave the actual Linux MDWE call undeclared.
        return replace_once(code, "#ifndef _WIN32\n// MDWE", "#if !defined(_WIN32) && !defined(__APPLE__)\n// MDWE")
    edit("FEXCore/Source/Interface/Core/CPUBackend.cpp", backend)
    def huge_pages(code):
        original = "  ::madvise(const_cast<void*>(Ptr), Size, Control == THPControl::Enable ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);"
        return replace_once(code, original, """#if defined(__APPLE__)
  // Darwin has no Linux transparent-huge-page advice. This optional
  // optimization is unsupported; do not substitute MADV_NORMAL for it.
  (void)Ptr;
  (void)Size;
  (void)Control;
#else
""" + original + "\n#endif")
    edit("FEXCore/include/FEXCore/Utils/AllocatorHooks.h", huge_pages)
    def cache(code):
        code = replace_once(code, "#include <type_traits>", "#include <type_traits>\n#if defined(__APPLE__)\n#include <libkern/OSCacheControl.h>\n#endif")
        old = "    __builtin___clear_cache(static_cast<char*>(Begin), static_cast<char*>(Begin) + Length);"
        return replace_once(code, old, """#if defined(__APPLE__)
    // Darwin exports sys_icache_invalidate; Linux's __clear_cache symbol
    // is not provided by the iPhoneOS runtime.
    sys_icache_invalidate(Begin, Length);
#else
""" + old + "\n#endif")
    edit("CodeEmitter/CodeEmitter/Buffer.h", cache)

    def libc_allocator(code):
        code = replace_once(code, "  return ::memalign(align, s);", """#if defined(__APPLE__)
  // POSIX requires at least pointer alignment. FEX's typed allocator
  // also requests alignof(char), so normalize smaller valid alignments.
  if (align == 0 || (align & (align - 1)) != 0) return nullptr;
  void* Ptr {};
  const size_t Alignment = align < sizeof(void*) ? sizeof(void*) : align;
  return ::posix_memalign(&Ptr, Alignment, s) == 0 ? Ptr : nullptr;
#else
  return ::memalign(align, s);
#endif""")
        code = replace_once(code, "  return ::valloc(size);", """#if defined(__APPLE__)
  void* Ptr {};
  return ::posix_memalign(&Ptr, static_cast<size_t>(::getpagesize()), size) == 0 ? Ptr : nullptr;
#else
  return ::valloc(size);
#endif""")
        return replace_once(code, "  return ::malloc_usable_size(ptr);", """#if defined(__APPLE__)
  return ::malloc_size(ptr);
#else
  return ::malloc_usable_size(ptr);
#endif""")
    edit("FEXCore/Source/Utils/AllocatorHooks.cpp", libc_allocator)

    def cpuid(code):
        code = replace_once(code, "#include <FEXHeaderUtils/Syscalls.h>",
                            "#if !defined(__APPLE__)\n#include <FEXHeaderUtils/Syscalls.h>\n#endif")
        return replace_once(code, "  FHU::Syscalls::getcpu(&CPU, nullptr);",
                            "#if !defined(__APPLE__)\n  FHU::Syscalls::getcpu(&CPU, nullptr);\n#endif")
    edit("FEXCore/Source/Interface/Core/CPUID.cpp", cpuid)
    # Validate every fragment before writing any file, to fail on upstream drift.
    for path, code in changes.items():
        path.write_text(code)
    print(f"Applied {len(changes)} Darwin adaptations (compile port only)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    args = parser.parse_args()
    adapt(args.checkout)
