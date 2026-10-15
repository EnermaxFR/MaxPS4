#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-build/orbis-mission-198}
mkdir -p "$OUT"
CXX=${CXX:-c++}
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
for suite in GuestArenaGuardTest GuestELFMemoryTest GuestELFInterpreterTest GuestRIPStoreAdmissionTest PS4GuestStagingTest PS4StagedCPUAdmissionTest PS4OrbisCPUPreparationTest; do
  "$CXX" "${flags[@]}" -I MaxPS4-iOS/Native MaxPS4-iOS/Native/maxps4_guest_elf.cpp "MaxPS4-iOS/Tests/$suite.cpp" -o "$OUT/$suite"
  "$OUT/$suite" | tee "$OUT/$suite.txt"
done

m1_sources=(
  MaxPS4-iOS/Native/maxps4_guest_elf.cpp
  MaxPS4-iOS/Native/maxps4_ps4_linker.cpp
  MaxPS4-iOS/Native/maxps4_ps4_libkernel.cpp
  MaxPS4-iOS/Native/maxps4_ps4_virtual_memory.cpp
  MaxPS4-iOS/Native/maxps4_ps4_threads.cpp
  MaxPS4-iOS/Tests/OrbisM1MemoryTest.cpp
)
# Test ordinary VM policy too: sanitizer runtimes can alter process VA layout.
"$CXX" -std=c++17 -Wall -Wextra -Werror -pedantic -I MaxPS4-iOS/Native \
  "${m1_sources[@]}" -o "$OUT/OrbisM1NativeMemoryTest"
"$OUT/OrbisM1NativeMemoryTest" | tee "$OUT/OrbisM1NativeMemoryTest.txt"
# Linux ASan shadow owns the 16 GiB CPU/device fixture range. The preceding
# unsanitized suite checks that low profile; sanitizer checks retain high VAs.
"$CXX" "${flags[@]}" -DMAXPS4_M1_HIGH_VA_SANITIZER -I MaxPS4-iOS/Native \
  "${m1_sources[@]}" -o "$OUT/OrbisM1MemoryTest"
"$OUT/OrbisM1MemoryTest" | tee "$OUT/OrbisM1MemoryTest.txt"
"$CXX" "${flags[@]}" -I MaxPS4-iOS/Native \
  MaxPS4-iOS/Tests/GuestDynamicTLSTest.cpp -o "$OUT/GuestDynamicTLSTest"
"$OUT/GuestDynamicTLSTest" | tee "$OUT/GuestDynamicTLSTest.txt"


"$CXX" "${flags[@]}" -I MaxPS4-iOS/Native \
  MaxPS4-iOS/Tests/GuestDynamicTLSAllocationTest.cpp -o "$OUT/GuestDynamicTLSAllocationTest"
"$OUT/GuestDynamicTLSAllocationTest" | tee "$OUT/GuestDynamicTLSAllocationTest.txt"
