#ifndef GUI_DISPLAY_SETTINGS_H
#define GUI_DISPLAY_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize display brightness and sleep timer subsystem.
 *        Loads settings from NVS, sets initial brightness,
 *        and starts inactivity watchdog.
 */
void gui_display_settings_init(void);

/**
 * @brief Open the Display settings subpage
 * @param parent The parent view (view_settings)
 */
void gui_display_settings_open(lv_obj_t *parent);

/**
 * @brief Get currently configured brightness (10 - 100)
 */
uint8_t gui_display_get_brightness(void);

/**
 * @brief Set brightness and apply immediately
 */
void gui_display_set_brightness(uint8_t val);

/**
 * @brief Get whether auto sleep is enabled
 */
bool gui_display_get_sleep_enabled(void);

/**
 * @brief Set auto sleep enable
 */
void gui_display_set_sleep_enabled(bool enabled);

/**
 * @brief Get sleep timeout in seconds
 */
uint32_t gui_display_get_sleep_timeout(void);

/**
 * @brief Set sleep timeout in seconds (e.g. 600, 1800, 3600, 7200)
 */
void gui_display_set_sleep_timeout(uint32_t timeout_sec);

/**
 * @brief Wake up display if currently asleep
 */
void gui_display_wake_up(void);

#ifdef __cplusplus
}
#endif

#endif // GUI_DISPLAY_SETTINGS_H
