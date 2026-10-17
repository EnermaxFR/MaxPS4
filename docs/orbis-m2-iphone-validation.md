# iPhone M2 validation baseline

Device report (2026-10-15 14:37:32 UTC), source commit 4fc3ecbcf7966d19d4e7e46c635a6ae752768f2a: iOS 27.2 physical device; FEXCore ARM64 JIT backend 2; ELF load 0 (2 PT_LOAD); libkernel scePthreadSelf NID resolution and JUMP_SLOT 0; execution 0; observed libkernel calls 1; guest memory 0x10108 = 1 and 0x10100 = 2; RAX = 2; cleanup successful. This is a user-supplied diagnostic, not a CI result.

The unrelated PKG input returned execution -8 with no backend and guest entry 0. This does not test translated M2.

The successful ELF/libkernel diagnostic does not exercise the experimental translated guest-memory M2 path and cannot establish that direct_mapping_collision (-48) is resolved. The M2 public API must remain fail-closed.

Next steps: expose an isolated synthetic device diagnostic (not general ELF/PKG execution); exercise FEXCore guest-to-host translation with distinct guest/host VAs; record RAX/RIP/RSP, stop reason, memory, Mach errors, cache invalidation and repeated lifecycle; reject unsupported cross-mapping/masked/gather/fault paths safely. Require an actual physical-device regression reproducing the former collision before declaring -48 fixed.
