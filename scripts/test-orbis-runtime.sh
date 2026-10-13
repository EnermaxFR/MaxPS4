#!/usr/bin/env bash
# Synthetic ELF/PRX only; does not execute guest code or access a PKG/PFS.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-build/orbis-runtime}
mkdir -p "$OUT"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Native/OrbisLinkerBridge.cpp MaxPS4-iOS/Tests/OrbisLinkerTest.cpp \
  -o "$OUT/orbis-linker-test"
python3 scripts/run-bounded.py 30 "$OUT/linker-tests.txt" "$OUT/orbis-linker-test" "$OUT"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/PS4ExecutableImageTest.cpp -o "$OUT/segments-test"
python3 scripts/run-bounded.py 30 "$OUT/segments-tests.txt" "$OUT/segments-test"
