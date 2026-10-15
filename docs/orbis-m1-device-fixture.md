# Dedicated M1 device fixture (PR #199)

The previous on-device probe passed V3 bytes with no TLS inputs to an API that
requires at least one TLS module. Its -EINVAL result did not attempt M1 threads.

The experimental app now embeds the exact native test bundle: a dedicated ELF
and two plaintext PRX PT_TLS templates (83 and 129). The default finite direct
layout starts at 16 GiB instead of 48 TiB, outside the macOS GPU carve-out. It
still requires iOS to admit those exact mappings and JIT execution. Collisions
are refused without overwriting mappings. No general FEX memory translation is
implemented, and device admissibility is not established by host CI.

`autotests=orbis-m1` calls the real two-context API twice. Each call creates both
CPU contexts and private stacks/TLS before deterministic run-to-completion
execution. The report checks joined IDs/self 1 and 2, native ARM64 JIT backend,
entry, aligned distinct stacks, distinct FS bases, guest RAX/memory 131, static
TLS/heap 22, module words 84/131, zero tails, four guest service calls and mutex
results 0/0. Budget and W+X rejection must leave reports unexecuted. A nonzero
runtime status produces BLOCKED with its numeric error and a fixed reason;
incorrect successful CPU results produce FAIL. General translation and the SCE
dynamic TLS ABI remain SKIP. Fixture FS offsets are not a DTV/__tls_get_addr ABI.

Packaging embeds fixture hashes, actual checkout SHA, PR head SHA and workflow
run ID before signing. On pull_request builds the checkout can legitimately be
a synthetic merge commit; `source` and `pr_head` identify both explicitly.
The IPA verifier requires the three fixture resources, checks their hashes and
requires the M1 thread runtime export. The app exports actual fixture hashes.

The existing native FEX tests and Swift bridge CI consume the same low-address
bundle. The ordinary native memory suite checks low-address mappings; ASan
memory tests explicitly retain high addresses because Linux ASan shadow memory
occupies the low fixture range. Those sanitizer results do not establish low
address CPU execution. No iPhone execution or working PS4 game is claimed.

The existing isolated Mission 198 IPA workflow and draft PR target are retained.
SideStore must re-sign the app and framework. A new hardware test should only be
requested after the new SHA's packaging and verification jobs succeed.

Initial CI at 8 GiB was refused on macOS with EADDRINUSE before CPU dispatch.
The revised candidate starts at 16 GiB, beyond the shared-cache range as well
as below the GPU carve-out. This remains subject to actual device admission.
