#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
OUT=${1:-build/orbis-graphics}
mkdir -p "$OUT"
git rev-parse HEAD > "$OUT/source-revision.txt"
for mode in native sanitized; do
  flags=(-std=c++17 -O1 -g -pthread -I MaxPS4-iOS/Native)
  if [[ "$mode" == sanitized ]]; then
    flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  fi
  for suite in PM4Type3DecoderTest PM4CommandInterpreterTest PM4GuestSubmissionTest \
    PM4MetalDrawPlanTest LinearTextureSnapshotTest GNMCommandExecutorTest OrbisV4GraphicsServicesTest; do
    sources=("MaxPS4-iOS/Tests/$suite.cpp")
    if [[ "$suite" == PM4GuestSubmissionTest ]]; then
      sources+=(MaxPS4-iOS/Native/maxps4_guest_elf.cpp)
    fi
    "${CXX:-c++}" "${flags[@]}" "${sources[@]}" -ldl \
      -o "$OUT/$suite-$mode" > "$OUT/$suite-$mode-build.txt" 2>&1
    python3 scripts/run-bounded.py 30 "$OUT/$suite-$mode.txt" "$OUT/$suite-$mode"
  done
done
