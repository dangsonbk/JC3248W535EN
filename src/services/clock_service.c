#include "clock_service.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_player.h"
#include "lvgl.h"

static const char *TAG = "CLOCK_SVC";

#define NVS_NAMESPACE       "clk_cfg"
#define NVS_KEY_TZ_OFFSET   "tz_off"
#define NVS_KEY_24H         "use_24h"
#define NVS_KEY_ALARM_EN    "alm_en"
#define NVS_KEY_ALARM_H     "alm_h"
#define NVS_KEY_ALARM_M     "alm_m"

static clock_config_t s_cfg = {
    .tz_offset_hours = 7,       // UTC+7 (Vietnam / Hanoi / Bangkok)
    .use_24h_format = true,
    .alarm = {
        .enabled = false,
        .hour = 7,
        .minute = 0,
        .label = "ALARM"
    },
    .world_tz_us = -5,          // UTC-5 (New York / US)
    .world_tz_uk = 0,           // UTC+0 (London / UK)
    .world_tz_jp = 9            // UTC+9 (Tokyo / Japan)
};

static bool s_is_ringing = false;
static TaskHandle_t s_chime_task_handle = NULL;
static lv_timer_t *s_alarm_timer = NULL;
static uint8_t s_last_checked_min = 255;

/* --------------------------------------------------------------------
 * NVS Operations
 * -------------------------------------------------------------------- */
static void load_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        int8_t s8_val = 0;
        if (nvs_get_i8(handle, NVS_KEY_TZ_OFFSET, &s8_val) == ESP_OK) {
            s_cfg.tz_offset_hours = s8_val;
        }
        uint8_t u8_val = 0;
        if (nvs_get_u8(handle, NVS_KEY_24H, &u8_val) == ESP_OK) {
            s_cfg.use_24h_format = (u8_val != 0);
        }
        if (nvs_get_u8(handle, NVS_KEY_ALARM_EN, &u8_val) == ESP_OK) {
            s_cfg.alarm.enabled = (u8_val != 0);
        }
        if (nvs_get_u8(handle, NVS_KEY_ALARM_H, &u8_val) == ESP_OK) {
            if (u8_val < 24) s_cfg.alarm.hour = u8_val;
        }
        if (nvs_get_u8(handle, NVS_KEY_ALARM_M, &u8_val) == ESP_OK) {
            if (u8_val < 60) s_cfg.alarm.minute = u8_val;
        }
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Clock config loaded: TZ=UTC%+d, 24H=%d, Alarm=%02d:%02d (En=%d)",
             s_cfg.tz_offset_hours, s_cfg.use_24h_format,
             s_cfg.alarm.hour, s_cfg.alarm.minute, s_cfg.alarm.enabled);
}

esp_err_t clock_service_save_config(const clock_config_t *cfg)
{
    if (cfg) memcpy(&s_cfg, cfg, sizeof(clock_config_t));
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_set_i8(handle, NVS_KEY_TZ_OFFSET, s_cfg.tz_offset_hours);
        nvs_set_u8(handle, NVS_KEY_24H, s_cfg.use_24h_format ? 1 : 0);
        nvs_set_u8(handle, NVS_KEY_ALARM_EN, s_cfg.alarm.enabled ? 1 : 0);
        nvs_set_u8(handle, NVS_KEY_ALARM_H, s_cfg.alarm.hour);
        nvs_set_u8(handle, NVS_KEY_ALARM_M, s_cfg.alarm.minute);
        nvs_commit(handle);
        nvs_close(handle);
    }
    return ret;
}

const clock_config_t *clock_service_get_config(void)
{
    return &s_cfg;
}

void clock_service_get_local_time(struct tm *out_tm)
{
    if (!out_tm) return;
    time_t now = 0;
    time(&now);
    time_t local = now + (s_cfg.tz_offset_hours * 3600);
    gmtime_r(&local, out_tm);
}

void clock_service_get_world_time(int8_t utc_offset_hours, int *out_hour, int *out_min)
{
    time_t now = 0;
    time(&now);
    time_t w_time = now + (utc_offset_hours * 3600);
    struct tm tm_w;
    gmtime_r(&w_time, &tm_w);
    if (out_hour) *out_hour = tm_w.tm_hour;
    if (out_min)  *out_min  = tm_w.tm_min;
}

/* --------------------------------------------------------------------
 * Melodious Chime Player Task (Core 0)
 * -------------------------------------------------------------------- */
static void chime_player_task(void *pvParam)
{
    (void)pvParam;
    ESP_LOGI(TAG, "Alarm chime melody starting");

    /* Musical notes for chime (E5, G5, B5, E6, B5, G5) */
    static const uint16_t notes[] = { 659, 784, 988, 1318, 988, 784 };
    static const uint16_t durations[] = { 180, 180, 220, 400, 200, 350 };
    const size_t num_notes = sizeof(notes) / sizeof(notes[0]);

    while (s_is_ringing) {
        for (size_t i = 0; i < num_notes && s_is_ringing; i++) {
            audio_player_play_tone(notes[i], durations[i]);
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        /* Brief pause between melodies */
        for (int p = 0; p < 15 && s_is_ringing; p++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "Alarm chime stopped");
    s_chime_task_handle = NULL;
    vTaskDelete(NULL);
}

void clock_service_play_chime(void)
{
    if (s_is_ringing) return;
    s_is_ringing = true;

    xTaskCreatePinnedToCore(
        chime_player_task,
        "chime_task",
        4096,
        NULL,
        4,
        &s_chime_task_handle,
        0 // Core 0
    );
}

void clock_service_stop_chime(void)
{
    s_is_ringing = false;
}

bool clock_service_is_alarm_ringing(void)
{
    return s_is_ringing;
}

void clock_service_dismiss_alarm(void)
{
    s_is_ringing = false;
}

/* --------------------------------------------------------------------
 * Alarm Watchdog Timer (runs every second in LVGL)
 * -------------------------------------------------------------------- */
static void alarm_monitor_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_cfg.alarm.enabled || s_is_ringing) return;

    struct tm tm_loc;
    clock_service_get_local_time(&tm_loc);

    /* Check once per minute at second 0 */
    if (tm_loc.tm_sec <= 2 && tm_loc.tm_min != s_last_checked_min) {
        if (tm_loc.tm_hour == s_cfg.alarm.hour && tm_loc.tm_min == s_cfg.alarm.minute) {
            s_last_checked_min = tm_loc.tm_min;
            ESP_LOGW(TAG, "ALARM TRIGGERED AT %02d:%02d!", tm_loc.tm_hour, tm_loc.tm_min);
            clock_service_play_chime();
        }
    }
}

esp_err_t clock_service_init(void)
{
    load_nvs();

    if (!s_alarm_timer) {
        s_alarm_timer = lv_timer_create(alarm_monitor_timer_cb, 1000, NULL);
    }

    ESP_LOGI(TAG, "Clock Service initialized");
    return ESP_OK;
}
