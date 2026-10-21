/*
 * Weather & Atmosphere Configuration Page
 * Supports Temperature Unit Toggle (°C / °F) and Geocoding City Search
 * Target: JC3248W535EN (ESP32-S3, 480x320 Landscape, LVGL 8.4)
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open weather configuration subpage */
void gui_weather_settings_open(lv_obj_t *parent);

/* Close weather configuration subpage */
void gui_weather_settings_close(void);

#ifdef __cplusplus
}
#endif
