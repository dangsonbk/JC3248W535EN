/*
 * System Terminal Log & Telemetry HUD Ticker
 * Thread-safe logging, priority event messages, and automatic idle carousel
 * Target: JC3248W535EN (ESP32-S3, ESP-IDF 5.2)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize System Log subsystem */
void sys_log_init(void);

/* Post an event message to the terminal HUD (e.g. WiFi, Weather, Boot) */
void sys_log_set(const char *msg);
void sys_log_printf(const char *fmt, ...);

/* Get current active log message and its revision version */
const char *sys_log_get(uint32_t *out_version);

/* Generate the next periodic idle telemetry status string (RAM, PSRAM, Weather, WiFi, Uptime, etc.) */
void sys_log_get_next_idle(char *out_buf, size_t max_len);

#ifdef __cplusplus
}
#endif
