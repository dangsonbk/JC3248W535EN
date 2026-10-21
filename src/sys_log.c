/*
 * System Terminal Log & Telemetry HUD Ticker Implementation
 */

#include "sys_log.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "weather_service.h"
#include "wifi_controller.h"

static char s_log_buffer[128] = "INITIALIZING SYSTEM...";
static uint32_t s_log_version = 1;
static SemaphoreHandle_t s_log_mutex = NULL;
static uint8_t s_idle_step = 0;

void sys_log_init(void)
{
    if (!s_log_mutex) {
        s_log_mutex = xSemaphoreCreateMutex();
    }
}

void sys_log_set(const char *msg)
{
    if (!msg) return;
    if (!s_log_mutex) sys_log_init();

    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_log_buffer, msg, sizeof(s_log_buffer) - 1);
        s_log_buffer[sizeof(s_log_buffer) - 1] = '\0';
        s_log_version++;
        xSemaphoreGive(s_log_mutex);
    }
}

void sys_log_printf(const char *fmt, ...)
{
    if (!fmt) return;
    char temp[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);
    sys_log_set(temp);
}

const char *sys_log_get(uint32_t *out_version)
{
    if (out_version) {
        *out_version = s_log_version;
    }
    return s_log_buffer;
}

void sys_log_get_next_idle(char *out_buf, size_t max_len)
{
    if (!out_buf || max_len == 0) return;

    switch (s_idle_step % 5) {
        case 0: {
            /* 1. Memory Telemetry */
            uint32_t free_sram = esp_get_free_heap_size() / 1024;
            uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
            snprintf(out_buf, max_len, "RAM: %luK :: PSRAM: %luK FREE", (unsigned long)free_sram, (unsigned long)free_psram);
            break;
        }
        case 1: {
            /* 2. Weather Status with Last Sync Time */
            const weather_data_t *w = weather_service_get_data();
            const char *sync_t = weather_service_get_last_sync_time();
            if (w && w->valid) {
                int t = w->is_fahrenheit ? (int)roundf((w->current_temp * 9.0f / 5.0f) + 32.0f) : w->current_temp;
                snprintf(out_buf, max_len, "METEO: %d%s :: %s :: @%s",
                         t, w->is_fahrenheit ? "F" : "C", w->condition_str, sync_t);
            } else {
                snprintf(out_buf, max_len, "METEO :: SCANNING ATMOSPHERE");
            }
            break;
        }
        case 2: {
            /* 3. Wi-Fi / Orbital Link Telemetry */
            wifi_status_t st = wifi_controller_get_status();
            if (st == WIFI_STATUS_CONNECTED) {
                wifi_ap_record_t ap;
                if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                    snprintf(out_buf, max_len, "LINK: %s :: %d dBm", (char*)ap.ssid, ap.rssi);
                } else {
                    snprintf(out_buf, max_len, "LINK: CONNECTED :: STABLE");
                }
            } else {
                snprintf(out_buf, max_len, "LINK: STANDBY :: SCAN READY");
            }
            break;
        }
        case 3: {
            /* 4. CPU & Core Telemetry */
            int64_t sec = esp_timer_get_time() / 1000000;
            int hrs = (int)(sec / 3600);
            int mins = (int)((sec % 3600) / 60);
            int s = (int)(sec % 60);
            snprintf(out_buf, max_len, "UPTIME: %02d:%02d:%02d :: CORE 240MHz", hrs, mins, s);
            break;
        }
        case 4: {
            /* 5. Mission HUD Diagnostics */
            snprintf(out_buf, max_len, "HUD STATUS :: SYSTEMS NOMINAL");
            break;
        }
    }
    s_idle_step++;
}
