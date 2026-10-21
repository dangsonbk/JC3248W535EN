/*
 * Wi-Fi Controller & Dual-Core IPC Handler
 * Runs on Core 0 to offload networking from LVGL UI (Core 1)
 * Target: JC3248W535EN (ESP32-S3, ESP-IDF 5.2)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_WIFI_SCAN_RECORDS 16

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_scan_item_t;

typedef struct {
    uint16_t count;
    wifi_scan_item_t items[MAX_WIFI_SCAN_RECORDS];
} wifi_scan_result_t;

typedef enum {
    WIFI_STATUS_IDLE = 0,
    WIFI_STATUS_SCANNING,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED
} wifi_status_t;

/* Initialize Wi-Fi subsystem and Core 0 worker task */
esp_err_t wifi_controller_init(void);

/* Request asynchronous Wi-Fi scan on Core 0 (non-blocking for UI) */
bool wifi_controller_start_scan(void);

/* Check if scan results are available. Returns true and fills out_result if available */
bool wifi_controller_get_scan_results(wifi_scan_result_t *out_result);

/* Check if Wi-Fi scan is currently in progress */
bool wifi_controller_is_scanning(void);

/* Request connection to an SSID */
bool wifi_controller_connect(const char *ssid, const char *password);

/* Disconnect from current Wi-Fi network */
void wifi_controller_disconnect(void);

/* Check if Wi-Fi radio is enabled */
bool wifi_controller_is_enabled(void);

/* Enable or disable Wi-Fi radio power */
void wifi_controller_set_enabled(bool enabled);

/* Get current Wi-Fi connection status */
wifi_status_t wifi_controller_get_status(void);

/* Get currently connected IP string (e.g. "192.168.1.100" or "0.0.0.0") */
const char *wifi_controller_get_ip(void);

/* Get connected or connecting SSID */
const char *wifi_controller_get_connected_ssid(void);

/* NVS Storage for Wi-Fi credentials */
void wifi_controller_save_credentials(const char *ssid, const char *password);
bool wifi_controller_load_saved_credentials(char *out_ssid, size_t ssid_len, char *out_pass, size_t pass_len);
void wifi_controller_auto_connect(void);

/* NTP Time Synchronization */
bool wifi_controller_is_time_synced(void);

/* System Log Queue for Sci-Fi Terminal Typewriter effect */
void wifi_controller_set_log(const char *msg);
const char *wifi_controller_get_log(uint32_t *out_ver);

#ifdef __cplusplus
}
#endif
