/*
 * Futuristic Sci-Fi System Diagnostics / Telemetry Page
 * Inspired by Orion N5 Mars Expedition UI
 * Target: JC3248W535EN (ESP32-S3, 480x320 Landscape, LVGL 8.4)
 */

#include "gui_system_page.h"
#include "lvgl.h"
#include <stdio.h>

#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* External Orbitron Fonts */
extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;
extern const lv_font_t font_orbitron_38;

/* Visual Palette */
#define COLOR_BG            lv_color_hex(0x111413) // Dark industrial slate
#define COLOR_PANEL_BG      lv_color_hex(0x181C1A) // Panel surface
#define COLOR_ORANGE        lv_color_hex(0xFF5A2A) // Industrial hazard orange
#define COLOR_MINT          lv_color_hex(0xC2DEC9) // Sage mint
#define COLOR_TEXT_WHITE    lv_color_hex(0xF0F5F1) // Off-white
#define COLOR_TEXT_MUTED    lv_color_hex(0x6E8376) // Coordinate gray-green
#define COLOR_BORDER_LINE   lv_color_hex(0x28332C) // Panel border line

static lv_obj_t *view_system = NULL;
static lv_timer_t *timer_telemetry = NULL;

#include "wifi_controller.h"

/* Dynamic labels and bars for real-time updates */
static lv_obj_t *bar_psram = NULL;
static lv_obj_t *bar_sram = NULL;
static lv_obj_t *lbl_psram_val = NULL;
static lv_obj_t *lbl_sram_val = NULL;
static lv_obj_t *lbl_tasks_val = NULL;
static lv_obj_t *lbl_wifi_val = NULL;
static lv_obj_t *lbl_ip_val = NULL;

static void btn_back_click_cb(lv_event_t *e)
{
    (void)e;
    gui_system_page_close();
}

static void update_telemetry_data(void)
{
    if (!view_system) return;

    /* 1. Memory stats */
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_psram  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t used_psram  = (total_psram > free_psram) ? (total_psram - free_psram) : 0;

    size_t total_sram  = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t free_sram   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t used_sram   = (total_sram > free_sram) ? (total_sram - free_sram) : 0;

    if (bar_psram && total_psram > 0) {
        int psram_pct = (int)((used_psram * 100) / total_psram);
        lv_bar_set_value(bar_psram, psram_pct, LV_ANIM_ON);
    }
    if (lbl_psram_val) {
        char buf[64];
        snprintf(buf, sizeof(buf), "PSRAM: %.1f MB Free / %.1f MB", (float)free_psram / (1024.0f * 1024.0f), (float)total_psram / (1024.0f * 1024.0f));
        lv_label_set_text(lbl_psram_val, buf);
    }

    if (bar_sram && total_sram > 0) {
        int sram_pct = (int)((used_sram * 100) / total_sram);
        lv_bar_set_value(bar_sram, sram_pct, LV_ANIM_ON);
    }
    if (lbl_sram_val) {
        char buf[64];
        snprintf(buf, sizeof(buf), "SRAM:  %u KB Free / %u KB", (unsigned)(free_sram / 1024), (unsigned)(total_sram / 1024));
        lv_label_set_text(lbl_sram_val, buf);
    }

    /* 2. Task stats */
    if (lbl_tasks_val) {
        char buf[64];
        UBaseType_t task_cnt = uxTaskGetNumberOfTasks();
        snprintf(buf, sizeof(buf), "RTOS Tasks: %u Active Tasks", (unsigned)task_cnt);
        lv_label_set_text(lbl_tasks_val, buf);
    }

    /* 3. Wi-Fi status & IP */
    wifi_status_t st = wifi_controller_get_status();
    if (lbl_wifi_val) {
        char buf[64];
        if (st == WIFI_STATUS_CONNECTED) {
            const char *ssid = wifi_controller_get_connected_ssid();
            snprintf(buf, sizeof(buf), "STATUS: ONLINE (%s)", (ssid && strlen(ssid)) ? ssid : "NET");
            lv_obj_set_style_text_color(lbl_wifi_val, COLOR_MINT, 0);
        } else if (st == WIFI_STATUS_CONNECTING) {
            snprintf(buf, sizeof(buf), "STATUS: CONNECTING...");
            lv_obj_set_style_text_color(lbl_wifi_val, COLOR_ORANGE, 0);
        } else if (st == WIFI_STATUS_SCANNING) {
            snprintf(buf, sizeof(buf), "STATUS: SCANNING...");
            lv_obj_set_style_text_color(lbl_wifi_val, COLOR_ORANGE, 0);
        } else {
            snprintf(buf, sizeof(buf), "STATUS: DISCONNECTED");
            lv_obj_set_style_text_color(lbl_wifi_val, COLOR_TEXT_MUTED, 0);
        }
        lv_label_set_text(lbl_wifi_val, buf);
    }

    if (lbl_ip_val) {
        char buf[64];
        if (st == WIFI_STATUS_CONNECTED) {
            snprintf(buf, sizeof(buf), "IP: %s", wifi_controller_get_ip());
            lv_obj_set_style_text_color(lbl_ip_val, COLOR_MINT, 0);
        } else {
            snprintf(buf, sizeof(buf), "IP: 0.0.0.0 (OFFLINE)");
            lv_obj_set_style_text_color(lbl_ip_val, COLOR_TEXT_MUTED, 0);
        }
        lv_label_set_text(lbl_ip_val, buf);
    }
}

static void telemetry_timer_cb(lv_timer_t *t)
{
    (void)t;
    update_telemetry_data();
}

static lv_obj_t *create_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, const char *title, lv_color_t accent_color)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 3, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Panel Title */
    lv_obj_t *lbl_t = lv_label_create(panel);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_font(lbl_t, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_t, accent_color, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Divider line */
    lv_obj_t *line = lv_obj_create(panel);
    lv_obj_set_size(line, w - 16, 1);
    lv_obj_set_pos(line, 0, 16);
    lv_obj_set_style_bg_color(line, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    return panel;
}

void gui_system_page_open(void)
{
    if (view_system) return;

    /* Read actual chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint32_t flash_size_bytes = 0;
    esp_flash_get_size(NULL, &flash_size_bytes);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    /* Full-screen container over the screen */
    view_system = lv_obj_create(lv_scr_act());
    lv_obj_set_size(view_system, 480, 320);
    lv_obj_set_pos(view_system, 0, 0);
    lv_obj_set_style_bg_color(view_system, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(view_system, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view_system, 0, 0);
    lv_obj_set_style_pad_all(view_system, 0, 0);
    lv_obj_clear_flag(view_system, LV_OBJ_FLAG_SCROLLABLE);

    /* ====================================================================
     * HEADER: RETURN BUTTON & TITLE
     * ==================================================================== */

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(view_system);
    lv_obj_set_size(btn_back, 110, 28);
    lv_obj_set_pos(btn_back, 10, 8);
    lv_obj_set_style_bg_color(btn_back, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_back, COLOR_MINT, 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 2, 0);
    lv_obj_set_style_pad_all(btn_back, 0, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, btn_back_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< LAUNCHER");
    lv_obj_set_style_text_font(lbl_back, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_back, COLOR_MINT, 0);
    lv_obj_center(lbl_back);

    /* Header title */
    lv_obj_t *lbl_title = lv_label_create(view_system);
    lv_label_set_text(lbl_title, "SYSTEM TELEMETRY\n// HARDWARE MONITOR");
    lv_obj_set_style_text_font(lbl_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_title, 125, 4);
    lv_obj_set_width(lbl_title, 250);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_WRAP);

    /* Right status badge */
    lv_obj_t *badge = lv_obj_create(view_system);
    lv_obj_set_size(badge, 90, 24);
    lv_obj_set_pos(badge, 380, 10);
    lv_obj_set_style_bg_color(badge, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(badge, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_radius(badge, 2, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_badge = lv_label_create(badge);
    lv_label_set_text(lbl_badge, "D350 NODE");
    lv_obj_set_style_text_font(lbl_badge, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_badge, COLOR_MINT, 0);
    lv_obj_center(lbl_badge);

    /* Separator line */
    lv_obj_t *line_hdr = lv_obj_create(view_system);
    lv_obj_set_size(line_hdr, 460, 1);
    lv_obj_set_pos(line_hdr, 10, 42);
    lv_obj_set_style_bg_color(line_hdr, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(line_hdr, 0, 0);
    lv_obj_clear_flag(line_hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* ====================================================================
     * PANEL 1: CPU & KERNEL (Top Left: x=10, y=48, w=224, h=126)
     * ==================================================================== */
    lv_obj_t *p1 = create_panel(view_system, 10, 48, 224, 126, "[01] PROCESSOR & CORE", COLOR_ORANGE);

    lv_obj_t *lbl_chip = lv_label_create(p1);
    char chip_str[64];
    snprintf(chip_str, sizeof(chip_str), "CHIP: ESP32-S3 (%d Cores)", chip_info.cores);
    lv_label_set_text(lbl_chip, chip_str);
    lv_obj_set_style_text_font(lbl_chip, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_chip, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_chip, 0, 22);

    lv_obj_t *lbl_clock = lv_label_create(p1);
    lv_label_set_text(lbl_clock, "FREQ: 240 MHz (Max Turbo)");
    lv_obj_set_style_text_font(lbl_clock, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_clock, COLOR_MINT, 0);
    lv_obj_set_pos(lbl_clock, 0, 38);

    lv_obj_t *lbl_rtos = lv_label_create(p1);
    lv_label_set_text(lbl_rtos, "RTOS: FreeRTOS @ 1000 Hz");
    lv_obj_set_style_text_font(lbl_rtos, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_rtos, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_rtos, 0, 54);

    lbl_tasks_val = lv_label_create(p1);
    lv_label_set_text(lbl_tasks_val, "RTOS Tasks: Calculating...");
    lv_obj_set_style_text_font(lbl_tasks_val, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_tasks_val, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_tasks_val, 0, 70);

    lv_obj_t *lbl_aff = lv_label_create(p1);
    lv_label_set_text(lbl_aff, "LVGL Task: Core 1 (Prio 5)");
    lv_obj_set_style_text_font(lbl_aff, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_aff, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_aff, 0, 86);

    /* ====================================================================
     * PANEL 2: MEMORY SUBSYSTEM (Top Right: x=246, y=48, w=224, h=126)
     * ==================================================================== */
    lv_obj_t *p2 = create_panel(view_system, 246, 48, 224, 126, "[02] MEMORY SUBSYSTEM", COLOR_MINT);

    lbl_psram_val = lv_label_create(p2);
    lv_label_set_text(lbl_psram_val, "PSRAM: 8.0 MB Loading...");
    lv_obj_set_style_text_font(lbl_psram_val, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_psram_val, COLOR_MINT, 0);
    lv_obj_set_pos(lbl_psram_val, 0, 22);

    bar_psram = lv_bar_create(p2);
    lv_obj_set_size(bar_psram, 206, 6);
    lv_obj_set_pos(bar_psram, 0, 38);
    lv_obj_set_style_bg_color(bar_psram, COLOR_BG, 0);
    lv_obj_set_style_bg_color(bar_psram, COLOR_MINT, LV_PART_INDICATOR);
    lv_bar_set_range(bar_psram, 0, 100);

    lbl_sram_val = lv_label_create(p2);
    lv_label_set_text(lbl_sram_val, "SRAM: 320 KB Loading...");
    lv_obj_set_style_text_font(lbl_sram_val, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_sram_val, COLOR_ORANGE, 0);
    lv_obj_set_pos(lbl_sram_val, 0, 52);

    bar_sram = lv_bar_create(p2);
    lv_obj_set_size(bar_sram, 206, 6);
    lv_obj_set_pos(bar_sram, 0, 68);
    lv_obj_set_style_bg_color(bar_sram, COLOR_BG, 0);
    lv_obj_set_style_bg_color(bar_sram, COLOR_ORANGE, LV_PART_INDICATOR);
    lv_bar_set_range(bar_sram, 0, 100);

    lv_obj_t *lbl_cache = lv_label_create(p2);
    lv_label_set_text(lbl_cache, "L1 Cache: 64KB D / 32KB I");
    lv_obj_set_style_text_font(lbl_cache, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_cache, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_cache, 0, 84);

    /* ====================================================================
     * PANEL 3: STORAGE & FLASH (Bottom Left: x=10, y=180, w=224, h=130)
     * ==================================================================== */
    lv_obj_t *p3 = create_panel(view_system, 10, 180, 224, 130, "[03] STORAGE & FLASH", COLOR_ORANGE);

    lv_obj_t *lbl_flash = lv_label_create(p3);
    char flash_str[64];
    snprintf(flash_str, sizeof(flash_str), "FLASH: %.1f MB Total", (float)flash_size_bytes / (1024.0f * 1024.0f));
    lv_label_set_text(lbl_flash, flash_str);
    lv_obj_set_style_text_font(lbl_flash, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_flash, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_flash, 0, 22);

    lv_obj_t *lbl_bus = lv_label_create(p3);
    lv_label_set_text(lbl_bus, "INTERFACE: Octal SPI 80MHz");
    lv_obj_set_style_text_font(lbl_bus, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_bus, COLOR_MINT, 0);
    lv_obj_set_pos(lbl_bus, 0, 38);

    const esp_partition_t *running_part = esp_ota_get_running_partition();
    lv_obj_t *lbl_app = lv_label_create(p3);
    if (running_part) {
        char part_str[64];
        snprintf(part_str, sizeof(part_str), "APP: %s (%u KB)", running_part->label, (unsigned)(running_part->size / 1024));
        lv_label_set_text(lbl_app, part_str);
    } else {
        lv_label_set_text(lbl_app, "APP: Factory Partition");
    }
    lv_obj_set_style_text_font(lbl_app, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_app, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_app, 0, 54);

    lv_obj_t *lbl_fs = lv_label_create(p3);
    lv_label_set_text(lbl_fs, "FS: LittleFS Storage");
    lv_obj_set_style_text_font(lbl_fs, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_fs, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_fs, 0, 70);

    lv_obj_t *lbl_lcd = lv_label_create(p3);
    lv_label_set_text(lbl_lcd, "PANEL: QSPI AXS15231B");
    lv_obj_set_style_text_font(lbl_lcd, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_lcd, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_lcd, 0, 86);

    /* ====================================================================
     * PANEL 4: WIRELESS & NETWORK (Bottom Right: x=246, y=180, w=224, h=130)
     * ==================================================================== */
    lv_obj_t *p4 = create_panel(view_system, 246, 180, 224, 130, "[04] WIRELESS & NETWORK", COLOR_MINT);

    lbl_wifi_val = lv_label_create(p4);
    lv_label_set_text(lbl_wifi_val, "STATUS: QUERYING...");
    lv_obj_set_style_text_font(lbl_wifi_val, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_val, COLOR_MINT, 0);
    lv_obj_set_pos(lbl_wifi_val, 0, 22);

    lv_obj_t *lbl_mac = lv_label_create(p4);
    char mac_str[64];
    snprintf(mac_str, sizeof(mac_str), "MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    lv_label_set_text(lbl_mac, mac_str);
    lv_obj_set_style_text_font(lbl_mac, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_mac, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_mac, 0, 38);

    lv_obj_t *lbl_band = lv_label_create(p4);
    lv_label_set_text(lbl_band, "RADIO: 2.4 GHz 802.11 b/g/n");
    lv_obj_set_style_text_font(lbl_band, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_band, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_band, 0, 54);

    lv_obj_t *lbl_bt = lv_label_create(p4);
    lv_label_set_text(lbl_bt, "BT: Bluetooth 5.0 LE");
    lv_obj_set_style_text_font(lbl_bt, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_bt, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_bt, 0, 70);

    lbl_ip_val = lv_label_create(p4);
    lv_label_set_text(lbl_ip_val, "IP: QUERYING...");
    lv_obj_set_style_text_font(lbl_ip_val, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_ip_val, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_ip_val, 0, 86);

    /* Update initial values immediately */
    update_telemetry_data();

    /* Start 1-second refresh timer */
    timer_telemetry = lv_timer_create(telemetry_timer_cb, 1000, NULL);
}

void gui_system_page_close(void)
{
    if (timer_telemetry) {
        lv_timer_del(timer_telemetry);
        timer_telemetry = NULL;
    }

    if (view_system) {
        lv_obj_del(view_system);
        view_system = NULL;
    }

    bar_psram = NULL;
    bar_sram = NULL;
    lbl_psram_val = NULL;
    lbl_sram_val = NULL;
    lbl_tasks_val = NULL;
    lbl_wifi_val = NULL;
    lbl_ip_val = NULL;
}
