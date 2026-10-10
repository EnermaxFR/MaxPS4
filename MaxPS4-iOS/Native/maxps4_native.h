#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Stable C ABI consumed by Swift. Version 1: capability interrogation only.
uint32_t maxps4_native_abi_version(void);
uint32_t maxps4_native_capabilities(void);
// 1: upstream guest CPU dispatch regression passed; 0: failed.
// Not a JIT or PS4 game-launch capability.
int32_t maxps4_native_aether_cpu_probe(void);
int32_t maxps4_native_launch(const char *path);
#ifdef __cplusplus
}
#endif
