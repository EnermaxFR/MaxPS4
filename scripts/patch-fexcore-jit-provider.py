#!/usr/bin/env python3
"""Replace only the pinned iOS allocator with MaxPS4's real prepared-region pool.

All edits are validated before writing; repeated application must match exactly.
No generic FEX ABI or replacement CPU backend is introduced.
"""
from pathlib import Path
import sys

TAG = '// MaxPS4 authorized debugger region provider'
ALLOCATOR = 'FEXCore/Source/Utils/Allocator.cpp'
BACKEND = 'FEXCore/Source/Interface/Core/CPUBackend.cpp'
START = '#if defined(__APPLE__) && TARGET_OS_IPHONE\n// iOS JIT allocation:'
END = '#endif\n} // namespace FEXCore::Allocator'
PROVIDER = '''#if defined(__APPLE__) && TARGET_OS_IPHONE
// MaxPS4 authorized debugger region provider
void* iOSJITAlloc(size_t Size) { return maxps4_jit_allocate(Size); }
bool iOSJITFreeIfOwned(void* Address, size_t Size) {
  const auto Result = maxps4_jit_free(Address, Size);
  if (Result < 0) std::abort();
  return Result == 1;
}
void* GetExecutableAddress(void* Address) { return maxps4_jit_executable(Address); }
void* GetWritableAddress(void* Address) { return maxps4_jit_writable(Address); }
iOSJITAddressKind iOSJITDescribeAddress(void* Address, uintptr_t* Base, size_t* Offset, size_t* Size) {
  return maxps4_jit_describe(Address, Base, Offset, Size) ?
    iOSJITAddressKind::LiveAllocation : iOSJITAddressKind::NotTracked;
}
'''
OLD_BACKEND = '''#else
    Ptr = static_cast<uint8_t*>(FEXCore::Allocator::VirtualAlloc(Size, true));
    LOGMAN_THROW_A_FMT(!!Ptr, "Couldn't allocate code buffer");'''
NEW_BACKEND = '''#elif defined(__APPLE__) && TARGET_OS_IPHONE
    // MaxPS4 authorized debugger region provider: native 16 KiB write guard
    // is installed before initialization; no late RX request after detach.
    Ptr = static_cast<uint8_t*>(FEXCore::Allocator::VirtualAlloc(Size, true));
    if (!Ptr || Ptr == reinterpret_cast<void*>(-1)) throw std::bad_alloc();
#else
    Ptr = static_cast<uint8_t*>(FEXCore::Allocator::VirtualAlloc(Size, true));
    LOGMAN_THROW_A_FMT(!!Ptr, "Couldn't allocate code buffer");'''


def adapt(root):
    paths = {name: root / name for name in (ALLOCATOR, BACKEND)}
    texts = {name: path.read_text() for name, path in paths.items()}
    allocator = texts[ALLOCATOR]
    if TAG in allocator:
        if allocator.count(PROVIDER) != 1 or allocator.count('#include "maxps4_jit_memory.h"') != 1:
            raise ValueError('Applied iOS allocator provider diverged')
    else:
        if allocator.count(START) != 1 or allocator.count(END) != 1:
            raise ValueError('Pinned iOS allocator context diverged')
        start = allocator.index(START)
        end = allocator.index(END, start)
        original = allocator[start:end]
        if original.count('void* iOSJITAlloc(size_t Size)') != 1 or 'BreakGetJITMapping' not in original:
            raise ValueError('Pinned iOS allocator implementation diverged')
        texts[ALLOCATOR] = '#include "maxps4_jit_memory.h"\n' + allocator[:start] + PROVIDER + allocator[end:]
    backend = texts[BACKEND]
    if TAG in backend:
        if backend.count(NEW_BACKEND) != 1:
            raise ValueError('Applied code-cache provider diverged')
    else:
        if backend.count(OLD_BACKEND) != 1:
            raise ValueError('Pinned code-cache allocation context diverged')
        texts[BACKEND] = '#include <new>\n' + backend.replace(OLD_BACKEND, NEW_BACKEND, 1)
    for name, path in paths.items():
        path.write_text(texts[name])


if __name__ == '__main__':
    adapt(Path(sys.argv[1]))
