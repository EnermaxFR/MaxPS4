#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __OBJC__
#import <Metal/Metal.h>
#ifdef __cplusplus
extern "C" {
#endif
// Diagnostic native-shader draw only. Zero is success. No PS4 shader translation.
// drawn is reset on failure; all commands are checked before touching the encoder.
int32_t maxps4_pm4_encode_triangle(const uint32_t *words, size_t word_count,
    id<MTLRenderCommandEncoder> encoder, id<MTLRenderPipelineState> pipeline,
    size_t *drawn);
#ifdef __cplusplus
}
#endif
#endif
