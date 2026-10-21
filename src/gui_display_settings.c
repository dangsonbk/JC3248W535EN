#include "gui_display_settings.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "display.h"
#include "esp_bsp.h"

static const char *TAG = "GUI_DISP_SET";

/* Sci-Fi Palette matching GUI HUD */
#define COLOR_BG          lv_color_make(0x08, 0x0c, 0x14)
#define COLOR_PANEL_BG    lv_color_make(0x0e, 0x16, 0x26)
#define COLOR_BORDER_LINE lv_color_make(0x1a, 0x2b, 0x44)
#define COLOR_ORANGE      lv_color_make(0xff, 0x5a, 0x2a)
#define COLOR_MINT        lv_color_make(0x33, 0xd1, 0x7a)
#define COLOR_TEXT_WHITE  lv_color_make(0xf0, 0xf4, 0xf8)
#define COLOR_TEXT_MUTED  lv_color_make(0x70, 0x82, 0x98)

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

/* NVS Storage Namespace & Keys */
#define NVS_NAMESPACE       "disp_cfg"
#define NVS_KEY_BRIGHTNESS  "bright"
#define NVS_KEY_SLEEP_EN    "sleep_en"
#define NVS_KEY_SLEEP_SEC   "sleep_sec"

/* Settings State */
static uint8_t  s_brightness       = 80;    /* 10% .. 100% */
static bool     s_sleep_enabled    = true;
static uint32_t s_sleep_timeout_sec= 600;   /* Default: 10 mins (600s) */
static bool     s_is_sleeping      = false;

/* Sleep Timeout Presets */
typedef struct {
    uint32_t seconds;
    const char *label;
} sleep_preset_t;

static const sleep_preset_t s_presets[4] = {
    {600,   "10 MIN"},
    {1800,  "30 MIN"},
    {3600,  "1 HOUR"},
    {7200,  "2 HOURS"}
};

/* UI Widgets */
static lv_obj_t *s_view_disp        = NULL;
static lv_obj_t *s_slider_bright    = NULL;
static lv_obj_t *s_lbl_bright_val   = NULL;
static lv_obj_t *s_sw_sleep         = NULL;
static lv_obj_t *s_lbl_sleep_status = NULL;
static lv_obj_t *s_btn_presets[4]   = {NULL};
static lv_obj_t *s_lbl_presets[4]   = {NULL};
static lv_obj_t *s_lbl_idle_telemetry = NULL;

static lv_timer_t *s_sleep_watchdog_timer = NULL;

/* --------------------------------------------------------------------
 * NVS Operations
 * -------------------------------------------------------------------- */
static void load_settings_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t u8_val = 0;
        if (nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &u8_val) == ESP_OK) {
            if (u8_val >= 10 && u8_val <= 100) s_brightness = u8_val;
        }
        if (nvs_get_u8(handle, NVS_KEY_SLEEP_EN, &u8_val) == ESP_OK) {
            s_sleep_enabled = (u8_val != 0);
        }
        uint32_t u32_val = 0;
        if (nvs_get_u32(handle, NVS_KEY_SLEEP_SEC, &u32_val) == ESP_OK) {
            if (u32_val == 600 || u32_val == 1800 || u32_val == 3600 || u32_val == 7200) {
                s_sleep_timeout_sec = u32_val;
            }
        }
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Loaded settings: Brightness=%d%%, SleepEn=%d, Timeout=%lus",
             s_brightness, s_sleep_enabled, (unsigned long)s_sleep_timeout_sec);
}

static void save_settings_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_BRIGHTNESS, s_brightness);
        nvs_set_u8(handle, NVS_KEY_SLEEP_EN, s_sleep_enabled ? 1 : 0);
        nvs_set_u32(handle, NVS_KEY_SLEEP_SEC, s_sleep_timeout_sec);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

/* --------------------------------------------------------------------
 * Watchdog Timer: handles screen sleep & wake
 * -------------------------------------------------------------------- */
static void sleep_watchdog_cb(lv_timer_t *timer)
{
    (void)timer;

    uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);

    /* Update idle telemetry in UI if page is currently visible */
    if (s_lbl_idle_telemetry && lv_obj_is_valid(s_lbl_idle_telemetry)) {
        char buf[80];
        uint32_t idle_sec = inactive_ms / 1000;
        if (s_sleep_enabled) {
            uint32_t remain_sec = (s_sleep_timeout_sec > idle_sec) ? (s_sleep_timeout_sec - idle_sec) : 0;
            snprintf(buf, sizeof(buf), "IDLE: %lus / SLEEP IN: %lus",
                     (unsigned long)idle_sec, (unsigned long)remain_sec);
        } else {
            snprintf(buf, sizeof(buf), "IDLE: %lus (AUTO-SLEEP OFF)", (unsigned long)idle_sec);
        }
        lv_label_set_text(s_lbl_idle_telemetry, buf);
    }

    if (!s_sleep_enabled) {
        if (s_is_sleeping) {
            s_is_sleeping = false;
            bsp_display_brightness_set(s_brightness);
        }
        return;
    }

    uint32_t target_ms = s_sleep_timeout_sec * 1000ULL;

    if (!s_is_sleeping) {
        if (inactive_ms >= target_ms) {
            s_is_sleeping = true;
            bsp_display_brightness_set(0);
            ESP_LOGI(TAG, "Display entered sleep mode (inactivity: %lums)", (unsigned long)inactive_ms);
        }
    } else {
        /* Woken up by touch activity */
        if (inactive_ms < 1000) {
            s_is_sleeping = false;
            bsp_display_brightness_set(s_brightness);
            ESP_LOGI(TAG, "Display woke up from touch!");
        }
    }
}

void gui_display_wake_up(void)
{
    if (s_is_sleeping) {
        s_is_sleeping = false;
        bsp_display_brightness_set(s_brightness);
    }
    lv_disp_trig_activity(NULL);
}

/* --------------------------------------------------------------------
 * Public Getters / Setters
 * -------------------------------------------------------------------- */
uint8_t gui_display_get_brightness(void)
{
    return s_brightness;
}

void gui_display_set_brightness(uint8_t val)
{
    if (val < 10) val = 10;
    if (val > 100) val = 100;
    s_brightness = val;
    bsp_display_brightness_set(s_brightness);
    save_settings_to_nvs();
}

bool gui_display_get_sleep_enabled(void)
{
    return s_sleep_enabled;
}

void gui_display_set_sleep_enabled(bool enabled)
{
    s_sleep_enabled = enabled;
    save_settings_to_nvs();
}

uint32_t gui_display_get_sleep_timeout(void)
{
    return s_sleep_timeout_sec;
}

void gui_display_set_sleep_timeout(uint32_t timeout_sec)
{
    s_sleep_timeout_sec = timeout_sec;
    save_settings_to_nvs();
}

/* --------------------------------------------------------------------
 * UI Event Handlers
 * -------------------------------------------------------------------- */
static void update_preset_buttons_ui(void)
{
    for (int i = 0; i < 4; i++) {
        if (!s_btn_presets[i] || !lv_obj_is_valid(s_btn_presets[i])) continue;

        bool active = (s_presets[i].seconds == s_sleep_timeout_sec);
        if (active) {
            lv_obj_set_style_bg_color(s_btn_presets[i], COLOR_ORANGE, 0);
            lv_obj_set_style_border_color(s_btn_presets[i], COLOR_ORANGE, 0);
            if (s_lbl_presets[i] && lv_obj_is_valid(s_lbl_presets[i])) {
                lv_obj_set_style_text_color(s_lbl_presets[i], COLOR_TEXT_WHITE, 0);
            }
        } else {
            lv_obj_set_style_bg_color(s_btn_presets[i], COLOR_PANEL_BG, 0);
            lv_obj_set_style_border_color(s_btn_presets[i], COLOR_BORDER_LINE, 0);
            if (s_lbl_presets[i] && lv_obj_is_valid(s_lbl_presets[i])) {
                lv_obj_set_style_text_color(s_lbl_presets[i], COLOR_TEXT_MUTED, 0);
            }
        }
    }
}

static void update_sleep_status_label(void)
{
    if (!s_lbl_sleep_status || !lv_obj_is_valid(s_lbl_sleep_status)) return;

    if (!s_sleep_enabled) {
        lv_label_set_text(s_lbl_sleep_status, "#708298 [ DISABLED ] DISPLAY ALWAYS ON#");
    } else {
        const char *preset_name = "10 MIN";
        for (int i = 0; i < 4; i++) {
            if (s_presets[i].seconds == s_sleep_timeout_sec) {
                preset_name = s_presets[i].label;
                break;
            }
        }
        char buf[80];
        snprintf(buf, sizeof(buf), "#33d17a [ ENABLED ]# SLEEP AFTER %s", preset_name);
        lv_label_set_text(s_lbl_sleep_status, buf);
    }
}

static void slider_brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    if (val < 10) val = 10;
    if (val > 100) val = 100;

    s_brightness = (uint8_t)val;
    bsp_display_brightness_set(s_brightness);

    if (s_lbl_bright_val && lv_obj_is_valid(s_lbl_bright_val)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", s_brightness);
        lv_label_set_text(s_lbl_bright_val, buf);
    }
}

static void slider_brightness_release_cb(lv_event_t *e)
{
    (void)e;
    save_settings_to_nvs();
}

static void switch_sleep_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    s_sleep_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    update_sleep_status_label();
    save_settings_to_nvs();
}

static void btn_preset_cb(lv_event_t *e)
{
    uint32_t sec = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    s_sleep_timeout_sec = sec;
    update_preset_buttons_ui();
    update_sleep_status_label();
    save_settings_to_nvs();
}

static void btn_back_cb(lv_event_t *e)
{
    (void)e;
    if (s_view_disp) {
        lv_obj_del(s_view_disp);
        s_view_disp = NULL;
        s_slider_bright = NULL;
        s_lbl_bright_val = NULL;
        s_sw_sleep = NULL;
        s_lbl_sleep_status = NULL;
        s_lbl_idle_telemetry = NULL;
        for (int i = 0; i < 4; i++) {
            s_btn_presets[i] = NULL;
            s_lbl_presets[i] = NULL;
        }
    }
}

/* --------------------------------------------------------------------
 * Subsystem Initialization
 * -------------------------------------------------------------------- */
void gui_display_settings_init(void)
{
    load_settings_from_nvs();

    /* Apply initial brightness */
    bsp_display_brightness_set(s_brightness);

    /* Inactivity watchdog running every 1000ms */
    if (!s_sleep_watchdog_timer) {
        s_sleep_watchdog_timer = lv_timer_create(sleep_watchdog_cb, 1000, NULL);
    }
}

/* --------------------------------------------------------------------
 * Create Subpage View
 * -------------------------------------------------------------------- */
void gui_display_settings_open(lv_obj_t *parent)
{
    if (s_view_disp) return;

    s_view_disp = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(s_view_disp, 480, 320);
    lv_obj_set_pos(s_view_disp, 0, 0);
    lv_obj_set_style_bg_color(s_view_disp, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_view_disp, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_view_disp, 0, 0);
    lv_obj_set_style_pad_all(s_view_disp, 0, 0);
    lv_obj_clear_flag(s_view_disp, LV_OBJ_FLAG_SCROLLABLE);

    /* Top Left Back Button */
    lv_obj_t *btn_back = lv_btn_create(s_view_disp);
    lv_obj_set_size(btn_back, 100, 28);
    lv_obj_set_pos(btn_back, 10, 8);
    lv_obj_set_style_bg_color(btn_back, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_back, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 2, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< SETTINGS");
    lv_obj_set_style_text_font(lbl_back, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_back, COLOR_ORANGE, 0);
    lv_obj_center(lbl_back);

    /* Header Title */
    lv_obj_t *lbl_title = lv_label_create(s_view_disp);
    lv_label_set_text(lbl_title, "DISPLAY CONTROL\n// OPTICS & SLEEP");
    lv_obj_set_style_text_font(lbl_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_title, 118, 4);
    lv_obj_set_width(lbl_title, 195);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_WRAP);

    /* Top Right Idle Telemetry Badge */
    lv_obj_t *badge = lv_obj_create(s_view_disp);
    lv_obj_set_size(badge, 150, 26);
    lv_obj_set_pos(badge, 320, 9);
    lv_obj_set_style_bg_color(badge, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(badge, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_radius(badge, 2, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_idle_telemetry = lv_label_create(badge);
    lv_label_set_text(s_lbl_idle_telemetry, "IDLE: 0s");
    lv_obj_set_style_text_font(s_lbl_idle_telemetry, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_idle_telemetry, COLOR_MINT, 0);
    lv_obj_center(s_lbl_idle_telemetry);

    /* Top Header Divider Line */
    lv_obj_t *line_top = lv_obj_create(s_view_disp);
    lv_obj_set_size(line_top, 460, 1);
    lv_obj_set_pos(line_top, 10, 42);
    lv_obj_set_style_bg_color(line_top, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(line_top, 0, 0);

    /* Main Container (Scrollable) */
    lv_obj_t *cont = lv_obj_create(s_view_disp);
    lv_obj_set_size(cont, 460, 268);
    lv_obj_set_pos(cont, 10, 46);
    lv_obj_set_style_bg_color(cont, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cont, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 3, 0);
    lv_obj_set_style_pad_all(cont, 12, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* ================================================================
     * SECTION 1: LUMINANCE / BRIGHTNESS CONTROL
     * ================================================================ */
    lv_obj_t *sec1_title = lv_label_create(cont);
    lv_label_set_text(sec1_title, ":: BACKLIGHT LUMINANCE (PWM)");
    lv_obj_set_style_text_font(sec1_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(sec1_title, COLOR_ORANGE, 0);
    lv_obj_set_pos(sec1_title, 4, 4);

    /* Brightness value label badge */
    s_lbl_bright_val = lv_label_create(cont);
    char buf_brt[16];
    snprintf(buf_brt, sizeof(buf_brt), "%d%%", s_brightness);
    lv_label_set_text(s_lbl_bright_val, buf_brt);
    lv_obj_set_style_text_font(s_lbl_bright_val, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(s_lbl_bright_val, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(s_lbl_bright_val, 380, 2);

    /* Brightness Slider */
    s_slider_bright = lv_slider_create(cont);
    lv_obj_set_size(s_slider_bright, 430, 16);
    lv_obj_set_pos(s_slider_bright, 4, 30);
    lv_slider_set_range(s_slider_bright, 10, 100);
    lv_slider_set_value(s_slider_bright, s_brightness, LV_ANIM_OFF);

    /* Styling slider */
    lv_obj_set_style_bg_color(s_slider_bright, lv_color_make(0x18, 0x24, 0x38), LV_PART_MAIN);
    lv_obj_set_style_radius(s_slider_bright, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider_bright, COLOR_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_slider_bright, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_bright, COLOR_TEXT_WHITE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider_bright, 4, LV_PART_KNOB);
    lv_obj_set_style_radius(s_slider_bright, 6, LV_PART_KNOB);

    lv_obj_add_event_cb(s_slider_bright, slider_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_slider_bright, slider_brightness_release_cb, LV_EVENT_RELEASED, NULL);

    /* Quick marks under slider */
    lv_obj_t *lbl_min = lv_label_create(cont);
    lv_label_set_text(lbl_min, "10% MIN");
    lv_obj_set_style_text_font(lbl_min, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_min, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_min, 4, 52);

    lv_obj_t *lbl_max = lv_label_create(cont);
    lv_label_set_text(lbl_max, "100% MAX");
    lv_obj_set_style_text_font(lbl_max, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_max, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_max, 360, 52);

    /* Horizontal separator */
    lv_obj_t *sep = lv_obj_create(cont);
    lv_obj_set_size(sep, 430, 1);
    lv_obj_set_pos(sep, 4, 76);
    lv_obj_set_style_bg_color(sep, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    /* ================================================================
     * SECTION 2: AUTO STANDBY & SLEEP TIMER
     * ================================================================ */
    lv_obj_t *sec2_title = lv_label_create(cont);
    lv_label_set_text(sec2_title, ":: AUTO DISPLAY STANDBY (SLEEP MODE)");
    lv_obj_set_style_text_font(sec2_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(sec2_title, COLOR_ORANGE, 0);
    lv_obj_set_pos(sec2_title, 4, 88);

    /* Toggle Switch */
    s_sw_sleep = lv_switch_create(cont);
    lv_obj_set_size(s_sw_sleep, 46, 24);
    lv_obj_set_pos(s_sw_sleep, 386, 84);
    lv_obj_set_style_bg_color(s_sw_sleep, lv_color_make(0x18, 0x24, 0x38), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sw_sleep, COLOR_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (s_sleep_enabled) {
        lv_obj_add_state(s_sw_sleep, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_sw_sleep, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_sw_sleep, switch_sleep_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Sleep Status description */
    s_lbl_sleep_status = lv_label_create(cont);
    lv_label_set_recolor(s_lbl_sleep_status, true);
    lv_obj_set_style_text_font(s_lbl_sleep_status, &font_orbitron_12, 0);
    lv_obj_set_pos(s_lbl_sleep_status, 4, 114);
    update_sleep_status_label();

    /* Sub-label for presets */
    lv_obj_t *lbl_preset_head = lv_label_create(cont);
    lv_label_set_text(lbl_preset_head, "INACTIVITY TIMEOUT PRESETS:");
    lv_obj_set_style_text_font(lbl_preset_head, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_preset_head, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_preset_head, 4, 140);

    /* 4 Preset Buttons (10 Min, 30 Min, 1 Hour, 2 Hours) */
    int btn_w = 100;
    int btn_h = 42;
    int spacing = 10;
    int start_x = 4;
    int start_y = 162;

    for (int i = 0; i < 4; i++) {
        s_btn_presets[i] = lv_btn_create(cont);
        lv_obj_set_size(s_btn_presets[i], btn_w, btn_h);
        lv_obj_set_pos(s_btn_presets[i], start_x + i * (btn_w + spacing), start_y);
        lv_obj_set_style_radius(s_btn_presets[i], 3, 0);
        lv_obj_set_style_border_width(s_btn_presets[i], 1, 0);
        lv_obj_clear_flag(s_btn_presets[i], LV_OBJ_FLAG_SCROLLABLE);

        s_lbl_presets[i] = lv_label_create(s_btn_presets[i]);
        lv_label_set_text(s_lbl_presets[i], s_presets[i].label);
        lv_obj_set_style_text_font(s_lbl_presets[i], &font_orbitron_12, 0);
        lv_obj_center(s_lbl_presets[i]);

        lv_obj_add_event_cb(s_btn_presets[i], btn_preset_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s_presets[i].seconds);
    }

    update_preset_buttons_ui();

    /* Information / Hint Box at bottom */
    lv_obj_t *hint_box = lv_obj_create(cont);
    lv_obj_set_size(hint_box, 430, 32);
    lv_obj_set_pos(hint_box, 4, 214);
    lv_obj_set_style_bg_color(hint_box, lv_color_make(0x0a, 0x12, 0x20), 0);
    lv_obj_set_style_border_color(hint_box, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(hint_box, 1, 0);
    lv_obj_set_style_radius(hint_box, 2, 0);
    lv_obj_set_style_pad_all(hint_box, 0, 0);
    lv_obj_clear_flag(hint_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_hint = lv_label_create(hint_box);
    lv_label_set_text(lbl_hint, "TIP: TOUCH ANYWHERE ON SCREEN TO WAKE UP DISPLAY");
    lv_obj_set_style_text_font(lbl_hint, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_hint, COLOR_TEXT_MUTED, 0);
    lv_obj_center(lbl_hint);
}
