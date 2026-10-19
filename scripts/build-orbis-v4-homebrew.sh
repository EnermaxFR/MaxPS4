#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:?output directory required}
SOURCE=${2:-homebrew/orbis-v4-minimal/start.S}
mkdir -p "$OUT"
LINKER=${ELF_LD:-ld}
if [[ $(uname -s) == Darwin && -z ${ELF_LD:-} ]]; then
  # Apple ld cannot emit ELF. CI installs the open-source lld formula.
  LINKER="$(brew --prefix lld)/bin/ld.lld"
fi
# Keep the argument array nonempty: macOS Bash 3 treats an empty array
# as unset under set -u.
flags=(-target x86_64-unknown-none)
if [[ "$SOURCE" == *.c ]]; then
  level=${MAXPS4_HOMEBREW_C_OPT_LEVEL:-1}
  [[ "$level" == 0 || "$level" == 1 ]] || { echo 'C optimization level must be 0 or 1' >&2; exit 2; }
  # This single freestanding C probe fits the existing bounded admission at O1.
  # No promise that arbitrary optimized C or a PS4 CRT uses that subset.
  flags+=(-std=c11 "-O$level" -ffreestanding -fno-builtin
    -fomit-frame-pointer -fno-asynchronous-unwind-tables -fno-unwind-tables
    -fno-pic -fno-pie -mno-red-zone -Wall -Wextra -Werror)
fi
"${CLANG:-clang}" "${flags[@]}" -c "$SOURCE" -o "$OUT/start.o"
"$LINKER" -m elf_x86_64 -z max-page-size=0x4000 -T homebrew/orbis-v4-minimal/layout.ld \
  "$OUT/start.o" -o "$OUT/homebrew.elf"
# Brand our own linked plaintext ELF for the existing Orbis loader. This only
# changes OSABI and e_type; no proprietary converter, signing or decryption.
python3 - "$OUT/homebrew.elf" <<'PY'
import pathlib, struct, sys
p = pathlib.Path(sys.argv[1])
b = bytearray(p.read_bytes())
assert b[:7] == b'\x7fELF\x02\x01\x01'
assert struct.unpack_from('<HH', b, 16) == (2, 62)
b[7] = 9
struct.pack_into('<H', b, 16, 0xfe00)
p.write_bytes(b)
PY
