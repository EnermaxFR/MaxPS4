#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ShadPS4Options {
    const char* user_dir;
    int show_fps;
    int fullscreen;
    int network_enabled;
} ShadPS4Options;

int shadps4_init(const ShadPS4Options* options);
int shadps4_prepare_window(const char* eboot_path);
int shadps4_prepare_window_with_args(const char* eboot_path, const char* args_joined_by_newline);
int shadps4_run_loop(void);
void shadps4_stop(void);
void shadps4_toggle_pause(void);
int shadps4_is_paused(void);
int shadps4_has_presented_frame(void);
void shadps4_register_first_frame_callback(void (*callback)(void));
void* shadps4_get_uikit_window(void);
void shadps4_apply_touch_input(uint32_t buttons, int left_x, int left_y, int right_x, int right_y, int l2, int r2);

#ifdef __cplusplus
}
#endif
