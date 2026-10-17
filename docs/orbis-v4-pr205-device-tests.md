# PR #205 experimental iPhone validation

Build only the artifact produced by the SideStore workflow on the exact PR head.
Check MaxPS4SourceRevision in the IPA Info.plist against that SHA. Retain the
workflow URL, artifact ID, IPA SHA-256 and pre-resigning signature audit.
SideStore must re-sign both the app and embedded MaxPS4FEXRuntime.framework.

On a physical iPhone, record model, iOS version, SideStore version, signing
identity/profile and source SHA. Enable JIT through the existing authorized
debugger flow. Capture engine_open and initialize statuses and the selected
backend before any execution test.

1. Repeat the existing bounded ELF/libkernel diagnostic three times, including
   close/reopen and background/foreground. Record RAX, guest memory, call count
   and cleanup. A successful synthetic ELF is not a PS4 game result.
2. Repeat the original direct_mapping_collision (-48) input unchanged. Preserve
   guest range, guard-inclusive reservation length, Mach return code and errno.
   Never use VM_FLAGS_OVERWRITE or MAP_FIXED to force an occupied mapping.
3. The public V4 synthetic API intentionally returns -ENOTSUP with stop reason
   TRANSLATION_REQUIRED after successful staging on an initialized engine.
   cpu_valid, backend and CPU registers remain zero. This refusal is expected.
4. M1 direct memory/TLS and deterministic thread tests in CI are host results.
   They do not establish that the same guest VA layout is available on iPhone.
5. Do not run arbitrary translated guest code on the device: unsupported
   masked/gather/cache/ValidateCode paths, split data operands and native JIT
   data-fault recovery remain incomplete. Checked -EFAULT recovery in the
   mapping owner is separate from native signal/fault recovery.

No physical iPhone was available in this development session. Error -48 remains
unverified. Do not close #202 or advertise a working PS4 game from this build.

Observed integration failure: macOS ARM64 run 38767323866 on 0feef0b completed
all single-thread M1 cases but rejected the two-thread queue with -48 before
packaging. Linux native ARM64 and simulator passed the low-profile thread cases.
Darwin host-linked tests now select MAXPS4_M1_HIGH_VA_HOST_TEST, a separate high
fixture profile. iOS builds keep the default low profile. This host test selection
does not resolve the low-profile collision or establish iPhone compatibility.
