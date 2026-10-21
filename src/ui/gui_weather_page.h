/*
 * Detailed Atmospheric Telemetry & Solar Cycle Page
 * Target: JC3248W535EN (ESP32-S3, 480x320 Landscape, LVGL 8.4)
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open detailed weather subpage */
void gui_weather_page_open(void);

/* Close weather subpage */
void gui_weather_page_close(void);

#ifdef __cplusplus
}
#endif
