#pragma once
#include <stdint.h>
// Host-side metadata for an owned, pending linear frame. No guest/host pointers.
typedef struct maxps4_videoout_frame {
    uint64_t ticket;
    uint64_t port;
    uint64_t flip_argument;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_in_pixels;
    uint32_t pixel_format;
    uint32_t buffer_index;
    uint32_t reserved;
    uint64_t byte_count;
} maxps4_videoout_frame;
