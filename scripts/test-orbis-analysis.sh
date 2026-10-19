#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-build/orbis}
mkdir -p "$OUT"
python3 -B scripts/test-report-orbis-imports.py
for suite in OrbisSymbolsTest OrbisDynamicTagsTest OrbisELFHeadersTest OrbisModuleParamTest OrbisV4TLSRuntimeTest OrbisV4TLSRelocationsTest OrbisV4TLSLayoutTest OrbisV4TLSExtractTest OrbisV4TLSMultiTest OrbisV4TLSTest OrbisBootMission43Test OrbisLoaderMission42Test LinearTextureSnapshotTest OrbisV4PngHeaderTest OrbisV4NpTrophyTest OrbisV4NpManagerTest OrbisV4ProviderFunctionsTest OrbisV4NpCommonTest OrbisV4DependencyDiagnosticTest; do
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    "MaxPS4-iOS/Tests/$suite.cpp" -o "$OUT/$suite"
  python3 scripts/run-bounded.py 30 "$OUT/$suite.txt" "$OUT/$suite"
done
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4GnmDriverTest.cpp -o "$OUT/v4-gnm-driver-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-gnm-driver-tests.txt" "$OUT/v4-gnm-driver-test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4ErrorDialogTest.cpp -o "$OUT/v4-error-dialog-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-error-dialog-tests.txt" "$OUT/v4-error-dialog-test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4AudioOutTest.cpp -o "$OUT/v4-audio-out-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-audio-out-tests.txt" "$OUT/v4-audio-out-test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4AppContentTest.cpp -o "$OUT/v4-app-content-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-app-content-tests.txt" "$OUT/v4-app-content-test"
python3 -B scripts/test-inspect-orbis-self.py
bash scripts/build-orbis-v4-homebrew.sh "$OUT/global-boot" homebrew/orbis-v4-global-startup/start.c
python3 scripts/build-orbis-v4-self.py "$OUT/global-boot/homebrew.elf" "$OUT/global-boot/homebrew.self"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4ExperimentalSELFTest.cpp -o "$OUT/v4-experimental-self-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-experimental-self-tests.txt" "$OUT/v4-experimental-self-test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4SELFTest.cpp -o "$OUT/v4-self-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-self-tests.txt" \
  "$OUT/v4-self-test" "$OUT/global-boot/homebrew.elf" "$OUT/global-boot/homebrew.self"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4EbootTest.cpp -o "$OUT/v4-eboot-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-eboot-tests.txt" \
  "$OUT/v4-eboot-test" "$OUT/global-boot/homebrew.elf"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4GlobalStartupTest.cpp -o "$OUT/v4-global-startup-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-global-startup-tests.txt" \
  "$OUT/v4-global-startup-test" "$OUT/global-boot/homebrew.elf"
bash scripts/build-orbis-v4-homebrew.sh "$OUT/c-boot" homebrew/orbis-v4-c-startup/start.c
MAXPS4_HOMEBREW_C_OPT_LEVEL=0 bash scripts/build-orbis-v4-homebrew.sh "$OUT/c-boot-o0" homebrew/orbis-v4-c-startup/start.c
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4CGameBootTest.cpp -o "$OUT/v4-c-boot-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-c-boot-tests.txt" "$OUT/v4-c-boot-test" \
  "$OUT/c-boot/homebrew.elf" "$OUT/c-boot-o0/homebrew.elf"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4GuestMemoryServiceTest.cpp \
  MaxPS4-iOS/Native/maxps4_guest_elf.cpp MaxPS4-iOS/Native/maxps4_ps4_libkernel.cpp \
  MaxPS4-iOS/Native/maxps4_ps4_threads.cpp MaxPS4-iOS/Native/maxps4_ps4_virtual_memory.cpp \
  -o "$OUT/v4-guest-memory-service-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-guest-memory-service-tests.txt" "$OUT/v4-guest-memory-service-test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4ImportsTest.cpp -o "$OUT/v4-imports-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-imports-tests.txt" "$OUT/v4-imports-test"
bash scripts/build-orbis-v4-homebrew.sh "$OUT/startup" homebrew/orbis-v4-startup/start.S
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4StartupTest.cpp -o "$OUT/v4-startup-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-startup-tests.txt" "$OUT/v4-startup-test" "$OUT/startup/homebrew.elf"
bash scripts/build-orbis-v4-homebrew.sh "$OUT/homebrew"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4HomebrewTest.cpp -o "$OUT/v4-homebrew-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-homebrew-tests.txt" "$OUT/v4-homebrew-test" "$OUT/homebrew/homebrew.elf"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisPRXFEXBridgeTest.cpp -o "$OUT/prx-fex-bridge-test"
python3 scripts/run-bounded.py 30 "$OUT/prx-fex-bridge-tests.txt" "$OUT/prx-fex-bridge-test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisPRXTranslationTest.cpp -o "$OUT/prx-translation-test"
python3 scripts/run-bounded.py 30 "$OUT/prx-translation-tests.txt" "$OUT/prx-translation-test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisV4PRXLaunchTest.cpp -o "$OUT/v4-prx-launch-test"
python3 scripts/run-bounded.py 30 "$OUT/v4-prx-launch-tests.txt" "$OUT/v4-prx-launch-test"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/OrbisMetadataTest.cpp MaxPS4-iOS/Native/OrbisAnalysisBridge.cpp \
  MaxPS4-iOS/Native/maxps4_guest_elf.cpp MaxPS4-iOS/Native/maxps4_ps4_libkernel.cpp \
  MaxPS4-iOS/Native/maxps4_ps4_virtual_memory.cpp MaxPS4-iOS/Native/maxps4_ps4_threads.cpp \
  -o "$OUT/orbis-test"
python3 scripts/run-bounded.py 30 "$OUT/native-tests.txt" "$OUT/orbis-test" "$OUT/synthetic-sce.prx"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  MaxPS4-iOS/Tests/GameDirectoryPreflightTest.cpp MaxPS4-iOS/Native/OrbisAnalysisBridge.cpp \
  MaxPS4-iOS/Native/PS4PKGBridge.cpp \
  MaxPS4-iOS/Native/maxps4_guest_elf.cpp MaxPS4-iOS/Native/maxps4_ps4_libkernel.cpp \
  MaxPS4-iOS/Native/maxps4_ps4_virtual_memory.cpp MaxPS4-iOS/Native/maxps4_ps4_threads.cpp \
  -o "$OUT/game-directory-test"
python3 scripts/run-bounded.py 30 "$OUT/game-directory-tests.txt" "$OUT/game-directory-test"
