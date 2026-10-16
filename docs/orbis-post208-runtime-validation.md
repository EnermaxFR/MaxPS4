# Post-#208 runtime validation gates

This branch is isolated from `main`. The PRX/FEX bridge from #208 is a **translated-storage staging adapter**, not a working FEX CPU memory translator.

## Required regression matrix

| Scenario | Required result |
| --- | --- |
| Prepared PRX mapping in owned guest storage | Copied bytes, BSS zeroed, RX/RW/NX enforced |
| RELRO after relocation | Read-only, non-executable |
| Same-page aliases / overlapping guest ranges | Refused without mutating the active image |
| Arithmetic overflow / 32 MiB or 4096-mapping budget | Refused without publishing partial state |
| Allocation failure after partial mapping | Previous image retained; no leaked reservations |
| Replace then unload | Old leases invalidated and owned mappings released |
| Translated PRX mapped, FEX direct dispatch requested | Explicit `-ENOTSUP`; no host pointer, no guest instruction |
| FEX guest/host numeric address mismatch | Direct dispatch refused; no `-48` workaround claim |
| PRX import function/object kind mismatch | Unresolved; no synthetic HLE success |
| Encrypted Kero Blaster PFS mode `0x000d` | `eboot.bin` inaccessible; no runtime admission |

## Reproduce before any new PR or merge

```sh
bash scripts/test-orbis-analysis.sh
bash scripts/test-orbis-runtime.sh
bash scripts/test-orbis-v4-memory.sh
bash scripts/test-ps4-system-services.sh
```

Run the sanitizers and iPhone ARM64 compilation CI against the **exact commit** to be reviewed. These tests are necessary but do not prove physical-device execution. Compare installed IPA source SHA to that commit. On a real iPhone, collect the full FEX/JIT/memory/PRX/readiness diagnostics, including exact failure codes. No gameplay claim until legitimate readable title executable, dependencies, startup, rendering, input, audio and saves are observed.

## Next implementation boundary

Before attempting live translated FEX execution, specify and test a per-page guest-to-host translation contract for JIT instruction fetch, scalar/vector data reads/writes, invalidation, protection transitions and lifetime under concurrent access. Never assume numeric guest == host for translated storage. Reject unsupported operations explicitly, preserving rollback, W^X and occupied host mappings. Do not bypass DRM or decrypt PFS.

Do not merge to `main`; no merge of this experimental branch without explicit authorization.
