# Orbis M1 — iPhone direct-mapping collision investigation

## Evidence from physical iPhone

Build PR head `0d4f2692d7bdee79e30cfe2bc8f135329b9f51f9`, run `38713227769`. The dedicated two-thread, three-TLS-module fixture reports `engine_open=0`, `jit_initialize=0`, then `m1_thread_api_status=-48`, `m1_threads=BLOCKED`, `reason=direct_mapping_collision`. No M1 CPU/TLS success on iPhone is established.

## Isolated diagnostics changes

- Log fixed-address Mach VM reservation failure with guest address, guard base, reservation byte count and kernel status. Never overwrite or silently relocate a FEX direct mapping.
- Test collision non-overwrite, zero accounting/regions after failure and clean retry once the owner releases the address.
- Enable the memory safety workflow on this experimental branch; workflow success must be observed before any claims of CI validation.

## Next engineering work

1. Capture the failing guest address and Mach status from an on-device diagnostic that exposes the error safely. Stderr-only logs are not automatically available in the copyable iPhone report.
2. Distinguish occupied address space from VM entitlement/protection failures. Never map with `VM_FLAGS_OVERWRITE` or `MAP_FIXED` to bypass the collision.
3. Implement and validate general guest-VA-to-host-storage translation in the pinned FEXCore backend for instruction fetch, scalar/vector/atomic accesses, cross-page operations and invalidation. A translated `GuestAddressSpace` alone does not make direct FEX accesses safe.
4. Keep the explicit `BLOCKED` result until two actual guest CPU reports and isolated TLS outputs pass on iPhone. The SCE dynamic TLS ABI and general thread scheduler remain out of scope.

This branch does not modify `main`, PR #199 or PR #201. Do not merge or declare games playable without additional evidence.
