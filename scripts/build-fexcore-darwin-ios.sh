#!/usr/bin/env bash
# Compile/link the complete matching Darwin runtime, never an app or IPA.
set -euo pipefail
SOURCE=${1:?Usage: bash scripts/build-fexcore-darwin-ios.sh AETHER_CHECKOUT BUILD_DIR}
OUT=${2:?Missing build directory}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SOURCE=$(cd "$SOURCE" && pwd)
FEX="$SOURCE/runtime/sources/fexcore-darwin"
test "$(git -C "$SOURCE" rev-parse HEAD)" = 3347fc13f19860855ce91b66356940989ea6b6a6
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
# Patch only the disposable dependency checkout. No replacement symbols are defined.
python3 - "$FEX" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
old = '__builtin___clear_cache(static_cast<char*>(Begin), static_cast<char*>(Begin) + Length);'
for relative in ('CodeEmitter/CodeEmitter/Buffer.h', 'FEXCore/Source/Utils/ArchHelpers/Arm64.cpp'):
    path = root / relative
    text = path.read_text()
    if 'MaxPS4 Darwin cache API' in text:
        continue
    if text.count(old) != 1:
        raise ValueError(f'Unexpected cache invalidation context: {relative}')
    text = text.replace(old, 'sys_icache_invalidate(Begin, Length);', 1)
    path.write_text('// MaxPS4 Darwin cache API\n#include <libkern/OSCacheControl.h>\n' + text)
PY
cmake -S "$FEX" -B "$OUT" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_SYSTEM_PROCESSOR=arm64 \
  -DCMAKE_OSX_SYSROOT="$SDK" -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 -DCMAKE_BUILD_TYPE=Debug \
  -DTUNE_CPU=none -DTUNE_ARCH=generic \
  -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$ROOT/cmake/FEXCoreIOSLinkage.cmake" \
  -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_unordered_dense=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_range-v3=ON \
  -DBUILD_FEXCORE_ONLY=ON -DBUILD_FEXCORE_SMOKE=OFF \
  -DBUILD_TESTING=OFF -DBUILD_FEX_LINUX_TESTS=OFF -DBUILD_THUNKS=OFF \
  -DBUILD_FEXCONFIG=OFF -DENABLE_GDB_SYMBOLS=OFF -DENABLE_LTO=OFF \
  -DENABLE_CCACHE=OFF -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF \
  -DENABLE_FEX_ALLOCATOR=OFF -DENABLE_VIXL_DISASSEMBLER=OFF \
  -DENABLE_ZYDIS=OFF -DOVERRIDE_VERSION=MaxPS4-Darwin \
  -DOVERRIDE_HASH=f2b679f6028ce1c38875233aecfcf5d3f8ebecec \
  2>&1 | tee "$OUT/configure.log"
cmake --build "$OUT" --target FEXCore_shared --parallel 3 --verbose 2>&1 | tee "$OUT/build-link.log"
python3 - "$OUT" <<'PY'
import json, sys
from pathlib import Path
out = Path(sys.argv[1])
commands = json.loads((out / 'compile_commands.json').read_text())
sources = sorted({row['file'].split('/fexcore-darwin/')[-1] for row in commands
                  if '/fexcore-darwin/FEXCore/Source/' in row['file']})
assert len([p for p in sources if '/JIT/' in p]) == 11
assert 'FEXCore/Source/Interface/Core/CPUBackend.cpp' in sources
(out / 'source-manifest.txt').write_text('\n'.join(sources) + '\n')
PY
LIB="$OUT/FEXCore/Source/libFEXCore.dylib"
test -s "$LIB"
file "$LIB" | tee "$OUT/mach-o.txt"
xcrun lipo -archs "$LIB" | tee "$OUT/architectures.txt"
xcrun nm -u "$LIB" > "$OUT/undefined.txt"
xcrun nm -gU "$LIB" > "$OUT/defined.txt"
xcrun otool -L "$LIB" > "$OUT/libraries.txt"
xcrun otool -Iv "$LIB" > "$OUT/indirect.txt"
# A strict dylib may retain SDK imports; FEXCore imports must all be defined.
if xcrun nm -u "$LIB" | grep -E 'FEXCore|LogMan'; then
  echo 'Unresolved FEXCore runtime import' >&2
  exit 1
fi
echo 'PASS complete strict iOS ARM64 Darwin linkage; device execution NOT tested' | tee "$OUT/link-status.txt"
