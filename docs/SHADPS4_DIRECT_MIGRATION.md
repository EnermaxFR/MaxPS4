# MaxPS4 direct shadPS4 migration

## Target architecture

MaxPS4 SwiftUI frontend -> MaxPS4 iOS bridge -> official shadPS4 engine

AetherPS4 is not part of the target architecture and must not be a build or runtime dependency of the release branch.

## Upstream

Official engine: https://github.com/shadps4-emu/shadPS4

MaxPS4 will preserve the copyright, license and source-distribution obligations of shadPS4 and all other bundled open-source components.

## Migration gates

1. Keep the last known compiling native MaxPS4 build only as a temporary technical reference.
2. Pin an official shadPS4 revision and create a clean iOS cross-compilation probe.
3. Implement the MaxPS4-owned iOS C bridge instead of importing AetherPS4's iOS frontend/bridge.
4. Implement iOS window, Vulkan, JIT, input and lifecycle adaptations in MaxPS4-owned source files.
5. Build the MaxPS4 SwiftUI app against that bridge.
6. Verify the final build contains no AetherPS4 source, resources, names or runtime/build dependencies.
7. Add required GPL/open-source notices and corresponding-source information before any public release.

## Current upstream constraint

The official shadPS4 project currently targets desktop Apple builds and does not expose the Aether-specific `shadps4_ios` API used by the temporary MaxPS4 build. The direct migration therefore requires a MaxPS4-owned iOS port/bridge rather than simply changing the clone URL.

## Release rule

Do not publish MaxPS4 v0.1 Alpha until the direct shadPS4 build path passes and the release artifact has been audited for AetherPS4 remnants and required open-source notices.
