# PM4/FEX/Metal integration validation

This branch merges main `98525349e8d4193de138c591131271a3086c12fb` into
`integration/pm4-fex-metal-main-157` from preparatory commit `4a77a94`.
The original experimental branch remains unchanged. No automatic merge is enabled.

## Conflict decisions

- Preserve main's unconditional `-ENOTSUP` iPhone FEX initialization and execution gates. The experimental prepared-region provider and its tests remain available, but do not unlock FEX on iPhone. Device proof and a reviewed allocation contract are required before changing these gates.
- Preserve bounded ELF execution and the same-arena PM4 snapshot on macOS, native Metal rendering, FEX diagnostics and main's StikDebug memory preflight UI.
- Preserve trap safety tests, signature audits, leak checks, ELF/FEX/Metal and PM4 tests in the SideStore workflow, plus main's parallel workflow fixes.
- Use project build `1000133`, above main's `1000132`. The Xcode build reads the project version without a script override, so IPA verification checks the same source of truth.

## Local verification

Executor: Linux x86_64. Seven C++ tests compiled with warnings as errors and
ASan/UBSan passed: PM4 type-3 framing, command interpreter, Metal draw plan,
guest PM4 submission, RIP-store admission, guest ELF memory, PS4 ELF/plaintext
SELF segments. LeakSanitizer fails under the executor's ptrace sandbox;
local runs use `ASAN_OPTIONS=detect_leaks=0`. CI retains its normal leak checks.

Signature audit: 3 tests passed. FEX diagnostics: 3 tests passed.
Fixed Stik script contract passed. Dispatcher initialization patch: 3 tests
passed. JIT provider patch: 3 tests passed against the actual pinned Aether
Darwin source. Generic Darwin adaptation patch: 3 tests passed against pinned
FEX `f2b679f6028ce1c38875233aecfcf5d3f8ebecec` (its required input, rather
than the already adapted Aether source). Workflow/project YAML parsing and shell
syntax checks passed.
Detailed local compiler/test logs are in `build/integration-157/` in the workspace.

## Required remote validation

This executor has no macOS ARM64 runtime, Metal device, Xcode/iPhone SDK,
codesign or physical iPhone. Genuine FEX CPU execution, Mach JIT pool tests,
Swift/Metal readback, Stik trap safety (its exported functions are Apple-only),
iOS compile/link, full app/SideStore IPA build and actual
signature audits must pass in the PR's macOS workflows. They are not claimed
as successful based on portable tests. Workflow diagnostic artifacts retain
build/runtime logs even on failure.

Keep the PR in draft and do not merge until the relevant head-commit checks
are green and changes have been reviewed. Physical iPhone JIT and a real PS4
title have not been validated; no game compatibility is claimed.
