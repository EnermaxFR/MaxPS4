#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Stable C ABI consumed by Swift. Version 1: capability interrogation only.
uint32_t maxps4_native_abi_version(void);
uint32_t maxps4_native_capabilities(void);
int32_t maxps4_native_launch(const char *path);
#ifdef __cplusplus
}
#endif
