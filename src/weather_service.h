/*
 * Weather Service & Open-Meteo Integration
 * Handles Geocoding, Atmospheric Telemetry, NVS Persistence, and Dual-Core Async Fetching
 * Target: JC3248W535EN (ESP32-S3, ESP-IDF 5.2)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char day_name[8];       /* e.g. "WED", "THU" */
    int temp_max;
    int temp_min;
    int rain_prob;          /* Percentage (0..100) */
    int weather_code;
    const char *icon;
    char condition[24];
} weather_forecast_day_t;

typedef struct {
    bool valid;
    char city_name[48];     /* e.g. "HANOI, VN" */
    float latitude;
    float longitude;
    bool is_fahrenheit;     /* false: °C, true: °F */

    /* Current Telemetry */
    int current_temp;
    int temp_max;
    int temp_min;
    int humidity;           /* Percentage (0..100) */
    float wind_speed;       /* km/h */
    int rain_probability;   /* Percentage (0..100) */
    int weather_code;
    char condition_str[32]; /* e.g. "LIGHT RAIN", "CLEAR SKY" */
    const char *icon;       /* LVGL Symbol */

    /* Solar Cycle */
    char sunrise_str[12];   /* e.g. "05:48" */
    char sunset_str[12];    /* e.g. "17:35" */
    int sunrise_minutes;    /* Minutes from midnight (e.g. 5*60+48) */
    int sunset_minutes;     /* Minutes from midnight (e.g. 17*60+35) */

    /* 4-Day Forecast */
    weather_forecast_day_t forecast[4];
} weather_data_t;

/* Initialize Weather Service and load saved location from NVS */
esp_err_t weather_service_init(void);

/* Trigger async weather update from Open-Meteo on Core 0 */
bool weather_service_fetch_async(void);

/* Search city asynchronously. Results checked via weather_service_get_search_status */
bool weather_service_search_city_async(const char *city_name);

typedef enum {
    CITY_SEARCH_IDLE = 0,
    CITY_SEARCH_IN_PROGRESS,
    CITY_SEARCH_SUCCESS,
    CITY_SEARCH_NOT_FOUND,
    CITY_SEARCH_ERROR
} city_search_status_t;

/* Check status of ongoing city search */
city_search_status_t weather_service_get_search_status(char *out_resolved_name, size_t max_len);

/* Get current active weather telemetry */
const weather_data_t *weather_service_get_data(void);

/* Toggle or set temperature unit (°C / °F) */
void weather_service_set_unit(bool is_fahrenheit);
bool weather_service_get_unit(void);

/* Get active location name */
const char *weather_service_get_city(void);

#ifdef __cplusplus
}
#endif
