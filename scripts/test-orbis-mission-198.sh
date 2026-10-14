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

"$CXX" "${flags[@]}" -I MaxPS4-iOS/Native \
  MaxPS4-iOS/Native/maxps4_guest_elf.cpp \
  MaxPS4-iOS/Native/maxps4_ps4_linker.cpp \
  MaxPS4-iOS/Native/maxps4_ps4_libkernel.cpp \
  MaxPS4-iOS/Native/maxps4_ps4_virtual_memory.cpp \
  MaxPS4-iOS/Native/maxps4_ps4_threads.cpp \
  MaxPS4-iOS/Tests/OrbisM1MemoryTest.cpp -o "$OUT/OrbisM1MemoryTest"
"$OUT/OrbisM1MemoryTest" | tee "$OUT/OrbisM1MemoryTest.txt"
