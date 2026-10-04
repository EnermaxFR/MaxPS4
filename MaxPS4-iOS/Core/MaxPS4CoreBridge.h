// SPDX-License-Identifier: GPL-2.0-or-later
// MaxPS4 iOS bridge — independent frontend integration for the upstream shadPS4 core.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MaxPS4CoreOptions {
    const char *user_directory;
    int show_fps;
    int network_enabled;
} MaxPS4CoreOptions;

// Stable C ABI exposed to the Swift frontend and implemented directly against
// the official upstream shadPS4 Core::Emulator interface.
int maxps4_core_initialize(const MaxPS4CoreOptions *options);
int maxps4_core_prepare_game(const char *eboot_path);
int maxps4_core_run(void);
void maxps4_core_shutdown(void);
int maxps4_core_is_running(void);

#ifdef __cplusplus
}
#endif
