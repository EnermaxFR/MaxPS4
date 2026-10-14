#!/usr/bin/env bash
# Build a separate experimental Orbis V3 app, never overwrite the stable IPA.
set -euo pipefail
SOURCE=${1:?Usage: scripts/build-orbis-test-ipa.sh AETHER_CHECKOUT}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
# Reuse the pinned runtime build, framework and existing verified stable app.
bash scripts/build-fexcore-app.sh "$SOURCE"
xcodebuild -project MaxPS4-iOS/MaxPS4.xcodeproj -scheme MaxPS4OrbisTest \
  -configuration Release -sdk iphoneos -destination 'generic/platform=iOS' \
  -derivedDataPath build/xcode-fex CODE_SIGNING_ALLOWED=NO build \
  2>&1 | tee build/diagnostics/orbis-test-build.log
APP=build/xcode-fex/Build/Products/Release-iphoneos/MaxPS4OrbisTest.app
test -d "$APP"
mkdir -p build/orbis-test/Payload
cp -R "$APP" build/orbis-test/Payload/
APP=build/orbis-test/Payload/MaxPS4OrbisTest.app
python3 - "$APP/Info.plist" <<'PY'
import plistlib, subprocess, sys
from pathlib import Path
p = Path(sys.argv[1])
info = plistlib.loads(p.read_bytes())
assert info['CFBundleIdentifier'] == 'com.enermaxfr.maxps4.orbistest', info['CFBundleIdentifier']
info['MaxPS4SourceRevision'] = subprocess.check_output(['git','rev-parse','HEAD'], text=True).strip()
p.write_bytes(plistlib.dumps(info))
PY
EMBEDDED="$APP/Frameworks/MaxPS4FEXRuntime.framework"
test -f "$EMBEDDED/MaxPS4FEXRuntime"
codesign --force --sign - "$EMBEDDED"
codesign --verify --strict "$EMBEDDED"
codesign --force --sign - --entitlements MaxPS4-iOS/MaxPS4.entitlements "$APP"
codesign --verify --strict "$APP"
xcrun nm -u "$APP/MaxPS4OrbisTest" > build/diagnostics/orbis-test-imports.txt
for symbol in maxps4_fex_engine_open maxps4_fex_engine_prepare_orbis maxps4_fex_engine_run_orbis maxps4_fex_engine_last_cpu_report; do
  grep -q "_${symbol}$" build/diagnostics/orbis-test-imports.txt || {
    echo "Orbis Test missing runtime import: $symbol" >&2; exit 1;
  }
done
(cd build/orbis-test && zip -qry ../MaxPS4-Orbis-Test-SideStore.ipa Payload)
python3 - <<'PY'
import plistlib, zipfile
p='build/MaxPS4-Orbis-Test-SideStore.ipa'
with zipfile.ZipFile(p) as z:
    name='Payload/MaxPS4OrbisTest.app/Info.plist'
    info=plistlib.loads(z.read(name))
    assert info['CFBundleIdentifier']=='com.enermaxfr.maxps4.orbistest'
    assert 'Payload/MaxPS4OrbisTest.app/Frameworks/MaxPS4FEXRuntime.framework/MaxPS4FEXRuntime' in z.namelist()
print('Orbis Test IPA verified: separate bundle and embedded FEX runtime')
PY
