#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT=${1:?output directory required}
BIN=${2:-build/m1-device-fixture-export}
mkdir -p "$(dirname "$BIN")" "$OUT"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -U MAXPS4_M1_HIGH_VA_HOST_TEST -U MAXPS4_M1_HIGH_VA_SANITIZER \
  "$ROOT/MaxPS4-iOS/Tests/M1DeviceFixtureExport.cpp" -o "$BIN"
"$BIN" "$OUT"
python3 - "$OUT" <<'PY'
import hashlib,json,pathlib,struct,sys
root=pathlib.Path(sys.argv[1]); files={}
for name in ['m1-two-threads-three-tls-modules.elf','tls-83.prx','tls-129.prx']:
    data=(root/name).read_bytes()
    assert data[:9]==b'\x7fELF\x02\x01\x01\x09\x00'
    assert struct.unpack_from('<Q',data,24)[0]==0x400004000
    files[name]=hashlib.sha256(data).hexdigest()
(root/'manifest.json').write_text(json.dumps({'profile':'iphone-low-m1','entry':0x400004000,
    'snapshot':0x400010100,'files':files},sort_keys=True,indent=2)+'\n')
PY
