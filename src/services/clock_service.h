#ifndef CLOCK_SERVICE_H
#define CLOCK_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    uint8_t hour;     // 0 - 23
    uint8_t minute;   // 0 - 59
    char label[32];   // e.g. "MORNING ALARM"
} alarm_config_t;

typedef struct {
    int8_t tz_offset_hours;    // Local timezone (default: +7 for Vietnam)
    bool use_24h_format;       // true = 24H, false = 12H
    alarm_config_t alarm;
    int8_t world_tz_us;        // Default: -5 (New York / EST)
    int8_t world_tz_uk;        // Default: 0 (London / GMT)
    int8_t world_tz_jp;        // Default: +9 (Tokyo / JST)
} clock_config_t;

/**
 * @brief Initialize clock and alarm service, loads NVS settings,
 *        and starts alarm monitor watchdog.
 */
esp_err_t clock_service_init(void);

/**
 * @brief Get global clock configuration
 */
const clock_config_t *clock_service_get_config(void);

/**
 * @brief Update and persist clock configuration to NVS
 */
esp_err_t clock_service_save_config(const clock_config_t *cfg);

/**
 * @brief Get current local broken-down time with timezone applied
 */
void clock_service_get_local_time(struct tm *out_tm);

/**
 * @brief Get time for a specific UTC offset (e.g. -5, 0, +9)
 */
void clock_service_get_world_time(int8_t utc_offset_hours, int *out_hour, int *out_min);

/**
 * @brief Play audible alarm chime / melody through onboard I2S speaker
 */
void clock_service_play_chime(void);

/**
 * @brief Stop active alarm chime
 */
void clock_service_stop_chime(void);

/**
 * @brief Check if alarm is currently ringing
 */
bool clock_service_is_alarm_ringing(void);

/**
 * @brief Dismiss currently active alarm
 */
void clock_service_dismiss_alarm(void);

#ifdef __cplusplus
}
#endif

#endif // CLOCK_SERVICE_H
