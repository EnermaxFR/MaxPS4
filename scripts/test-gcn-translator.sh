#!/usr/bin/env bash
# GCN -> C++/MSL pixel shader translator: kernel prelude equivalence, stage 1
# (translation and refusals) and stage 2 (translated code compiled as C++ and
# compared with the interpreter), natively and under ASan/UBSan.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
OUT=${1:-build/gcn-translator}
mkdir -p "$OUT"
for mode in native sanitized; do
  flags=(-std=c++17 -O1 -g -Wall -Wextra -pthread -I MaxPS4-iOS/Native)
  [[ "$mode" == sanitized ]] && flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  "${CXX:-c++}" "${flags[@]}" MaxPS4-iOS/Tests/GCNKernelOpsTest.cpp -o "$OUT/GCNKernelOpsTest-$mode"
  python3 scripts/run-bounded.py 600 "$OUT/GCNKernelOpsTest-$mode.txt" "$OUT/GCNKernelOpsTest-$mode"
  "${CXX:-c++}" "${flags[@]}" MaxPS4-iOS/Tests/GCNTranslatorTool.cpp -o "$OUT/GCNTranslatorTool-$mode"
  python3 scripts/run-bounded.py 60 "$OUT/GCNTranslatorTool-$mode.txt" "$OUT/GCNTranslatorTool-$mode" "$OUT/translated-$mode.inc"
  "${CXX:-c++}" "${flags[@]}" -DGCN_TRANSLATED="\"$ROOT/$OUT/translated-$mode.inc\"" \
    MaxPS4-iOS/Tests/GCNTranslatorTest.cpp -ldl -o "$OUT/GCNTranslatorTest-$mode"
  ASAN_OPTIONS=detect_leaks=1 python3 scripts/run-bounded.py 300 "$OUT/GCNTranslatorTest-$mode.txt" "$OUT/GCNTranslatorTest-$mode"
done
