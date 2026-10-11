#!/usr/bin/env bash
# Strict link-only diagnostic. Run after the workflow compiles the real sources.
set -euo pipefail
OUT=${1:?Usage: bash scripts/link-fexcore-ios.sh OBJECT_DIRECTORY}
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
objects=()
for unit in ALUOps AtomicOps BranchOps ConversionOps EncryptionOps JIT MemoryOps MiscOps MoveOps VectorOps Arm64Relocations CPUBackend; do
  object="$OUT/$unit-ios-arm64.o"
  # The pre-existing separate MiscOps compilation uses a lowercase filename.
  if [[ "$unit" == MiscOps ]]; then object="$OUT/miscops-ios-arm64.o"; fi
  test -s "$object"
  objects+=("$object")
done
for unit in Arm64Emitter LookupCache CPUID LongJump LogManager ForcedAssert SpinWaitLock AllocatorHooks; do
  object="$OUT/dependency-$unit-ios-arm64.o"
  test -s "$object"
  objects+=("$object")
done
printf '%s\n' "SDK=$SDK" 'target=arm64-apple-ios16.0' 'kind=dynamiclib; undefined symbols forbidden' > "$OUT/link-target.txt"
file "${objects[@]}" "$OUT/jit-with-dependencies.o" > "$OUT/link-mach-o.txt"
xcrun nm -u "$OUT/jit-with-dependencies.o" > "$OUT/link-undefined.nm"
xcrun nm -gU "$OUT/jit-with-dependencies.o" > "$OUT/link-defined.nm"
xcrun otool -Iv "$OUT/jit-with-dependencies.o" > "$OUT/link-indirect.txt"
# An object has no dynamically loaded libraries; inspect a dylib only on success.
if xcrun --sdk iphoneos clang++ -target arm64-apple-ios16.0 -isysroot "$SDK" \
    -dynamiclib -Wl,-undefined,error "${objects[@]}" \
    -o "$OUT/fexcore-link-probe.dylib" > "$OUT/strict-link.log" 2>&1; then
  file "$OUT/fexcore-link-probe.dylib" >> "$OUT/link-mach-o.txt"
  xcrun otool -L "$OUT/fexcore-link-probe.dylib" > "$OUT/link-libraries.txt"
  echo 'PASS strict iOS link (runtime JIT not tested)' > "$OUT/strict-link-status.txt"
else
  echo 'BLOCKED complete iOS link; see strict-link.log and link-report.json' > "$OUT/strict-link-status.txt"
  exit 1
fi
