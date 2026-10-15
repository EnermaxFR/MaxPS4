#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-build/orbis-v4}
mkdir -p "$OUT"
CXX=${CXX:-c++}
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
flags=(-std=c++17 -pthread -Wall -Wextra -Werror -pedantic -I MaxPS4-iOS/Native)
sources=(MaxPS4-iOS/Native/maxps4_guest_elf.cpp MaxPS4-iOS/Native/maxps4_ps4_linker.cpp
  MaxPS4-iOS/Native/maxps4_ps4_libkernel.cpp MaxPS4-iOS/Native/maxps4_ps4_virtual_memory.cpp
  MaxPS4-iOS/Native/maxps4_ps4_threads.cpp)
git rev-parse HEAD | tee "$OUT/source-revision.txt"
for mode in native sanitized; do
  extra=()
  if [[ "$mode" == sanitized ]]; then
    extra=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  fi
  for suite in OrbisV4AddressSpaceTest OrbisV4TranslationTest GuestDynamicTLSTest GuestDynamicTLSAllocationTest; do
    "$CXX" "${flags[@]}" "${extra[@]}" "MaxPS4-iOS/Tests/$suite.cpp" -o "$OUT/$suite-$mode"
    "$OUT/$suite-$mode" 2>&1 | tee "$OUT/$suite-$mode.txt"
  done
  # ASan owns low guest VAs on Linux. Keep native coverage on the device profile.
  if [[ "$mode" == sanitized ]]; then extra+=(-DMAXPS4_M1_HIGH_VA_SANITIZER); fi
  "$CXX" "${flags[@]}" "${extra[@]}" "${sources[@]}" MaxPS4-iOS/Tests/OrbisM1MemoryTest.cpp -o "$OUT/OrbisM1MemoryTest-$mode"
  "$OUT/OrbisM1MemoryTest-$mode" 2>&1 | tee "$OUT/OrbisM1MemoryTest-$mode.txt"
done
