#pragma once
// C ABI of one Orbis V4 game session (OrbisV4GameSession.hpp) for the app:
// links and runs a title directory on the calling thread until it exits,
// fails or `cancel` becomes non-zero. Implemented in MaxPS4FEXRuntime.
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct maxps4_game_callbacks {
    void* context;
    // Every presented frame, RGBA8 (width * height * 4 bytes, valid only during
    // the call). Writes the controller state for the next frame to *buttons
    // (SCE_PAD_BUTTON_* bits). Returns 0 to continue.
    int (*present)(void* context, uint32_t width, uint32_t height, const uint8_t* rgba, uint32_t* buttons);
    // 48 kHz interleaved stereo S16 samples. May be NULL.
    void (*audio)(void* context, const int16_t* pcm, size_t frames);
    // One diagnostic line, NUL-terminated, without the newline. May be NULL.
    void (*log)(void* context, const char* line);
} maxps4_game_callbacks;
// game_dir holds eboot.bin and sce_module/{libc,libSceFios2}.prx (the title's
// own files, read only); names_tsv is the import name table
// (docs/mission64-sonic-mania-names.tsv format). Returns the session status
// (0: the title exited normally), or a negative errno before the session runs.
int maxps4_game_run(const char* game_dir, const char* names_tsv, const maxps4_game_callbacks* callbacks,
                    const volatile int32_t* cancel);
#ifdef __cplusplus
}
#endif
