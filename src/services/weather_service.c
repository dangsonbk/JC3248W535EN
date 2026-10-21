/*
 * Weather Service & Open-Meteo Integration
 * Handles Geocoding, Atmospheric Telemetry, NVS Persistence, and Dual-Core Async Fetching
 * Target: JC3248W535EN (ESP32-S3, ESP-IDF 5.2)
 */

#include "weather_service.h"
#include "wifi_controller.h"
#include "sys_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "WEATHER_SVC";

#define HTTP_RECV_BUF_SIZE 4096

typedef enum {
    WCMD_FETCH = 1,
    WCMD_SEARCH_CITY
} weather_cmd_type_t;

typedef struct {
    weather_cmd_type_t type;
    char search_query[64];
} weather_cmd_msg_t;

static weather_data_t s_weather_data;
static city_search_status_t s_search_status = CITY_SEARCH_IDLE;
static char s_search_resolved_name[64] = "";
static QueueHandle_t q_weather_cmd = NULL;
static TaskHandle_t s_weather_task_handle = NULL;
static bool s_weather_inited = false;
static volatile bool s_is_fetching = false;
static uint32_t s_weather_update_count = 0;
static char s_last_sync_time[16] = "--:--";

static void map_wmo_code(int code, char *out_str, size_t max_len, const char **out_icon)
{
    switch (code) {
        case 0:
            strncpy(out_str, "CLEAR SKY", max_len);
            *out_icon = LV_SYMBOL_GPS;
            break;
        case 1:
        case 2:
            strncpy(out_str, "PARTLY CLOUDY", max_len);
            *out_icon = LV_SYMBOL_GPS;
            break;
        case 3:
            strncpy(out_str, "OVERCAST CLOUDS", max_len);
            *out_icon = LV_SYMBOL_BARS;
            break;
        case 45:
        case 48:
            strncpy(out_str, "DENSE FOG / HAZE", max_len);
            *out_icon = LV_SYMBOL_EYE_CLOSE;
            break;
        case 51:
        case 53:
        case 55:
            strncpy(out_str, "LIGHT DRIZZLE", max_len);
            *out_icon = LV_SYMBOL_TINT;
            break;
        case 61:
        case 63:
            strncpy(out_str, "MODERATE RAIN", max_len);
            *out_icon = LV_SYMBOL_TINT;
            break;
        case 65:
            strncpy(out_str, "HEAVY DOWNPOUR", max_len);
            *out_icon = LV_SYMBOL_TINT;
            break;
        case 71:
        case 73:
        case 75:
            strncpy(out_str, "SNOWFALL", max_len);
            *out_icon = LV_SYMBOL_CHARGE;
            break;
        case 80:
        case 81:
        case 82:
            strncpy(out_str, "RAIN SHOWER", max_len);
            *out_icon = LV_SYMBOL_TINT;
            break;
        case 95:
        case 96:
        case 99:
            strncpy(out_str, "THUNDERSTORM", max_len);
            *out_icon = LV_SYMBOL_WARNING;
            break;
        default:
            strncpy(out_str, "ATMOSPHERE STABLE", max_len);
            *out_icon = LV_SYMBOL_GPS;
            break;
    }
}

/* --------------------------------------------------------------------
 * NVS LOAD & SAVE
 * -------------------------------------------------------------------- */
static void save_weather_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("weather_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "city", s_weather_data.city_name);
        nvs_set_u8(h, "unit_f", s_weather_data.is_fahrenheit ? 1 : 0);

        char lat_s[24], lon_s[24];
        snprintf(lat_s, sizeof(lat_s), "%.4f", s_weather_data.latitude);
        snprintf(lon_s, sizeof(lon_s), "%.4f", s_weather_data.longitude);
        nvs_set_str(h, "lat", lat_s);
        nvs_set_str(h, "lon", lon_s);

        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Saved weather config to NVS: %s (%.4f, %.4f)",
                 s_weather_data.city_name, s_weather_data.latitude, s_weather_data.longitude);
    }
}

static void load_weather_nvs(void)
{
    /* Initialize with robust Sci-Fi defaults */
    s_weather_data.valid = true;
    strncpy(s_weather_data.city_name, "HANOI, VN", sizeof(s_weather_data.city_name));
    s_weather_data.latitude = 21.0285f;
    s_weather_data.longitude = 105.8542f;
    s_weather_data.is_fahrenheit = false;

    s_weather_data.current_temp = 28;
    s_weather_data.temp_max = 32;
    s_weather_data.temp_min = 24;
    s_weather_data.humidity = 76;
    s_weather_data.wind_speed = 12.5f;
    s_weather_data.rain_probability = 20;
    s_weather_data.weather_code = 1;
    strncpy(s_weather_data.condition_str, "PARTLY CLOUDY", sizeof(s_weather_data.condition_str));
    s_weather_data.icon = LV_SYMBOL_IMAGE;

    strncpy(s_weather_data.sunrise_str, "05:48", sizeof(s_weather_data.sunrise_str));
    strncpy(s_weather_data.sunset_str, "17:35", sizeof(s_weather_data.sunset_str));
    s_weather_data.sunrise_minutes = 5 * 60 + 48;
    s_weather_data.sunset_minutes = 17 * 60 + 35;

    const char *days[] = {"WED", "THU", "FRI", "SAT"};
    int maxs[] = {32, 31, 29, 30};
    int mins[] = {24, 23, 22, 23};
    int rains[] = {20, 45, 75, 15};
    int codes[] = {1, 2, 61, 0};
    for (int i = 0; i < 4; i++) {
        strncpy(s_weather_data.forecast[i].day_name, days[i], sizeof(s_weather_data.forecast[i].day_name));
        s_weather_data.forecast[i].temp_max = maxs[i];
        s_weather_data.forecast[i].temp_min = mins[i];
        s_weather_data.forecast[i].rain_prob = rains[i];
        s_weather_data.forecast[i].weather_code = codes[i];
        map_wmo_code(codes[i], s_weather_data.forecast[i].condition, sizeof(s_weather_data.forecast[i].condition),
                     &s_weather_data.forecast[i].icon);
    }

    nvs_handle_t h;
    if (nvs_open("weather_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_weather_data.city_name);
        nvs_get_str(h, "city", s_weather_data.city_name, &len);

        uint8_t u_f = 0;
        if (nvs_get_u8(h, "unit_f", &u_f) == ESP_OK) {
            s_weather_data.is_fahrenheit = (u_f == 1);
        }

        char lat_s[24] = {0}, lon_s[24] = {0};
        len = sizeof(lat_s);
        if (nvs_get_str(h, "lat", lat_s, &len) == ESP_OK) {
            s_weather_data.latitude = atof(lat_s);
        }
        len = sizeof(lon_s);
        if (nvs_get_str(h, "lon", lon_s, &len) == ESP_OK) {
            s_weather_data.longitude = atof(lon_s);
        }

        nvs_close(h);
    }
}

/* --------------------------------------------------------------------
 * HTTP HELPER
 * -------------------------------------------------------------------- */
static char *http_fetch_url(const char *url)
{
    char *buf = malloc(HTTP_RECV_BUF_SIZE);
    if (!buf) return NULL;
    buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 7000,
        .buffer_size = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(buf);
        return NULL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(buf);
        return NULL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    (void)content_length;

    int total_read = 0;
    while (total_read < HTTP_RECV_BUF_SIZE - 1) {
        int read_len = esp_http_client_read(client, buf + total_read, HTTP_RECV_BUF_SIZE - 1 - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }
    buf[total_read] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return buf;
}

/* --------------------------------------------------------------------
 * CORE 0 WORKER TASK
 * -------------------------------------------------------------------- */
static void do_fetch_forecast(void)
{
    s_is_fetching = true;
    char url[512];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,precipitation"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_probability_max"
             "&timezone=auto&forecast_days=4",
             s_weather_data.latitude, s_weather_data.longitude);

    ESP_LOGI(TAG, "Fetching Open-Meteo forecast: %s", url);
    char *json_str = http_fetch_url(url);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to fetch weather JSON");
        s_is_fetching = false;
        sys_log_set("METEO SYNC FAILED :: OFFLINE");
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON Parse Error");
        s_is_fetching = false;
        sys_log_set("METEO JSON ERROR :: PARSE FAIL");
        return;
    }

    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (current) {
        cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
        cJSON *hum  = cJSON_GetObjectItem(current, "relative_humidity_2m");
        cJSON *code = cJSON_GetObjectItem(current, "weather_code");
        if (temp) s_weather_data.current_temp = (int)temp->valuedouble;
        if (hum)  s_weather_data.humidity = (int)hum->valuedouble;
        if (code) {
            s_weather_data.weather_code = code->valueint;
            map_wmo_code(code->valueint, s_weather_data.condition_str, sizeof(s_weather_data.condition_str),
                         &s_weather_data.icon);
        }
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (daily) {
        cJSON *t_max = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *t_min = cJSON_GetObjectItem(daily, "temperature_2m_min");
        cJSON *w_code = cJSON_GetObjectItem(daily, "weather_code");
        cJSON *p_prob = cJSON_GetObjectItem(daily, "precipitation_probability_max");
        cJSON *sunr = cJSON_GetObjectItem(daily, "sunrise");
        cJSON *suns = cJSON_GetObjectItem(daily, "sunset");
        cJSON *d_time = cJSON_GetObjectItem(daily, "time");

        if (t_max && cJSON_GetArraySize(t_max) > 0) {
            s_weather_data.temp_max = (int)cJSON_GetArrayItem(t_max, 0)->valuedouble;
        }
        if (t_min && cJSON_GetArraySize(t_min) > 0) {
            s_weather_data.temp_min = (int)cJSON_GetArrayItem(t_min, 0)->valuedouble;
        }
        if (p_prob && cJSON_GetArraySize(p_prob) > 0) {
            s_weather_data.rain_probability = (int)cJSON_GetArrayItem(p_prob, 0)->valuedouble;
        }

        /* Sunrise / Sunset */
        if (sunr && cJSON_GetArraySize(sunr) > 0) {
            const char *sr_str = cJSON_GetArrayItem(sunr, 0)->valuestring;
            if (sr_str && strlen(sr_str) >= 16) {
                snprintf(s_weather_data.sunrise_str, sizeof(s_weather_data.sunrise_str), "%.5s", sr_str + 11);
                int h = atoi(s_weather_data.sunrise_str);
                int m = atoi(s_weather_data.sunrise_str + 3);
                s_weather_data.sunrise_minutes = h * 60 + m;
            }
        }
        if (suns && cJSON_GetArraySize(suns) > 0) {
            const char *ss_str = cJSON_GetArrayItem(suns, 0)->valuestring;
            if (ss_str && strlen(ss_str) >= 16) {
                snprintf(s_weather_data.sunset_str, sizeof(s_weather_data.sunset_str), "%.5s", ss_str + 11);
                int h = atoi(s_weather_data.sunset_str);
                int m = atoi(s_weather_data.sunset_str + 3);
                s_weather_data.sunset_minutes = h * 60 + m;
            }
        }

        /* 4-Day Forecast with Real Day Names parsed from API */
        int num_days = cJSON_GetArraySize(t_max);
        if (num_days > 4) num_days = 4;
        for (int i = 0; i < num_days; i++) {
            if (t_max) s_weather_data.forecast[i].temp_max = (int)cJSON_GetArrayItem(t_max, i)->valuedouble;
            if (t_min) s_weather_data.forecast[i].temp_min = (int)cJSON_GetArrayItem(t_min, i)->valuedouble;
            if (p_prob) s_weather_data.forecast[i].rain_prob = (int)cJSON_GetArrayItem(p_prob, i)->valuedouble;
            if (w_code) {
                int c = cJSON_GetArrayItem(w_code, i)->valueint;
                s_weather_data.forecast[i].weather_code = c;
                map_wmo_code(c, s_weather_data.forecast[i].condition, sizeof(s_weather_data.forecast[i].condition),
                             &s_weather_data.forecast[i].icon);
            }
            if (d_time && i < cJSON_GetArraySize(d_time)) {
                const char *dt = cJSON_GetArrayItem(d_time, i)->valuestring;
                int yr = 0, mo = 0, da = 0;
                if (dt && sscanf(dt, "%d-%d-%d", &yr, &mo, &da) == 3) {
                    struct tm tm_d = {0};
                    tm_d.tm_year = yr - 1900;
                    tm_d.tm_mon = mo - 1;
                    tm_d.tm_mday = da;
                    mktime(&tm_d);
                    const char *wdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
                    if (i == 0) {
                        strncpy(s_weather_data.forecast[i].day_name, "TODAY", sizeof(s_weather_data.forecast[i].day_name));
                    } else {
                        strncpy(s_weather_data.forecast[i].day_name, wdays[tm_d.tm_wday % 7], sizeof(s_weather_data.forecast[i].day_name));
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    s_weather_data.valid = true;
    s_is_fetching = false;
    s_weather_update_count++;

    time_t now = 0;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    snprintf(s_last_sync_time, sizeof(s_last_sync_time), "%02d:%02d", ti.tm_hour, ti.tm_min);

    char log_buf[96];
    snprintf(log_buf, sizeof(log_buf), "METEO :: SYNCED %s [%d C] @ %s", s_weather_data.city_name, s_weather_data.current_temp, s_last_sync_time);
    sys_log_set(log_buf);
    ESP_LOGI(TAG, "Weather sync complete: %d C, %s at %s", s_weather_data.current_temp, s_weather_data.condition_str, s_last_sync_time);
}

static void do_search_city(const char *query)
{
    s_search_status = CITY_SEARCH_IN_PROGRESS;

    /* URL encode query (replace spaces with %20) */
    char enc_query[128] = "";
    size_t ql = strlen(query);
    size_t eq_idx = 0;
    for (size_t i = 0; i < ql && eq_idx < sizeof(enc_query) - 4; i++) {
        if (query[i] == ' ') {
            enc_query[eq_idx++] = '%';
            enc_query[eq_idx++] = '2';
            enc_query[eq_idx++] = '0';
        } else {
            enc_query[eq_idx++] = query[i];
        }
    }
    enc_query[eq_idx] = '\0';

    char url[512];
    snprintf(url, sizeof(url), "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json", enc_query);

    ESP_LOGI(TAG, "Searching Open-Meteo Geocoding: %s", url);
    char *json_str = http_fetch_url(url);
    if (!json_str) {
        s_search_status = CITY_SEARCH_ERROR;
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) {
        s_search_status = CITY_SEARCH_ERROR;
        return;
    }

    cJSON *results = cJSON_GetObjectItem(root, "results");
    if (results && cJSON_GetArraySize(results) > 0) {
        cJSON *first = cJSON_GetArrayItem(results, 0);
        cJSON *name = cJSON_GetObjectItem(first, "name");
        cJSON *country = cJSON_GetObjectItem(first, "country_code");
        cJSON *lat = cJSON_GetObjectItem(first, "latitude");
        cJSON *lon = cJSON_GetObjectItem(first, "longitude");

        if (name && lat && lon) {
            snprintf(s_search_resolved_name, sizeof(s_search_resolved_name), "%s%s%s",
                     name->valuestring, country ? ", " : "", country ? country->valuestring : "");

            strncpy(s_weather_data.city_name, s_search_resolved_name, sizeof(s_weather_data.city_name) - 1);
            s_weather_data.latitude = (float)lat->valuedouble;
            s_weather_data.longitude = (float)lon->valuedouble;

            save_weather_nvs();
            s_search_status = CITY_SEARCH_SUCCESS;
            ESP_LOGI(TAG, "City located: %s (%.4f, %.4f)", s_search_resolved_name, s_weather_data.latitude, s_weather_data.longitude);

            /* Fetch new forecast immediately */
            do_fetch_forecast();
        } else {
            s_search_status = CITY_SEARCH_NOT_FOUND;
        }
    } else {
        s_search_status = CITY_SEARCH_NOT_FOUND;
    }

    cJSON_Delete(root);
}

static void weather_core0_task(void *pvParameters)
{
    (void)pvParameters;
    weather_cmd_msg_t msg;

    while (1) {
        if (xQueueReceive(q_weather_cmd, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.type == WCMD_FETCH) {
                if (wifi_controller_get_status() == WIFI_STATUS_CONNECTED) {
                    do_fetch_forecast();
                } else {
                    ESP_LOGW(TAG, "Wi-Fi not connected, skipping forecast fetch");
                }
            } else if (msg.type == WCMD_SEARCH_CITY) {
                if (wifi_controller_get_status() == WIFI_STATUS_CONNECTED) {
                    do_search_city(msg.search_query);
                } else {
                    s_search_status = CITY_SEARCH_ERROR;
                }
            }
        }
    }
}

esp_err_t weather_service_init(void)
{
    if (s_weather_inited) return ESP_OK;

    load_weather_nvs();

    q_weather_cmd = xQueueCreate(4, sizeof(weather_cmd_msg_t));

    /* Create Worker Task PINNED TO CORE 0 */
    xTaskCreatePinnedToCore(
        weather_core0_task,
        "weather_task",
        4096,
        NULL,
        2,                   /* Priority */
        &s_weather_task_handle,
        0                    /* Pinned to Core 0 */
    );

    s_weather_inited = true;
    ESP_LOGI(TAG, "Weather Service initialized on Core 0");
    return ESP_OK;
}

bool weather_service_fetch_async(void)
{
    if (!q_weather_cmd) return false;
    weather_cmd_msg_t msg;
    msg.type = WCMD_FETCH;
    return (xQueueSend(q_weather_cmd, &msg, 0) == pdTRUE);
}

bool weather_service_search_city_async(const char *city_name)
{
    if (!q_weather_cmd || !city_name) return false;
    weather_cmd_msg_t msg;
    msg.type = WCMD_SEARCH_CITY;
    strncpy(msg.search_query, city_name, sizeof(msg.search_query) - 1);
    msg.search_query[sizeof(msg.search_query) - 1] = '\0';
    s_search_status = CITY_SEARCH_IN_PROGRESS;
    return (xQueueSend(q_weather_cmd, &msg, 0) == pdTRUE);
}

city_search_status_t weather_service_get_search_status(char *out_resolved_name, size_t max_len)
{
    if (out_resolved_name && max_len > 0) {
        strncpy(out_resolved_name, s_search_resolved_name, max_len - 1);
        out_resolved_name[max_len - 1] = '\0';
    }
    return s_search_status;
}

const weather_data_t *weather_service_get_data(void)
{
    return &s_weather_data;
}

void weather_service_set_unit(bool is_fahrenheit)
{
    s_weather_data.is_fahrenheit = is_fahrenheit;
    save_weather_nvs();
}

bool weather_service_get_unit(void)
{
    return s_weather_data.is_fahrenheit;
}

const char *weather_service_get_city(void)
{
    return s_weather_data.city_name;
}

bool weather_service_is_busy(void)
{
    return s_is_fetching;
}

uint32_t weather_service_get_update_count(void)
{
    return s_weather_update_count;
}

const char *weather_service_get_last_sync_time(void)
{
    return s_last_sync_time;
}

