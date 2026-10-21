/*
 * Sci-Fi Weather Icon Set (24x24 Anti-Aliased Alpha Bitmaps)
 * High performance, zero RAM allocation, crisp LVGL 8 vector-quality rendering
 */
#pragma once

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEATHER_ICON_SUN = 0,
    WEATHER_ICON_PARTLY_CLOUDY,
    WEATHER_ICON_CLOUD,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_THUNDER,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_FOG,
    WEATHER_ICON_WIND,
    WEATHER_ICON_MOON,
    WEATHER_ICON_SUNRISE,
    WEATHER_ICON_SUNSET,
    WEATHER_ICON_COUNT
} weather_icon_type_t;

extern const lv_img_dsc_t img_weather_sun;
extern const lv_img_dsc_t img_weather_partly_cloudy;
extern const lv_img_dsc_t img_weather_cloud;
extern const lv_img_dsc_t img_weather_rain;
extern const lv_img_dsc_t img_weather_thunder;
extern const lv_img_dsc_t img_weather_snow;
extern const lv_img_dsc_t img_weather_fog;
extern const lv_img_dsc_t img_weather_wind;
extern const lv_img_dsc_t img_weather_moon;
extern const lv_img_dsc_t img_weather_sunrise;
extern const lv_img_dsc_t img_weather_sunset;

/* Map WMO weather code (and optional wind speed) to icon descriptor */
const lv_img_dsc_t *weather_icon_get_dsc(int wmo_code);

/* Get recommended theme color for this weather type */
lv_color_t weather_icon_get_color(int wmo_code);

#ifdef __cplusplus
}
#endif
