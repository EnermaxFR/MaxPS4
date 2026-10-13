#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct maxps4_orbis_image maxps4_orbis_image;
typedef struct {
    const char* path;
    const uint8_t* bytes;
    size_t size;
    uint64_t load_bias;
    const char* const* dependencies;
    size_t dependency_count;
} maxps4_orbis_module;
typedef struct {
    uint64_t address, size;
    uint32_t permissions;
} maxps4_orbis_mapping;
// No CPU execution. Caller serializes calls and keeps inputs immutable during link.
// Output handle must initially be null; failures leave it null. Diagnostic is
// always terminated when capacity > 0. Dependencies are explicit logical paths.
int32_t maxps4_orbis_link(const maxps4_orbis_module* modules, size_t count,
    size_t memory_budget, maxps4_orbis_image** output, char* diagnostic, size_t capacity);
void maxps4_orbis_close(maxps4_orbis_image* image);
size_t maxps4_orbis_mapping_count(const maxps4_orbis_image* image);
int32_t maxps4_orbis_get_mapping(const maxps4_orbis_image* image, size_t index,
    maxps4_orbis_mapping* output);
int32_t maxps4_orbis_entry(const maxps4_orbis_image* image, const char* path, uint64_t* output);
// Permission-checked guest read; failed calls leave output bytes untouched.
// Host addresses never cross this ABI. A future FEX address-space backend can
// copy mappings using these calls, then enforce their guest permissions.
int32_t maxps4_orbis_read(const maxps4_orbis_image* image, uint64_t address,
    uint8_t* output, size_t size);
#ifdef __cplusplus
}
#endif
