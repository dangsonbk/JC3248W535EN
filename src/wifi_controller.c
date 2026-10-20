/*
 * Wi-Fi Controller & Dual-Core IPC Handler
 * Implements FreeRTOS Inter-Core Message Queue & Synchronization between Core 0 & Core 1
 * Core 0: Wi-Fi stack, Event processing, NVS storage, NTP sync, Scanning & Connection
 * Core 1: LVGL UI rendering & Touch interaction
 */

#include "wifi_controller.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "weather_service.h"

static const char *TAG = "WIFI_CTRL";

typedef enum {
    WIFI_CMD_SCAN = 1,
    WIFI_CMD_CONNECT,
    WIFI_CMD_DISCONNECT
} wifi_cmd_type_t;

typedef struct {
    wifi_cmd_type_t type;
    char ssid[33];
    char password[64];
} wifi_cmd_msg_t;

static bool wifi_initialized = false;
static bool wifi_scanning = false;
static bool s_time_synced = false;
static volatile wifi_status_t s_wifi_status = WIFI_STATUS_IDLE;
static char s_ip_addr[16] = "0.0.0.0";
static char s_connected_ssid[33] = "";
static char s_last_password[64] = "";

static char s_system_log[96] = "SYSTEM INITIALIZED";
static uint32_t s_system_log_ver = 1;

static QueueHandle_t q_wifi_cmd = NULL;
static QueueHandle_t q_wifi_results = NULL;
static TaskHandle_t wifi_task_handle = NULL;

void wifi_controller_set_log(const char *msg)
{
    if (!msg) return;
    strncpy(s_system_log, msg, sizeof(s_system_log) - 1);
    s_system_log[sizeof(s_system_log) - 1] = '\0';
    s_system_log_ver++;
    ESP_LOGI(TAG, "SYS_LOG: %s", s_system_log);
}

const char *wifi_controller_get_log(uint32_t *out_ver)
{
    if (out_ver) *out_ver = s_system_log_ver;
    return s_system_log;
}

/* --------------------------------------------------------------------
 * NVS STORAGE FOR WI-FI CREDENTIALS
 * -------------------------------------------------------------------- */
void wifi_controller_save_credentials(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) return;
    nvs_handle_t h;
    if (nvs_open("wifi_store", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "password", password ? password : "");
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Saved Wi-Fi credentials to NVS: %s", ssid);
    }
}

bool wifi_controller_load_saved_credentials(char *out_ssid, size_t ssid_len, char *out_pass, size_t pass_len)
{
    if (!out_ssid || ssid_len == 0) return false;
    nvs_handle_t h;
    if (nvs_open("wifi_store", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err1 = nvs_get_str(h, "ssid", out_ssid, &ssid_len);
    if (out_pass && pass_len > 0) {
        nvs_get_str(h, "password", out_pass, &pass_len);
    }
    nvs_close(h);
    return (err1 == ESP_OK && strlen(out_ssid) > 0);
}

/* --------------------------------------------------------------------
 * NTP TIME SYNCHRONIZATION
 * -------------------------------------------------------------------- */
static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
    ESP_LOGI(TAG, "NTP Time sync notification received!");
    wifi_controller_set_log("NTP SYNC COMPLETE // TIME SET");
}

static void start_ntp_sync(void)
{
    setenv("TZ", "ICT-7", 1);
    tzset();

    if (!esp_sntp_enabled()) {
        ESP_LOGI(TAG, "Starting SNTP service...");
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        esp_sntp_setservername(2, "time.cloudflare.com");
        sntp_set_time_sync_notification_cb(time_sync_notification_cb);
        esp_sntp_init();
    } else {
        esp_sntp_restart();
    }
}

bool wifi_controller_is_time_synced(void)
{
    if (s_time_synced) return true;
    time_t now = 0;
    time(&now);
    if (now > 1700000000) { // Year > 2023
        s_time_synced = true;
        return true;
    }
    return false;
}

/* --------------------------------------------------------------------
 * WI-FI & IP EVENT HANDLERS
 * -------------------------------------------------------------------- */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Wi-Fi Station Started");
        } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "Wi-Fi Connected to AP, waiting for IP...");
            char buf[96];
            snprintf(buf, sizeof(buf), "CONNECTED TO %s // GETTING IP...", s_connected_ssid);
            wifi_controller_set_log(buf);
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "Wi-Fi Disconnected (reason: %d)", disconn ? disconn->reason : 0);
            if (s_wifi_status == WIFI_STATUS_CONNECTING) {
                s_wifi_status = WIFI_STATUS_FAILED;
                wifi_controller_set_log("CONNECTION FAILED // RETRYING");
            } else if (s_wifi_status == WIFI_STATUS_CONNECTED) {
                s_wifi_status = WIFI_STATUS_IDLE;
                strcpy(s_ip_addr, "0.0.0.0");
                s_connected_ssid[0] = '\0';
                wifi_controller_set_log("NETWORK DISCONNECTED");
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip_addr, sizeof(s_ip_addr));
        ESP_LOGI(TAG, "IP Assigned: %s", s_ip_addr);
        s_wifi_status = WIFI_STATUS_CONNECTED;

        /* Save successful connection to NVS for auto-load */
        wifi_controller_save_credentials(s_connected_ssid, s_last_password);

        /* Log network connection */
        char buf[96];
        snprintf(buf, sizeof(buf), "ONLINE // IP: %s", s_ip_addr);
        wifi_controller_set_log(buf);

        /* Trigger NTP sync */
        start_ntp_sync();

        /* Trigger Weather sync */
        weather_service_fetch_async();
    }
}

static void wifi_core0_worker_task(void *pvParameters)
{
    (void)pvParameters;
    wifi_cmd_msg_t cmd;

    ESP_LOGI(TAG, "Wi-Fi Worker Task running on Core %d", xPortGetCoreID());

    while (1) {
        if (xQueueReceive(q_wifi_cmd, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.type == WIFI_CMD_SCAN) {
                ESP_LOGI(TAG, "Core 0 starting Wi-Fi scan...");
                wifi_scanning = true;

                wifi_scan_config_t scan_cfg = {
                    .ssid = NULL,
                    .bssid = NULL,
                    .channel = 0,
                    .show_hidden = false,
                    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                    .scan_time = {
                        .active = { .min = 100, .max = 200 }
                    }
                };

                esp_err_t err = esp_wifi_scan_start(&scan_cfg, true); // Blocking call on Core 0
                wifi_scan_result_t res;
                memset(&res, 0, sizeof(res));

                if (err == ESP_OK) {
                    uint16_t ap_count = 0;
                    esp_wifi_scan_get_ap_num(&ap_count);

                    if (ap_count > MAX_WIFI_SCAN_RECORDS) {
                        ap_count = MAX_WIFI_SCAN_RECORDS;
                    }

                    wifi_ap_record_t *records = malloc(sizeof(wifi_ap_record_t) * ap_count);
                    if (records) {
                        esp_wifi_scan_get_ap_records(&ap_count, records);
                        res.count = ap_count;
                        for (uint16_t i = 0; i < ap_count; i++) {
                            strncpy(res.items[i].ssid, (const char *)records[i].ssid, sizeof(res.items[i].ssid) - 1);
                            res.items[i].rssi = records[i].rssi;
                            res.items[i].authmode = records[i].authmode;
                        }
                        free(records);
                    }
                } else {
                    ESP_LOGE(TAG, "Wi-Fi scan failed: %d", err);
                }

                wifi_scanning = false;
                /* Send results across cores to Queue */
                xQueueOverwrite(q_wifi_results, &res);
                ESP_LOGI(TAG, "Core 0 scan completed: %d APs found.", res.count);
            }
            else if (cmd.type == WIFI_CMD_CONNECT) {
                ESP_LOGI(TAG, "Core 0 connecting to: %s", cmd.ssid);
                s_wifi_status = WIFI_STATUS_CONNECTING;
                strncpy(s_connected_ssid, cmd.ssid, sizeof(s_connected_ssid) - 1);
                strncpy(s_last_password, cmd.password, sizeof(s_last_password) - 1);
                strcpy(s_ip_addr, "0.0.0.0");

                wifi_config_t wifi_cfg;
                memset(&wifi_cfg, 0, sizeof(wifi_cfg));
                strncpy((char *)wifi_cfg.sta.ssid, cmd.ssid, sizeof(wifi_cfg.sta.ssid));
                strncpy((char *)wifi_cfg.sta.password, cmd.password, sizeof(wifi_cfg.sta.password));
                wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                wifi_cfg.sta.pmf_cfg.capable = true;
                wifi_cfg.sta.pmf_cfg.required = false;

                esp_wifi_disconnect();
                esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
                esp_wifi_connect();
            }
            else if (cmd.type == WIFI_CMD_DISCONNECT) {
                ESP_LOGI(TAG, "Core 0 disconnecting Wi-Fi...");
                esp_wifi_disconnect();
                s_wifi_status = WIFI_STATUS_IDLE;
                strcpy(s_ip_addr, "0.0.0.0");
                s_connected_ssid[0] = '\0';
            }
        }
    }
}

void wifi_controller_auto_connect(void)
{
    char saved_ssid[33] = {0};
    char saved_pass[64] = {0};
    if (wifi_controller_load_saved_credentials(saved_ssid, sizeof(saved_ssid), saved_pass, sizeof(saved_pass))) {
        char buf[96];
        snprintf(buf, sizeof(buf), "CONNECTING // %s", saved_ssid);
        wifi_controller_set_log(buf);
        wifi_controller_connect(saved_ssid, saved_pass);
    } else {
        wifi_controller_set_log("SYSTEM LOADED // STANDBY");
    }
}

esp_err_t wifi_controller_init(void)
{
    if (wifi_initialized) return ESP_OK;

    /* Set default timezone UTC+7 */
    setenv("TZ", "ICT-7", 1);
    tzset();

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_controller_set_log("BOOT // CHECKING SAVED NETWORKS...");

    /* Initialize TCP/IP and Event Loop */
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    /* Register Event Handlers */
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    /* Initialize Wi-Fi Driver in STA mode */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Create FreeRTOS Queues for Inter-Core Communication */
    q_wifi_cmd = xQueueCreate(4, sizeof(wifi_cmd_msg_t));
    q_wifi_results = xQueueCreate(1, sizeof(wifi_scan_result_t));

    /* Create Worker Task PINNED TO CORE 0 */
    xTaskCreatePinnedToCore(
        wifi_core0_worker_task,
        "wifi_core0_task",
        4096,
        NULL,
        3,                   /* Priority */
        &wifi_task_handle,
        0                    /* Pinned to Core 0 */
    );

    wifi_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi Controller initialized on Core 0");

    /* Attempt auto-connecting to saved network */
    wifi_controller_auto_connect();

    return ESP_OK;
}

bool wifi_controller_start_scan(void)
{
    if (!wifi_initialized) {
        wifi_controller_init();
    }

    if (wifi_scanning) {
        return false; // Scan already in progress
    }

    wifi_cmd_msg_t cmd;
    cmd.type = WIFI_CMD_SCAN;
    return (xQueueSend(q_wifi_cmd, &cmd, 0) == pdTRUE);
}

bool wifi_controller_get_scan_results(wifi_scan_result_t *out_result)
{
    if (!q_wifi_results || !out_result) return false;
    return (xQueueReceive(q_wifi_results, out_result, 0) == pdTRUE);
}

bool wifi_controller_is_scanning(void)
{
    return wifi_scanning;
}

bool wifi_controller_connect(const char *ssid, const char *password)
{
    if (!wifi_initialized || !ssid) return false;

    wifi_cmd_msg_t cmd;
    cmd.type = WIFI_CMD_CONNECT;
    strncpy(cmd.ssid, ssid, sizeof(cmd.ssid) - 1);
    if (password) {
        strncpy(cmd.password, password, sizeof(cmd.password) - 1);
    } else {
        cmd.password[0] = '\0';
    }
    return (xQueueSend(q_wifi_cmd, &cmd, 0) == pdTRUE);
}

void wifi_controller_disconnect(void)
{
    if (!wifi_initialized) return;
    wifi_cmd_msg_t cmd;
    cmd.type = WIFI_CMD_DISCONNECT;
    xQueueSend(q_wifi_cmd, &cmd, 0);
}

wifi_status_t wifi_controller_get_status(void)
{
    return s_wifi_status;
}

const char *wifi_controller_get_ip(void)
{
    return s_ip_addr;
}

const char *wifi_controller_get_connected_ssid(void)
{
    return s_connected_ssid;
}
