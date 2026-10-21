#ifndef GUI_BOOT_SCREEN_H
#define GUI_BOOT_SCREEN_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*gui_boot_done_cb_t)(void);

/**
 * @brief Start the Cyberpunk Root Access boot sequence.
 * @param on_done Callback when boot sequence completes or is skipped.
 */
void gui_boot_screen_start(gui_boot_done_cb_t on_done);

/**
 * @brief Check if boot screen is enabled in NVS
 */
bool gui_boot_screen_is_enabled(void);

/**
 * @brief Set boot screen enabled status in NVS
 */
void gui_boot_screen_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // GUI_BOOT_SCREEN_H
