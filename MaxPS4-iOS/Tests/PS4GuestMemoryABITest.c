#include "../Native/maxps4_fex_runtime.h"
#include <stddef.h>
#include <stdint.h>

// Compile-only ABI contract: Swift's bridging header consumes this as C,
// not C++. Linkage is intentionally not required by this test.
typedef int32_t (*stage_fn)(maxps4_fex_engine *, const uint8_t *,
                            size_t, uint64_t *, uint32_t *, uint32_t *);
typedef int32_t (*read_fn)(maxps4_fex_engine *, uint64_t, uint8_t *, size_t);
typedef int32_t (*write_fn)(maxps4_fex_engine *, uint64_t, const uint8_t *, size_t);
typedef int32_t (*query_fn)(maxps4_fex_engine *, uint64_t, size_t,
                            uint32_t, uint32_t *);

static stage_fn stage_api = &maxps4_fex_engine_stage_ps4;
static read_fn read_api = &maxps4_fex_engine_read_ps4;
static read_fn fetch_api = &maxps4_fex_engine_fetch_ps4;
static write_fn write_api = &maxps4_fex_engine_write_ps4;
static query_fn query_api = &maxps4_fex_engine_query_ps4;
typedef int32_t (*contiguous_fn)(maxps4_fex_engine *, uint64_t, size_t,
                                 uint32_t, size_t *);
static contiguous_fn contiguous_api = &maxps4_fex_engine_contiguous_ps4;
typedef int32_t (*run_staged_fn)(maxps4_fex_engine *, uint32_t,
                                uint64_t *, uint32_t *, uint64_t *);
static run_staged_fn run_staged_api = &maxps4_fex_engine_run_staged_ps4;

int main(void) {
    return !(stage_api && read_api && fetch_api && write_api && query_api && contiguous_api && run_staged_api);
}

