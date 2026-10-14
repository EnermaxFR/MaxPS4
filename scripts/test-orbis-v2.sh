#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
cd "$(dirname "$0")/.."
OUT=${1:-build/orbis-v2}
mkdir -p "$OUT"
CXX=${CXX:-c++}
flags=(-std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
python3 scripts/create-orbis-test-v2.py "$OUT/orbis-test.elf"
for suite in PS4PKGInventoryTest PS4PKGMetadataTest PS4PFSHeaderDiagnosticTest PS4PFSReadOnlyTest PFSSparseExtractionV2Test; do
  "$CXX" "${flags[@]}" MaxPS4-iOS/Native/PS4PKGBridge.cpp "MaxPS4-iOS/Tests/$suite.cpp" -o "$OUT/$suite"
  python3 scripts/run-bounded.py 30 "$OUT/$suite.txt" "$OUT/$suite"
done
"$CXX" "${flags[@]}" -I MaxPS4-iOS/Native MaxPS4-iOS/Native/maxps4_jit_memory.cpp MaxPS4-iOS/Tests/FEXJITUnsupportedHostTest.cpp -o "$OUT/jit-host-test"
python3 scripts/run-bounded.py 30 "$OUT/jit-host-test.txt" "$OUT/jit-host-test"
node MaxPS4-iOS/Tests/StikScriptTest.js
python3 scripts/test-pkg-sparse-ci.py
bash scripts/test-orbis-runtime.sh "$OUT/linker"
bash scripts/test-orbis-analysis.sh "$OUT/analysis"
"$CXX" "${flags[@]}" MaxPS4-iOS/Native/PS4PKGBridge.cpp MaxPS4-iOS/Native/maxps4_guest_elf.cpp MaxPS4-iOS/Tests/OrbisRuntimeV2Test.cpp -o "$OUT/OrbisRuntimeV2Test"
python3 scripts/run-bounded.py 30 "$OUT/OrbisRuntimeV2Test.txt" "$OUT/OrbisRuntimeV2Test" "$OUT/orbis-test.elf"
