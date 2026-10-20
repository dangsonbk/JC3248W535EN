/*
 * Weather & Atmosphere Configuration Page
 * Supports Temperature Unit Toggle (°C / °F) and Geocoding City Search
 * Target: JC3248W535EN (ESP32-S3, 480x320 Landscape, LVGL 8.4)
 */

#include "gui_weather_settings.h"
#include "weather_service.h"
#include "gui_keyboard.h"
#include "wifi_controller.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

/* External Orbitron Fonts */
extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;

/* Cyberpunk Palette */
#define COLOR_BG            lv_color_hex(0x111413)
#define COLOR_PANEL_BG      lv_color_hex(0x181C1A)
#define COLOR_ORANGE        lv_color_hex(0xFF5A2A)
#define COLOR_MINT          lv_color_hex(0xC2DEC9)
#define COLOR_TEXT_WHITE    lv_color_hex(0xF0F5F1)
#define COLOR_TEXT_MUTED    lv_color_hex(0x6E8376)
#define COLOR_BORDER_LINE   lv_color_hex(0x28332C)

static lv_obj_t *view_weather_settings = NULL;
static lv_obj_t *lbl_active_city = NULL;
static lv_obj_t *lbl_active_geo = NULL;
static lv_obj_t *lbl_unit_desc = NULL;
static lv_obj_t *sw_unit = NULL;
static lv_timer_t *timer_search_poll = NULL;

static void btn_back_click_cb(lv_event_t *e)
{
    (void)e;
    gui_weather_settings_close();
}

static void sw_unit_changed_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool is_f = lv_obj_has_state(sw, LV_STATE_CHECKED);
    weather_service_set_unit(is_f);

    if (lbl_unit_desc) {
        lv_label_set_text(lbl_unit_desc, is_f ? "FAHRENHEIT (oF) SCALE ACTIVE" : "CELSIUS (oC) SCALE ACTIVE");
        lv_obj_set_style_text_color(lbl_unit_desc, is_f ? COLOR_ORANGE : COLOR_MINT, 0);
    }
}

static void search_poll_timer_cb(lv_timer_t *t)
{
    (void)t;
    char resolved[64] = "";
    city_search_status_t st = weather_service_get_search_status(resolved, sizeof(resolved));

    if (st == CITY_SEARCH_SUCCESS) {
        if (timer_search_poll) {
            lv_timer_del(timer_search_poll);
            timer_search_poll = NULL;
        }

        gui_input_dialog_set_status("#c2dec9 [ SECTOR ACQUIRED: %s ]#", resolved);

        if (lbl_active_city) {
            char buf[80];
            snprintf(buf, sizeof(buf), "CURRENT SECTOR: %s", resolved);
            lv_label_set_text(lbl_active_city, buf);
        }
        if (lbl_active_geo) {
            const weather_data_t *w = weather_service_get_data();
            char gbuf[64];
            snprintf(gbuf, sizeof(gbuf), "COORDINATES: %.2f N, %.2f E", w->latitude, w->longitude);
            lv_label_set_text(lbl_active_geo, gbuf);
        }

        /* Auto close input modal after 1.2s */
        lv_timer_t *ct = lv_timer_create((lv_timer_cb_t)gui_input_dialog_close, 1200, NULL);
        lv_timer_set_repeat_count(ct, 1);
    }
    else if (st == CITY_SEARCH_NOT_FOUND) {
        if (timer_search_poll) {
            lv_timer_del(timer_search_poll);
            timer_search_poll = NULL;
        }
        gui_input_dialog_set_status("#ff5a2a [ SECTOR NOT FOUND // VERIFY SPELLING ]#");
        gui_input_dialog_set_busy(false);
    }
    else if (st == CITY_SEARCH_ERROR) {
        if (timer_search_poll) {
            lv_timer_del(timer_search_poll);
            timer_search_poll = NULL;
        }
        gui_input_dialog_set_status("#ff5a2a [ NETWORK TIMEOUT // CHECK WI-FI ]#");
        gui_input_dialog_set_busy(false);
    }
}

static void city_search_dialog_cb(const char *text, bool submitted, void *user_data)
{
    (void)user_data;
    if (!submitted || !text || strlen(text) == 0) {
        if (timer_search_poll) {
            lv_timer_del(timer_search_poll);
            timer_search_poll = NULL;
        }
        return;
    }

    if (wifi_controller_get_status() != WIFI_STATUS_CONNECTED) {
        gui_input_dialog_set_status("#ff5a2a [ WI-FI OFFLINE // CONNECT TO INTERNET FIRST ]#");
        return;
    }

    gui_input_dialog_set_busy(true);
    gui_input_dialog_set_status("#ff5a2a [ QUERYING GEODATA SATELLITES... ]#");

    weather_service_search_city_async(text);

    if (timer_search_poll) {
        lv_timer_del(timer_search_poll);
    }
    timer_search_poll = lv_timer_create(search_poll_timer_cb, 200, NULL);
}

static void btn_search_open_cb(lv_event_t *e)
{
    (void)e;
    gui_input_dialog_config_t cfg = {
        .title = "[ // ATMOSPHERE SECTOR SEARCH // ]",
        .target_info = "ENTER CITY NAME (e.g. Tokyo, London, Da Nang, Paris):",
        .initial_text = NULL,
        .placeholder = "SEARCH CITY...",
        .action_btn_text = "SEARCH",
        .is_password = false,
        .cb = city_search_dialog_cb,
        .user_data = NULL
    };

    gui_input_dialog_open(&cfg);
    gui_input_dialog_set_status("TYPE SECTOR NAME AND PRESS SEARCH OR ENTER");
}

void gui_weather_settings_open(lv_obj_t *parent)
{
    if (view_weather_settings) return;

    const weather_data_t *w = weather_service_get_data();

    view_weather_settings = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(view_weather_settings, 480, 320);
    lv_obj_set_pos(view_weather_settings, 0, 0);
    lv_obj_set_style_bg_color(view_weather_settings, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(view_weather_settings, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view_weather_settings, 0, 0);
    lv_obj_set_style_pad_all(view_weather_settings, 0, 0);
    lv_obj_clear_flag(view_weather_settings, LV_OBJ_FLAG_SCROLLABLE);

    /* Header Back Button */
    lv_obj_t *btn_back = lv_btn_create(view_weather_settings);
    lv_obj_set_size(btn_back, 110, 28);
    lv_obj_set_pos(btn_back, 10, 8);
    lv_obj_set_style_bg_color(btn_back, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_back, COLOR_MINT, 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 2, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, btn_back_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_b = lv_label_create(btn_back);
    lv_label_set_text(lbl_b, "< SETTINGS");
    lv_obj_set_style_text_font(lbl_b, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_b, COLOR_MINT, 0);
    lv_obj_center(lbl_b);

    lv_obj_t *lbl_hdr = lv_label_create(view_weather_settings);
    lv_label_set_text(lbl_hdr, "ATMOSPHERE CONFIG // 09");
    lv_obj_set_style_text_font(lbl_hdr, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(lbl_hdr, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_hdr, 134, 6);

    /* Separator line */
    lv_obj_t *line_hdr = lv_obj_create(view_weather_settings);
    lv_obj_set_size(line_hdr, 460, 1);
    lv_obj_set_pos(line_hdr, 10, 42);
    lv_obj_set_style_bg_color(line_hdr, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(line_hdr, 0, 0);
    lv_obj_clear_flag(line_hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* ====================================================================
     * SECTION 1: TEMPERATURE SCALE TOGGLE (°C / °F)
     * ==================================================================== */
    lv_obj_t *p_unit = lv_obj_create(view_weather_settings);
    lv_obj_set_size(p_unit, 460, 72);
    lv_obj_set_pos(p_unit, 10, 52);
    lv_obj_set_style_bg_color(p_unit, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(p_unit, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(p_unit, 1, 0);
    lv_obj_set_style_radius(p_unit, 3, 0);
    lv_obj_set_style_pad_all(p_unit, 8, 0);
    lv_obj_clear_flag(p_unit, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_u_title = lv_label_create(p_unit);
    lv_label_set_text(lbl_u_title, "[01] TEMPERATURE UNIT // SCALE SELECTION");
    lv_obj_set_style_text_font(lbl_u_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_u_title, COLOR_ORANGE, 0);
    lv_obj_align(lbl_u_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_unit_desc = lv_label_create(p_unit);
    lv_label_set_text(lbl_unit_desc, w->is_fahrenheit ? "FAHRENHEIT (oF) SCALE ACTIVE" : "CELSIUS (oC) SCALE ACTIVE");
    lv_obj_set_style_text_font(lbl_unit_desc, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_unit_desc, w->is_fahrenheit ? COLOR_ORANGE : COLOR_MINT, 0);
    lv_obj_align(lbl_unit_desc, LV_ALIGN_BOTTOM_LEFT, 0, -4);

    /* Switch toggle */
    sw_unit = lv_switch_create(p_unit);
    lv_obj_set_size(sw_unit, 54, 26);
    lv_obj_align(sw_unit, LV_ALIGN_RIGHT_MID, 0, 4);
    lv_obj_set_style_bg_color(sw_unit, lv_color_hex(0x28332C), 0);
    lv_obj_set_style_bg_color(sw_unit, COLOR_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (w->is_fahrenheit) {
        lv_obj_add_state(sw_unit, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(sw_unit, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_unit, sw_unit_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ====================================================================
     * SECTION 2: SECTOR CITY SEARCH
     * ==================================================================== */
    lv_obj_t *p_city = lv_obj_create(view_weather_settings);
    lv_obj_set_size(p_city, 460, 175);
    lv_obj_set_pos(p_city, 10, 134);
    lv_obj_set_style_bg_color(p_city, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(p_city, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(p_city, 1, 0);
    lv_obj_set_style_radius(p_city, 3, 0);
    lv_obj_set_style_pad_all(p_city, 8, 0);
    lv_obj_clear_flag(p_city, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_c_title = lv_label_create(p_city);
    lv_label_set_text(lbl_c_title, "[02] SECTOR LOCATION // SATELLITE GEOCODING");
    lv_obj_set_style_text_font(lbl_c_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_c_title, COLOR_MINT, 0);
    lv_obj_align(lbl_c_title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Active City display */
    lbl_active_city = lv_label_create(p_city);
    char c_buf[80];
    snprintf(c_buf, sizeof(c_buf), "CURRENT SECTOR: %s", w->city_name);
    lv_label_set_text(lbl_active_city, c_buf);
    lv_obj_set_style_text_font(lbl_active_city, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_active_city, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_active_city, 0, 24);

    lbl_active_geo = lv_label_create(p_city);
    char g_buf[64];
    snprintf(g_buf, sizeof(g_buf), "COORDINATES: %.2f N, %.2f E", w->latitude, w->longitude);
    lv_label_set_text(lbl_active_geo, g_buf);
    lv_obj_set_style_text_font(lbl_active_geo, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_active_geo, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_active_geo, 0, 44);

    /* Search Action Button */
    lv_obj_t *btn_search = lv_btn_create(p_city);
    lv_obj_set_size(btn_search, 260, 36);
    lv_obj_set_pos(btn_search, 0, 72);
    lv_obj_set_style_bg_color(btn_search, COLOR_ORANGE, 0);
    lv_obj_set_style_radius(btn_search, 2, 0);
    lv_obj_clear_flag(btn_search, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_search, btn_search_open_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_s_btn = lv_label_create(btn_search);
    lv_label_set_text(lbl_s_btn, "[+] SEARCH NEW CITY // SECTOR");
    lv_obj_set_style_text_font(lbl_s_btn, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_s_btn, lv_color_hex(0x111413), 0);
    lv_obj_center(lbl_s_btn);

    /* Guidance Text */
    lv_obj_t *lbl_guide = lv_label_create(p_city);
    lv_label_set_text(lbl_guide, "GLOBAL SATELLITE NETWORK POWERED BY OPEN-METEO API.\nENTER ANY GLOBAL CITY, CAPITAL, OR REGION.");
    lv_obj_set_style_text_font(lbl_guide, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_guide, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_guide, 0, 120);
}

void gui_weather_settings_close(void)
{
    gui_input_dialog_close();
    if (timer_search_poll) {
        lv_timer_del(timer_search_poll);
        timer_search_poll = NULL;
    }
    if (view_weather_settings) {
        lv_obj_del(view_weather_settings);
        view_weather_settings = NULL;
    }
}
