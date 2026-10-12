# Synthetic ELF to FEX

The original injectable Swift admission test checks an exact immutable fixture:
MOV EAX,40; ADD EAX,2; HLT at guest entry 0x1000. It verifies simulated RX
permissions and rejects altered snapshots or interpreter results.

The FEX app's `MaxPS4SyntheticELFFEXBridge.run(url:)` now passes the actual ELF
file bytes to `maxps4_fex_engine_run_elf`. The native loader independently checks
ELF64 metadata and its bounded instruction stream, maps all PT_LOAD segments,
and starts FEX at the mapped ELF entry. The diagnostic uses the same native
path. Native execution also admits different MOV/ADD immediates and entries;
the old injected closure is a fixture test, not the real execution boundary.

See [the execution contract and validation scope](elf64-fex-execution-144.md).
No general ELF, PS4 syscall, game compatibility or physical iPhone JIT validation
is implied. The native ELF entry point has no interpreter fallback.

Standalone Swift fixture admission tests:

```sh
swiftc MaxPS4-iOS/Sources/MaxPS4GuestMemory.swift \
  MaxPS4-iOS/Sources/MaxPS4ELFLoader.swift \
  MaxPS4-iOS/Sources/MaxPS4CPUPrototype.swift \
  MaxPS4-iOS/Sources/MaxPS4SyntheticELFFEXBridge.swift \
  MaxPS4-iOS/Tests/SyntheticELFFEXBridgeTest.swift \
  -o /tmp/maxps4-synthetic-bridge-test
/tmp/maxps4-synthetic-bridge-test
```

The SideStore workflow additionally tests the real Swift/native ELF boundary
with RAX=123 and a rejected RET image on ARM64 macOS. Device proof remains
separate and is required before merging.
