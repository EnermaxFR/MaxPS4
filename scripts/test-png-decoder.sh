#!/usr/bin/env bash
# PngDecoder + libScePngDec service, native and ASan/UBSan (leak detection on).
# MAXPS4_KERO_DIR adds every PNG of the title (read-only) to the PIL comparison.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
OUT=${1:-build/png-decoder}
mkdir -p "$OUT"
python3 scripts/make-png-fixtures.py "$OUT/fixtures" ${MAXPS4_KERO_DIR:+"$MAXPS4_KERO_DIR"}
for mode in native sanitized; do
  flags=(-std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -I MaxPS4-iOS/Native)
  [[ "$mode" == sanitized ]] && flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  "${CXX:-c++}" "${flags[@]}" MaxPS4-iOS/Tests/PngDecoderTest.cpp -o "$OUT/PngDecoderTest-$mode"
  ASAN_OPTIONS=detect_leaks=1 python3 scripts/run-bounded.py 120 "$OUT/PngDecoderTest-$mode.txt" \
    "$OUT/PngDecoderTest-$mode" "$OUT/fixtures/manifest.txt"
done
