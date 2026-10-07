#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stable boundary between the MaxPS4 iOS frontend and the PS4 emulation runtime.
// READY means the validated shadPS4/FEX guest backend is actually linked into
// the current binary. It does not claim that the PS4 executable loader is wired yet.

typedef enum MaxPS4BackendState {
    MAXPS4_BACKEND_NOT_LINKED = 0,
    MAXPS4_BACKEND_READY = 1,
    MAXPS4_BACKEND_ERROR = 2,
} MaxPS4BackendState;

MaxPS4BackendState maxps4_backend_state(void);
const char *maxps4_backend_name(void);
const char *maxps4_backend_diagnostic(void);
void maxps4_backend_live_diagnostic(char *out, size_t out_size);
void maxps4_backend_live_output(char *out, size_t out_size);

// Host controller snapshot forwarded to the FEX guest compatibility bridge.
// This is a MaxPS4-private userspace ABI, not a claim of PS4 kernel compatibility.
void maxps4_backend_set_controller_state(unsigned int buttons,
                                         float left_x, float left_y,
                                         float right_x, float right_y,
                                         float left_trigger, float right_trigger);

typedef struct MaxPS4GuestRenderState {
    unsigned int sequence;
    float x;
    float y;
    float scale;
    float red;
    float green;
    float blue;
    float alpha;
} MaxPS4GuestRenderState;

// Returns true after an x86-64 guest has submitted at least one private MaxPS4
// render command. This is a legal smoke-test ABI, not a PS4 graphics API.
bool maxps4_backend_guest_render_state(MaxPS4GuestRenderState *out);

#define MAXPS4_GUEST_FRAME_MAX_RECTS 8

typedef struct MaxPS4GuestRect {
    float x;
    float y;
    float scale;
    float red;
    float green;
    float blue;
    float alpha;
} MaxPS4GuestRect;

typedef struct MaxPS4GuestFrame {
    unsigned int sequence;
    unsigned int rect_count;
    float clear_red;
    float clear_green;
    float clear_blue;
    float clear_alpha;
    MaxPS4GuestRect rects[MAXPS4_GUEST_FRAME_MAX_RECTS];
} MaxPS4GuestFrame;

// Atomic guest frame snapshot produced by the private legal render queue ABI.
bool maxps4_backend_guest_frame(MaxPS4GuestFrame *out);

#define MAXPS4_GUEST_SCENE_MAX_PRIMITIVES 12

typedef enum MaxPS4GuestPrimitiveType {
    MAXPS4_GUEST_PRIMITIVE_RECT = 1,
    MAXPS4_GUEST_PRIMITIVE_TRIANGLE = 2,
    MAXPS4_GUEST_PRIMITIVE_TEXTURED_QUAD = 3,
    MAXPS4_GUEST_PRIMITIVE_GUEST_TEXTURED_QUAD = 4,
} MaxPS4GuestPrimitiveType;

typedef struct MaxPS4GuestPrimitive {
    unsigned int type;
    float x;
    float y;
    float width;
    float height;
    float rotation;
    float red;
    float green;
    float blue;
    float alpha;
    unsigned int texture_id;
} MaxPS4GuestPrimitive;

typedef struct MaxPS4GuestSceneFrame {
    unsigned int sequence;
    unsigned int primitive_count;
    float clear_red;
    float clear_green;
    float clear_blue;
    float clear_alpha;
    MaxPS4GuestPrimitive primitives[MAXPS4_GUEST_SCENE_MAX_PRIMITIVES];
} MaxPS4GuestSceneFrame;

// Typed primitive command buffer used by the next legal graphics bridge stage.
bool maxps4_backend_guest_scene_frame(MaxPS4GuestSceneFrame *out);

#define MAXPS4_GUEST_TEXTURE_MAX_BYTES 4096

typedef struct MaxPS4GuestTexture {
    unsigned int sequence;
    unsigned int texture_id;
    unsigned int width;
    unsigned int height;
    unsigned int byte_count;
    unsigned char rgba[MAXPS4_GUEST_TEXTURE_MAX_BYTES];
} MaxPS4GuestTexture;

// Latest guest-uploaded legal RGBA8 texture resource, if one exists.
bool maxps4_backend_guest_texture(MaxPS4GuestTexture *out);
bool maxps4_backend_self_test(void);
// Validates an imported PS4 SELF/ELF executable using the same structural
// requirements as the locked shadPS4 loader before any runtime handoff.
bool maxps4_backend_validate_executable(const char *path);
bool maxps4_backend_boot(const char *path);
void maxps4_backend_stop(void);

#ifdef __cplusplus
}
#endif
