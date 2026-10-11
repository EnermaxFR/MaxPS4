#!/usr/bin/env bash
# Build the existing SwiftUI application with one coherent, embedded Darwin runtime.
set -euo pipefail
SOURCE=${1:?Usage: scripts/build-fexcore-app.sh AETHER_CHECKOUT}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
mkdir -p build/frameworks build/diagnostics
bash scripts/build-fexcore-darwin-ios.sh "$SOURCE" build/fex-app-ios --runtime
FRAMEWORK=build/frameworks/MaxPS4FEXRuntime.framework
mkdir -p "$FRAMEWORK/Headers" "$FRAMEWORK/Modules"
cp build/fex-app-ios/libMaxPS4FEXRuntime.dylib "$FRAMEWORK/MaxPS4FEXRuntime"
xcrun install_name_tool -id '@rpath/MaxPS4FEXRuntime.framework/MaxPS4FEXRuntime' "$FRAMEWORK/MaxPS4FEXRuntime"
cp MaxPS4-iOS/Native/maxps4_fex_runtime.h "$FRAMEWORK/Headers/MaxPS4FEXRuntime.h"
cat > "$FRAMEWORK/Modules/module.modulemap" <<'MAP'
framework module MaxPS4FEXRuntime { umbrella header "MaxPS4FEXRuntime.h" export * }
MAP
python3 - "$FRAMEWORK" <<'PY'
import plistlib, sys
from pathlib import Path
p=Path(sys.argv[1]) / 'Info.plist'
p.write_bytes(plistlib.dumps(dict(CFBundleIdentifier='com.enermaxfr.maxps4.fexruntime',
    CFBundleExecutable='MaxPS4FEXRuntime', CFBundleName='MaxPS4FEXRuntime',
    CFBundlePackageType='FMWK', CFBundleShortVersionString='0.2', CFBundleVersion='130',
    CFBundleSupportedPlatforms=['iPhoneOS'], MinimumOSVersion='18.0')))
PY
# Ad-hoc signing authenticates the edited Mach-O only; SideStore must re-sign it.
codesign --force --sign - "$FRAMEWORK"
# Preserve the existing exact logo as an Xcode app-icon asset.
ASSETS=MaxPS4-iOS/Resources/Assets.xcassets
mkdir -p "$ASSETS/AppIcon.appiconset"
sips -z 1024 1024 MaxPS4-iOS/Resources/MaxPS4ExactIcon.png \
  --out "$ASSETS/AppIcon.appiconset/AppIcon.png" >/dev/null
python3 - "$ASSETS" <<'PYICON'
import json, sys
from pathlib import Path
root=Path(sys.argv[1])
(root/'Contents.json').write_text(json.dumps({'info':{'author':'xcode','version':1}}))
(root/'AppIcon.appiconset/Contents.json').write_text(json.dumps({
 'images':[{'filename':'AppIcon.png','idiom':'universal','platform':'ios','size':'1024x1024'}],
 'info':{'author':'xcode','version':1}}))
PYICON
xcodegen generate --spec MaxPS4-iOS/project.yml
xcodebuild -project MaxPS4-iOS/MaxPS4.xcodeproj -scheme MaxPS4 \
  -configuration Release -sdk iphoneos -destination 'generic/platform=iOS' \
  -derivedDataPath build/xcode-fex CODE_SIGNING_ALLOWED=NO \
  CURRENT_PROJECT_VERSION="${GITHUB_RUN_NUMBER:-130}" build \
  2>&1 | tee build/diagnostics/fex-app-build.log
mkdir -p build/Payload
rm -rf build/Payload/MaxPS4.app
cp -R build/xcode-fex/Build/Products/Release-iphoneos/MaxPS4.app build/Payload/
# Xcode's selected sources must call the real opaque ABI, not a legacy build path.
xcrun nm -u build/Payload/MaxPS4.app/MaxPS4 > build/diagnostics/fex-app-imports.txt
xcrun otool -L build/Payload/MaxPS4.app/MaxPS4 > build/diagnostics/fex-app-libraries.txt
xcrun nm -gU "$FRAMEWORK/MaxPS4FEXRuntime" > build/diagnostics/fex-app-runtime-exports.txt
for symbol in maxps4_fex_engine_open maxps4_fex_engine_initialize maxps4_fex_engine_cpuid maxps4_fex_engine_run; do
  grep -q "_${symbol}$" build/diagnostics/fex-app-imports.txt
  grep -q "_${symbol}$" build/diagnostics/fex-app-runtime-exports.txt
done
(cd build && zip -qry MaxPS4-FEXCore-SideStore.ipa Payload)
python3 scripts/verify-fexcore-ipa.py build/MaxPS4-FEXCore-SideStore.ipa \
  | tee build/diagnostics/fex-app-ipa-verification.txt
