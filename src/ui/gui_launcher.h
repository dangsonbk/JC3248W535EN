/*
 * Futuristic Sci-Fi / Industrial Mission Control Launcher
 * Inspired by Orion N5 Mars Expedition UI
 * Target: JC3248W535EN (ESP32-S3, 480x320 Landscape, LVGL 8)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void gui_launcher_init(void);

/**
 * @brief Get currently configured mission name (max 8 characters, default "KPARK K2")
 */
const char *gui_launcher_get_mission_name(void);

/**
 * @brief Set and save new mission name (max 8 characters)
 */
void gui_launcher_set_mission_name(const char *name);

#ifdef __cplusplus
}
#endif
