/*
 * Futuristic Sci-Fi Settings Grid & Wi-Fi Scanner
 * Inspired by 5x2 Cyberpunk HUD Grid Design
 * Target: JC3248W535EN (ESP32-S3, 480x320 Landscape, LVGL 8.4)
 */

#include "gui_settings_page.h"
#include "gui_weather_settings.h"
#include "gui_display_settings.h"
#include "gui_storage_page.h"
#include "gui_about_page.h"
#include "gui_keyboard.h"
#include "wifi_controller.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/* External Orbitron Fonts */
extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

/* Cyberpunk Color Palette */
#define COLOR_BG            lv_color_hex(0x111413)
#define COLOR_PANEL_BG      lv_color_hex(0x181C1A)
#define COLOR_ORANGE        lv_color_hex(0xFF5A2A)
#define COLOR_ORANGE_DARK   lv_color_hex(0x9E3416)
#define COLOR_MINT          lv_color_hex(0xC2DEC9)
#define COLOR_TEXT_WHITE    lv_color_hex(0xF0F5F1)
#define COLOR_TEXT_MUTED    lv_color_hex(0x6E8376)
#define COLOR_BORDER_LINE   lv_color_hex(0x28332C)

static lv_obj_t *view_settings = NULL;
static lv_obj_t *view_wifi = NULL;
static lv_timer_t *timer_wifi_poll = NULL;
static lv_timer_t *timer_connect_poll = NULL;

/* Wi-Fi subview objects */
static lv_obj_t *lbl_wifi_status = NULL;
static lv_obj_t *wifi_list_cont = NULL;
static lv_obj_t *btn_wifi_scan = NULL;

/* Cache for discovered APs */
static wifi_scan_result_t s_cached_scan_results;

typedef struct {
    int id;
    const char *code;
    const char *icon;
    const char *title;
    const char *status_str;
    bool has_orange_corner;
} setting_item_t;

static const setting_item_t settings_grid[10] = {
    {1,  "01", LV_SYMBOL_WIFI,        "WI-FI",    "SCAN/CONNECT", true},
    {2,  "02", LV_SYMBOL_IMAGE,       "DISPLAY",  "PWM / SLEEP",  true},
    {3,  "03", LV_SYMBOL_VOLUME_MAX,  "AUDIO",    "SPEAKER OK",   false},
    {4,  "04", LV_SYMBOL_SHUFFLE,     "BLE",      "STANDBY",      true},
    {5,  "05", LV_SYMBOL_DRIVE,       "STORAGE",  "FLASH / SD",   true},
    {6,  "06", LV_SYMBOL_POWER,       "POWER",    "ACTIVE 240M",  true},
    {7,  "07", LV_SYMBOL_BELL,        "CLOCK",    "UTC +7",       false},
    {8,  "08", LV_SYMBOL_REFRESH,     "THEME",    "ORANGE/MINT",  false},
    {9,  "09", LV_SYMBOL_IMAGE,       "METEO",    "CITY / UNIT",  true},
    {10, "10", LV_SYMBOL_SETTINGS,    "ABOUT",    "ESP32-S3",     false}
};

static void btn_settings_back_cb(lv_event_t *e);
static void open_wifi_scanner_subpage(void);
static void setting_grid_btn_click_cb(lv_event_t *e);

/* ====================================================================
 * WI-FI CONNECTION & AUTH HANDLERS (Using Reusable gui_keyboard)
 * ==================================================================== */

static void connect_poll_timer_cb(lv_timer_t *t)
{
    (void)t;
    wifi_status_t st = wifi_controller_get_status();

    if (st == WIFI_STATUS_CONNECTED) {
        if (timer_connect_poll) {
            lv_timer_del(timer_connect_poll);
            timer_connect_poll = NULL;
        }

        gui_input_dialog_set_status("#c2dec9 [ CONNECTED ] IP: %s#", wifi_controller_get_ip());

        if (lbl_wifi_status) {
            char buf[80];
            snprintf(buf, sizeof(buf), "#c2dec9 [ ONLINE ]# %s (%s)",
                     wifi_controller_get_connected_ssid(), wifi_controller_get_ip());
            lv_label_set_text(lbl_wifi_status, buf);
        }

        /* Auto close dialog after 1.2s */
        lv_timer_t *close_timer = lv_timer_create((lv_timer_cb_t)gui_input_dialog_close, 1200, NULL);
        lv_timer_set_repeat_count(close_timer, 1);
    }
    else if (st == WIFI_STATUS_FAILED) {
        if (timer_connect_poll) {
            lv_timer_del(timer_connect_poll);
            timer_connect_poll = NULL;
        }

        gui_input_dialog_set_status("#ff5a2a [ AUTH FAILED // VERIFY PASSPHRASE ]#");
        gui_input_dialog_set_busy(false);
    }
}

static void wifi_auth_dialog_cb(const char *password, bool submitted, void *user_data)
{
    if (!submitted) {
        if (timer_connect_poll) {
            lv_timer_del(timer_connect_poll);
            timer_connect_poll = NULL;
        }
        return;
    }

    uint32_t idx = (uint32_t)(uintptr_t)user_data;
    if (idx < s_cached_scan_results.count) {
        wifi_scan_item_t *item = &s_cached_scan_results.items[idx];
        gui_input_dialog_set_busy(true);
        gui_input_dialog_set_status("#ff5a2a [ CONNECTING TO CORE 0... ]#");

        /* Send connect command across cores to Core 0 */
        wifi_controller_connect(item->ssid, password);

        if (timer_connect_poll) {
            lv_timer_del(timer_connect_poll);
        }
        timer_connect_poll = lv_timer_create(connect_poll_timer_cb, 250, NULL);
    }
}

static void wifi_item_click_cb(lv_event_t *e)
{
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx < s_cached_scan_results.count) {
        wifi_scan_item_t *item = &s_cached_scan_results.items[idx];

        char net_info[96];
        snprintf(net_info, sizeof(net_info), "TARGET: #c2dec9 %s#  |  %d dBm  |  %s",
                 item->ssid, item->rssi,
                 (item->authmode == WIFI_AUTH_OPEN) ? "OPEN NET" : "WPA2/WPA3");

        gui_input_dialog_config_t cfg = {
            .title = "[ // WI-FI AUTHENTICATION // ]",
            .target_info = net_info,
            .initial_text = NULL,
            .placeholder = (item->authmode == WIFI_AUTH_OPEN) ? "NO PASSWORD NEEDED" : "ENTER SECURITY KEY...",
            .action_btn_text = "CONNECT",
            .is_password = (item->authmode != WIFI_AUTH_OPEN),
            .cb = wifi_auth_dialog_cb,
            .user_data = (void *)(uintptr_t)idx
        };

        gui_input_dialog_open(&cfg);
        if (item->authmode == WIFI_AUTH_OPEN) {
            gui_input_dialog_set_status("#c2dec9 [ OPEN ACCESS ]# PRESS CONNECT TO JOIN");
        } else {
            gui_input_dialog_set_status("ENTER PASSPHRASE ON CYBERPUNK KEYBOARD BELOW");
        }
    }
}

/* ====================================================================
 * WI-FI SCANNER SUB-PAGE IMPLEMENTATION
 * ==================================================================== */

static void wifi_poll_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!wifi_controller_is_scanning()) {
        wifi_scan_result_t results;
        if (wifi_controller_get_scan_results(&results)) {
            /* Results received from Core 0 via FreeRTOS Queue */
            if (timer_wifi_poll) {
                lv_timer_del(timer_wifi_poll);
                timer_wifi_poll = NULL;
            }

            /* Cache results for click events */
            s_cached_scan_results = results;

            if (lbl_wifi_status) {
                char buf[64];
                snprintf(buf, sizeof(buf), "#c2dec9 SCAN COMPLETE:# %u ACCESS POINTS DISCOVERED", results.count);
                lv_label_set_text(lbl_wifi_status, buf);
            }

            /* Enable scan button */
            if (btn_wifi_scan) {
                lv_obj_clear_state(btn_wifi_scan, LV_STATE_DISABLED);
            }

            /* Populate AP List */
            if (wifi_list_cont) {
                lv_obj_clean(wifi_list_cont);

                if (results.count == 0) {
                    lv_obj_t *lbl_none = lv_label_create(wifi_list_cont);
                    lv_label_set_text(lbl_none, "NO WI-FI ACCESS POINTS FOUND IN RANGE");
                    lv_obj_set_style_text_font(lbl_none, &font_orbitron_12, 0);
                    lv_obj_set_style_text_color(lbl_none, COLOR_TEXT_MUTED, 0);
                    lv_obj_center(lbl_none);
                    return;
                }

                const char *curr_connected = wifi_controller_get_connected_ssid();
                bool is_conn = (wifi_controller_get_status() == WIFI_STATUS_CONNECTED);

                for (uint16_t i = 0; i < results.count; i++) {
                    bool is_this_connected = (is_conn && curr_connected && strlen(curr_connected) > 0 &&
                                              strcmp(results.items[i].ssid, curr_connected) == 0);

                    lv_obj_t *item = lv_obj_create(wifi_list_cont);
                    lv_obj_set_size(item, 440, 38);
                    lv_obj_set_style_bg_color(item, COLOR_PANEL_BG, 0);
                    lv_obj_set_style_bg_color(item, lv_color_hex(0x232D27), LV_STATE_PRESSED);
                    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
                    lv_obj_set_style_border_color(item, is_this_connected ? COLOR_MINT : COLOR_BORDER_LINE, 0);
                    lv_obj_set_style_border_width(item, is_this_connected ? 2 : 1, 0);
                    lv_obj_set_style_radius(item, 2, 0);
                    lv_obj_set_style_pad_hor(item, 12, 0);
                    lv_obj_set_style_pad_ver(item, 0, 0);
                    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_add_event_cb(item, wifi_item_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

                    /* Wi-Fi Icon */
                    lv_obj_t *icon = lv_label_create(item);
                    lv_label_set_text(icon, LV_SYMBOL_WIFI);
                    lv_obj_set_style_text_color(icon, is_this_connected ? COLOR_MINT : COLOR_ORANGE, 0);
                    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

                    /* SSID Name */
                    lv_obj_t *lbl_ssid = lv_label_create(item);
                    if (strlen(results.items[i].ssid) > 0) {
                        lv_label_set_text(lbl_ssid, results.items[i].ssid);
                    } else {
                        lv_label_set_text(lbl_ssid, "[HIDDEN SSID]");
                    }
                    lv_obj_set_style_text_font(lbl_ssid, &font_orbitron_12, 0);
                    lv_obj_set_style_text_color(lbl_ssid, COLOR_TEXT_WHITE, 0);
                    lv_obj_align(lbl_ssid, LV_ALIGN_LEFT_MID, 28, 0);

                    /* Signal Strength & Encryption */
                    lv_obj_t *lbl_info = lv_label_create(item);
                    char info_buf[48];
                    snprintf(info_buf, sizeof(info_buf), "%d dBm  |  %s %s",
                             results.items[i].rssi,
                             (results.items[i].authmode == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED",
                             is_this_connected ? "[ONLINE]" : "");
                    lv_label_set_text(lbl_info, info_buf);
                    lv_obj_set_style_text_font(lbl_info, &font_orbitron_12, 0);
                    lv_obj_set_style_text_color(lbl_info, is_this_connected ? COLOR_MINT : ((results.items[i].rssi > -65) ? COLOR_MINT : COLOR_ORANGE), 0);
                    lv_obj_align(lbl_info, LV_ALIGN_RIGHT_MID, 0, 0);
                }
            }
        }
    }
}

static void btn_scan_click_cb(lv_event_t *e)
{
    (void)e;
    if (wifi_controller_is_scanning()) return;

    if (lbl_wifi_status) {
        lv_label_set_text(lbl_wifi_status, "#ff5a2a [ SCANNING ]:# CORE 0 SCANNING CHANNELS 1-13...");
    }

    if (btn_wifi_scan) {
        lv_obj_add_state(btn_wifi_scan, LV_STATE_DISABLED);
    }

    if (wifi_list_cont) {
        lv_obj_clean(wifi_list_cont);
        lv_obj_t *lbl_wait = lv_label_create(wifi_list_cont);
        lv_label_set_text(lbl_wait, "SCANNING AIRWAVES IN BACKGROUND (CORE 0)...");
        lv_obj_set_style_text_font(lbl_wait, &font_orbitron_12, 0);
        lv_obj_set_style_text_color(lbl_wait, COLOR_ORANGE, 0);
        lv_obj_center(lbl_wait);
    }

    /* Trigger non-blocking scan on Core 0 */
    wifi_controller_start_scan();

    /* Start polling timer on UI task (Core 1) to check Queue */
    if (timer_wifi_poll) {
        lv_timer_del(timer_wifi_poll);
    }
    timer_wifi_poll = lv_timer_create(wifi_poll_timer_cb, 150, NULL);
}

static void btn_wifi_back_cb(lv_event_t *e)
{
    (void)e;
    gui_input_dialog_close();
    if (timer_wifi_poll) {
        lv_timer_del(timer_wifi_poll);
        timer_wifi_poll = NULL;
    }
    if (view_wifi) {
        lv_obj_del(view_wifi);
        view_wifi = NULL;
    }
}

static void open_wifi_scanner_subpage(void)
{
    if (view_wifi) return;

    view_wifi = lv_obj_create(view_settings);
    lv_obj_set_size(view_wifi, 480, 320);
    lv_obj_set_pos(view_wifi, 0, 0);
    lv_obj_set_style_bg_color(view_wifi, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(view_wifi, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view_wifi, 0, 0);
    lv_obj_set_style_pad_all(view_wifi, 0, 0);
    lv_obj_clear_flag(view_wifi, LV_OBJ_FLAG_SCROLLABLE);

    /* Back Button */
    lv_obj_t *btn_b = lv_btn_create(view_wifi);
    lv_obj_set_size(btn_b, 120, 28);
    lv_obj_set_pos(btn_b, 10, 8);
    lv_obj_set_style_bg_color(btn_b, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_b, COLOR_MINT, 0);
    lv_obj_set_style_border_width(btn_b, 1, 0);
    lv_obj_set_style_radius(btn_b, 2, 0);
    lv_obj_clear_flag(btn_b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_b, btn_wifi_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_b = lv_label_create(btn_b);
    lv_label_set_text(lbl_b, "< SETTINGS");
    lv_obj_set_style_text_font(lbl_b, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_b, COLOR_MINT, 0);
    lv_obj_center(lbl_b);

    /* Header Title */
    lv_obj_t *lbl_t = lv_label_create(view_wifi);
    lv_label_set_text(lbl_t, "WI-FI SCANNER\n// NETWORK RECON");
    lv_obj_set_style_text_font(lbl_t, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_t, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_t, 125, 4);
    lv_obj_set_width(lbl_t, 225);
    lv_label_set_long_mode(lbl_t, LV_LABEL_LONG_WRAP);

    /* Scan Action Button */
    btn_wifi_scan = lv_btn_create(view_wifi);
    lv_obj_set_size(btn_wifi_scan, 110, 28);
    lv_obj_set_pos(btn_wifi_scan, 360, 8);
    lv_obj_set_style_bg_color(btn_wifi_scan, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(btn_wifi_scan, 0, 0);
    lv_obj_set_style_radius(btn_wifi_scan, 2, 0);
    lv_obj_clear_flag(btn_wifi_scan, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_wifi_scan, btn_scan_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_scan_txt = lv_label_create(btn_wifi_scan);
    lv_label_set_text(lbl_scan_txt, "[+] SCAN");
    lv_obj_set_style_text_font(lbl_scan_txt, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_scan_txt, lv_color_hex(0x111413), 0);
    lv_obj_center(lbl_scan_txt);

    /* Status Bar below Header */
    lv_obj_t *status_bar = lv_obj_create(view_wifi);
    lv_obj_set_size(status_bar, 460, 24);
    lv_obj_set_pos(status_bar, 10, 42);
    lv_obj_set_style_bg_color(status_bar, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(status_bar, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(status_bar, 1, 0);
    lv_obj_set_style_radius(btn_b, 2, 0);
    lv_obj_set_style_pad_hor(status_bar, 8, 0);
    lv_obj_set_style_pad_ver(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_wifi_status = lv_label_create(status_bar);
    lv_label_set_recolor(lbl_wifi_status, true);
    if (wifi_controller_get_status() == WIFI_STATUS_CONNECTED) {
        char buf[80];
        snprintf(buf, sizeof(buf), "#c2dec9 [ ONLINE ]# %s (%s)",
                 wifi_controller_get_connected_ssid(), wifi_controller_get_ip());
        lv_label_set_text(lbl_wifi_status, buf);
    } else {
        lv_label_set_text(lbl_wifi_status, "#c2dec9 [ READY ]:# TAP SCAN, THEN TAP AP TO CONNECT");
    }
    lv_obj_set_style_text_font(lbl_wifi_status, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_status, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_wifi_status, LV_ALIGN_LEFT_MID, 0, 0);

    /* Scrollable Container for Discovered Networks */
    wifi_list_cont = lv_obj_create(view_wifi);
    lv_obj_set_size(wifi_list_cont, 460, 244);
    lv_obj_set_pos(wifi_list_cont, 10, 70);
    lv_obj_set_style_bg_color(wifi_list_cont, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(wifi_list_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(wifi_list_cont, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(wifi_list_cont, 1, 0);
    lv_obj_set_style_radius(wifi_list_cont, 3, 0);
    lv_obj_set_style_pad_all(wifi_list_cont, 6, 0);
    lv_obj_set_style_pad_row(wifi_list_cont, 6, 0);
    lv_obj_set_flex_flow(wifi_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(wifi_list_cont, LV_DIR_VER);

    /* Initial hint */
    lv_obj_t *lbl_hint = lv_label_create(wifi_list_cont);
    lv_label_set_text(lbl_hint, "READY FOR NETWORK RECONNAISSANCE.\nPRESS [+] SCAN BUTTON ABOVE.");
    lv_obj_set_style_text_font(lbl_hint, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_hint, COLOR_TEXT_MUTED, 0);
    lv_obj_center(lbl_hint);
}

/* ====================================================================
 * 5x2 SETTINGS GRID IMPLEMENTATION
 * ==================================================================== */

static void setting_grid_btn_click_cb(lv_event_t *e)
{
    const setting_item_t *item = (const setting_item_t *)lv_event_get_user_data(e);
    if (!item) return;

    if (item->id == 1) {
        /* Open Wi-Fi Scanner Sub-page */
        open_wifi_scanner_subpage();
    } else if (item->id == 2) {
        /* Open Display & Sleep Settings Sub-page */
        gui_display_settings_open(view_settings);
    } else if (item->id == 5) {
        /* Open Storage Explorer Sub-page */
        gui_storage_page_open(view_settings);
    } else if (item->id == 9) {
        /* Open Weather Settings Sub-page */
        gui_weather_settings_open(view_settings);
    } else if (item->id == 10) {
        /* Open About & Mission Callsign Sub-page */
        gui_about_page_open(view_settings);
    }
}

void gui_settings_page_open(void)
{
    if (view_settings) return;

    view_settings = lv_obj_create(lv_scr_act());
    lv_obj_set_size(view_settings, 480, 320);
    lv_obj_set_pos(view_settings, 0, 0);
    lv_obj_set_style_bg_color(view_settings, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(view_settings, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view_settings, 0, 0);
    lv_obj_set_style_pad_all(view_settings, 0, 0);
    lv_obj_clear_flag(view_settings, LV_OBJ_FLAG_SCROLLABLE);

    /* Top Left Back Button */
    lv_obj_t *btn_back = lv_btn_create(view_settings);
    lv_obj_set_size(btn_back, 110, 28);
    lv_obj_set_pos(btn_back, 10, 8);
    lv_obj_set_style_bg_color(btn_back, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_back, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 2, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, btn_settings_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< LAUNCHER");
    lv_obj_set_style_text_font(lbl_back, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_back, COLOR_ORANGE, 0);
    lv_obj_center(lbl_back);

    /* Center Header */
    lv_obj_t *lbl_title = lv_label_create(view_settings);
    lv_label_set_text(lbl_title, "SYSTEM SETTINGS\n// HUD CONTROL");
    lv_obj_set_style_text_font(lbl_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_title, 125, 4);
    lv_obj_set_width(lbl_title, 245);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_WRAP);

    /* Top Right Mode Indicator Badge */
    lv_obj_t *badge = lv_obj_create(view_settings);
    lv_obj_set_size(badge, 90, 24);
    lv_obj_set_pos(badge, 380, 10);
    lv_obj_set_style_bg_color(badge, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(badge, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_radius(badge, 2, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_badge = lv_label_create(badge);
    lv_label_set_text(lbl_badge, "5x2 GRID");
    lv_obj_set_style_text_font(lbl_badge, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_badge, COLOR_MINT, 0);
    lv_obj_center(lbl_badge);

    /* Separator line */
    lv_obj_t *line_hdr = lv_obj_create(view_settings);
    lv_obj_set_size(line_hdr, 460, 1);
    lv_obj_set_pos(line_hdr, 10, 42);
    lv_obj_set_style_bg_color(line_hdr, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(line_hdr, 0, 0);
    lv_obj_clear_flag(line_hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* 5x2 Cyberpunk HUD Grid (5 columns x 2 rows)
     * Total width: 480 -> 10px margin on each side, 460px available.
     * 5 buttons: 82px width, 12px column gap. 5 * 82 + 4 * 12 = 410 + 48 = 458px.
     * 2 rows: 124px height, 10px row gap.
     */
    for (int i = 0; i < 10; i++) {
        const setting_item_t *item = &settings_grid[i];
        int col = i % 5;
        int row = i / 5;

        lv_coord_t btn_x = 11 + col * (82 + 12);
        lv_coord_t btn_y = 52 + row * (118 + 12);

        lv_obj_t *btn = lv_btn_create(view_settings);
        lv_obj_set_pos(btn, btn_x, btn_y);
        lv_obj_set_size(btn, 82, 118);
        lv_obj_set_style_bg_color(btn, COLOR_PANEL_BG, 0);
        lv_obj_set_style_border_color(btn, (item->id == 1) ? COLOR_ORANGE : COLOR_BORDER_LINE, 0);
        lv_obj_set_style_border_width(btn, (item->id == 1) ? 2 : 1, 0);
        lv_obj_set_style_radius(btn, 3, 0);
        lv_obj_set_style_pad_all(btn, 6, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, setting_grid_btn_click_cb, LV_EVENT_CLICKED, (void *)item);

        /* Sci-Fi Corner Triangle Accent (as in reference image) */
        if (item->has_orange_corner) {
            lv_obj_t *corner_tag = lv_obj_create(btn);
            lv_obj_set_size(corner_tag, 10, 10);
            lv_obj_set_pos(corner_tag, 0, 0);
            lv_obj_set_style_bg_color(corner_tag, COLOR_ORANGE, 0);
            lv_obj_set_style_border_width(corner_tag, 0, 0);
            lv_obj_set_style_radius(corner_tag, 0, 0);
            lv_obj_clear_flag(corner_tag, LV_OBJ_FLAG_SCROLLABLE);
        }

        /* Module Code (e.g. "01", "02") */
        lv_obj_t *lbl_code = lv_label_create(btn);
        lv_label_set_text(lbl_code, item->code);
        lv_obj_set_style_text_font(lbl_code, &font_orbitron_12, 0);
        lv_obj_set_style_text_color(lbl_code, (item->id == 1) ? COLOR_ORANGE : COLOR_TEXT_MUTED, 0);
        lv_obj_align(lbl_code, LV_ALIGN_TOP_RIGHT, 0, 0);

        /* Center Icon */
        lv_obj_t *lbl_ico = lv_label_create(btn);
        lv_label_set_text(lbl_ico, item->icon);
        lv_obj_set_style_text_font(lbl_ico, &font_orbitron_24, 0);
        lv_obj_set_style_text_color(lbl_ico, (item->id == 1) ? COLOR_ORANGE : COLOR_MINT, 0);
        lv_obj_align(lbl_ico, LV_ALIGN_CENTER, 0, -8);

        /* Module Title */
        lv_obj_t *lbl_name = lv_label_create(btn);
        lv_label_set_text(lbl_name, item->title);
        lv_obj_set_style_text_font(lbl_name, &font_orbitron_12, 0);
        lv_obj_set_style_text_color(lbl_name, COLOR_TEXT_WHITE, 0);
        lv_obj_align(lbl_name, LV_ALIGN_BOTTOM_MID, 0, -14);

        /* Module Status Subtitle */
        lv_obj_t *lbl_stat = lv_label_create(btn);
        lv_label_set_text(lbl_stat, item->status_str);
        lv_obj_set_style_text_font(lbl_stat, &font_orbitron_12, 0);
        lv_obj_set_style_text_color(lbl_stat, COLOR_TEXT_MUTED, 0);
        lv_obj_align(lbl_stat, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

static void btn_settings_back_cb(lv_event_t *e)
{
    (void)e;
    gui_settings_page_close();
}

void gui_settings_page_close(void)
{
    gui_weather_settings_close();
    gui_input_dialog_close();
    if (timer_connect_poll) {
        lv_timer_del(timer_connect_poll);
        timer_connect_poll = NULL;
    }
    if (timer_wifi_poll) {
        lv_timer_del(timer_wifi_poll);
        timer_wifi_poll = NULL;
    }
    if (view_wifi) {
        lv_obj_del(view_wifi);
        view_wifi = NULL;
    }
    if (view_settings) {
        lv_obj_del(view_settings);
        view_settings = NULL;
    }
}
