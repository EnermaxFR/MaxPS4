#!/usr/bin/env bash
# Host development runner: original title files are read-only.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
GAME=${1:?Usage: bash scripts/run-keroblaster.sh GAME_DIRECTORY [BUILD_DIRECTORY] [LOG_DIRECTORY]}
BUILD=${2:-"$ROOT/build/mission51-release"}
LOG=${3:-"$ROOT/docs/mission51-logs/repro-$(date +%Y%m%d-%H%M%S)"}
mkdir -p "$LOG"
cmake --build "$BUILD" --target maxps4-keroblaster-boot --parallel 3 > "$LOG/build.log" 2>&1
sha256sum "$GAME/eboot.bin" "$GAME/sce_module/libc.prx" "$GAME/sce_module/libSceFios2.prx" > "$LOG/before.sha256"
status=0
MAXPS4_QUIET_LOCKS="${MAXPS4_QUIET_LOCKS-1}" MAXPS4_FRAME_DIR="${MAXPS4_FRAME_DIR:-$LOG}" MAXPS4_GPU_DUMP_DIR="$LOG" MAXPS4_FILE_TRACE=1 MAXPS4_INSTRUCTION_TRACE=1 MAXPS4_FETCH_TRACE=1 python3 "$ROOT/scripts/run-bounded.py" \
  "${MAXPS4_TIMEOUT:-300}" "$LOG/boot.txt" "$BUILD/Bin/maxps4-keroblaster-boot" \
  "$ROOT/docs/mission44-imports.tsv" "$GAME/eboot.bin" "$GAME/sce_module/libc.prx" \
  "$GAME/sce_module/libSceFios2.prx" "$GAME" > "$LOG/wrapper.txt" 2>&1 || status=$?
sha256sum --check "$LOG/before.sha256" > "$LOG/integrity.txt"
printf 'Runner exit=%s; logs: %s\n' "$status" "$LOG"
exit "$status"
