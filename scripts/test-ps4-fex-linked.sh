#!/usr/bin/env bash
# Real FEX frontend/JIT/dispatcher and libkernel. On x86 hosts only, execute the
# generated ARM64 instructions through FEX's upstream VIXL simulator; report
# backend=3 explicitly. This never establishes native ARM64 or iPhone execution.
set -euo pipefail
SOURCE=${1:?Usage: bash scripts/test-ps4-fex-linked.sh AETHER_CHECKOUT BUILD_DIR}
OUT=${2:?Missing build directory}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SOURCE=$(cd "$SOURCE" && pwd)
test "$(git -C "$SOURCE" rev-parse HEAD)" = 3347fc13f19860855ce91b66356940989ea6b6a6
FEX="$SOURCE/runtime/sources/fexcore-darwin"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
options=()
case "$(uname -m)" in
  x86_64)
    test "$(git -C "$FEX/External/vixl" rev-parse HEAD)" = 5f418449c48f6ca3ad37c47ec632f17a561c0580
    # FEX's x86 debug fallback ABI uses __m256i pairs even for this integer
    # fixture. Compile with the required AVX host ABI; no guest AVX is admitted.
    options+=(-DENABLE_X86_HOST_DEBUG=ON -DENABLE_VIXL_SIMULATOR=ON -DCMAKE_CXX_FLAGS=-mavx)
    ;;
  aarch64|arm64) ;;
  *) echo 'Unsupported execution host' >&2; exit 1 ;;
esac
python3 -B "$ROOT/MaxPS4-iOS/Tests/FEXDispatcherInitPatchTest.py" "$FEX"
python3 -B "$ROOT/scripts/patch-fexcore-dispatcher-init.py" "$FEX"
cmake -S "$FEX" -B "$OUT" -G Ninja \
  -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$ROOT/cmake/FEXCoreLinkedTests.cmake" \
  -DMAXPS4_ROOT="$ROOT" -DTUNE_CPU=none -DTUNE_ARCH=generic \
  -DBUILD_FEXCORE_ONLY=ON -DBUILD_FEXCORE_SMOKE=OFF -DBUILD_TESTING=OFF \
  -DBUILD_FEXCONFIG=OFF -DBUILD_THUNKS=OFF -DENABLE_GDB_SYMBOLS=OFF \
  -DENABLE_LTO=OFF -DENABLE_CCACHE=OFF -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF \
  -DENABLE_FEX_ALLOCATOR=OFF -DENABLE_VIXL_DISASSEMBLER=OFF -DENABLE_ZYDIS=OFF \
  -DOVERRIDE_VERSION=MaxPS4-Linked -DOVERRIDE_HASH=f2b679f6028ce1c38875233aecfcf5d3f8ebecec \
  "${options[@]}" > "$OUT/configure.log" 2>&1
cmake --build "$OUT" --target maxps4-ps4-linked-test --parallel "${MAXPS4_BUILD_JOBS:-3}" > "$OUT/build.log" 2>&1
python3 "$ROOT/scripts/run-bounded.py" 60 "$OUT/ps4-linked-tests.txt" "$OUT/Bin/maxps4-ps4-linked-test"
python3 "$ROOT/scripts/run-bounded.py" 10 "$OUT/fixture-export.txt" "$OUT/Bin/maxps4-ps4-linked-test" \
  "$OUT/ps4-libkernel-demo.elf" "$OUT/ps4-libkernel-combined.elf"
