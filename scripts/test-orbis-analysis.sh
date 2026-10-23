#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-build/orbis}
mkdir -p "$OUT"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisMetadataTest.cpp MaxPS4-iOS/Native/OrbisAnalysisBridge.cpp \
  MaxPS4-iOS/Native/maxps4_guest_elf.cpp MaxPS4-iOS/Native/maxps4_ps4_libkernel.cpp \
  MaxPS4-iOS/Native/maxps4_ps4_virtual_memory.cpp MaxPS4-iOS/Native/maxps4_ps4_threads.cpp \
  -o "$OUT/orbis-test"
python3 scripts/run-bounded.py 30 "$OUT/native-tests.txt" "$OUT/orbis-test" "$OUT/synthetic-sce.prx"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/GameDirectoryPreflightTest.cpp MaxPS4-iOS/Native/OrbisAnalysisBridge.cpp \
  MaxPS4-iOS/Native/PS4PKGBridge.cpp \
  MaxPS4-iOS/Native/maxps4_guest_elf.cpp MaxPS4-iOS/Native/maxps4_ps4_libkernel.cpp \
  MaxPS4-iOS/Native/maxps4_ps4_virtual_memory.cpp MaxPS4-iOS/Native/maxps4_ps4_threads.cpp \
  -o "$OUT/game-directory-test"
python3 scripts/run-bounded.py 30 "$OUT/game-directory-tests.txt" "$OUT/game-directory-test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/SelfImageTest.cpp -o "$OUT/self-image-test"
python3 scripts/run-bounded.py 30 "$OUT/self-image-tests.txt" "$OUT/self-image-test"
# Host analysis command (the app's analysis on a PC): builds and reads the
# synthetic PRX written by the metadata test.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tools/ps4-orbis-analyze.cpp -o "$OUT/ps4-orbis-analyze"
python3 scripts/run-bounded.py 30 "$OUT/ps4-orbis-analyze.txt" "$OUT/ps4-orbis-analyze" "$OUT/synthetic-sce.prx"
grep -q "Préparation : réussie" "$OUT/ps4-orbis-analyze.txt"
