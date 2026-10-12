# Synthetic ELF to FEX: first integration stage

This additive experiment does not change existing app diagnostics, loaders, IPA
artifacts, JIT providers or interpreter behavior. It is available through
`MaxPS4SyntheticELFFEXBridge.run(url:)` in the FEX app configuration; it has no UI
entry point yet.

The fixture contains one RX PT_LOAD at guest address 0x1000: MOV EAX,40;
ADD EAX,2; HLT. The existing ELF loader creates simulated guest memory. Admission
checks require one region, exact size, readable/executable bytes and denied writes.
RWX, writable code, altered instructions and different entry points fail before
native initialization. No permissions are elevated. The native probe executes its
own byte-identical immutable copy using its existing guarded stack and W^X code
provider. Guest addresses are never converted to host pointers.

This is a fixed-program admission bridge, **not a general guest-memory mapping
implementation**. It neither passes the ELF allocation to FEX nor runs arbitrary
ELFs. It does not connect PS4 memory, imports, relocations, syscall ABI or graphics.
There is no interpreter fallback: only native status zero, backend FEX ARM64 JIT
and RAX=42 count as success. Permission errors remain errors.

## Checks

On a host with Swift, compile the admission test independently of the app:

```sh
swiftc MaxPS4-iOS/Sources/MaxPS4GuestMemory.swift \
  MaxPS4-iOS/Sources/MaxPS4ELFLoader.swift \
  MaxPS4-iOS/Sources/MaxPS4CPUPrototype.swift \
  MaxPS4-iOS/Sources/MaxPS4SyntheticELFFEXBridge.swift \
  MaxPS4-iOS/Tests/SyntheticELFFEXBridgeTest.swift \
  -o /tmp/maxps4-synthetic-bridge-test
/tmp/maxps4-synthetic-bridge-test
```

The injected probe tests admission and result accounting; it does not prove JIT
execution. This workspace lacks Swift, Xcode, Apple SDKs and FEXCore dependencies,
so neither this Swift compilation nor device execution has been verified here.
The base branch also has an existing missing `elfControlFlowPassed` argument in
the FEX diagnostic initializer; this additive change does not repair that file.

Next stages require an explicit region export API, a separately owned native
address space with guest-to-host translation, protection-aware FEX executable
range queries, fault containment and memory access tests before accepting stores
or control flow. Do not enable arbitrary guest code using the fixed probe's
no-fault assumptions. A physical authorized ARM64 device run must record native
status, backend, RAX and cleanup before claiming this bridge executes through JIT.
No real PS4 game has been tested or is claimed to work.
