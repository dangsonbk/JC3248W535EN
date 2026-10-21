#include "gui_display_settings.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "display.h"
#include "esp_bsp.h"
#include "gui_screensaver.h"
#include "ui_frame.h"

static const char *TAG = "GUI_DISP_SET";

/* Sci-Fi Palette matching GUI HUD */
#define COLOR_BG          lv_color_make(0x08, 0x0c, 0x14)
#define COLOR_PANEL_BG    lv_color_make(0x0e, 0x16, 0x26)
#define COLOR_BORDER_LINE lv_color_make(0x1a, 0x2b, 0x44)
#define COLOR_ORANGE      lv_color_make(0xff, 0x5a, 0x2a)
#define COLOR_MINT        lv_color_make(0x33, 0xd1, 0x7a)
#define COLOR_TEXT_WHITE  lv_color_make(0xf0, 0xf4, 0xf8)
#define COLOR_TEXT_MUTED  lv_color_make(0xa0, 0xb4, 0xc8)

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

/* NVS Storage Namespace & Keys */
#define NVS_NAMESPACE       "disp_cfg"
#define NVS_KEY_BRIGHTNESS  "bright"
#define NVS_KEY_SLEEP_EN    "sleep_en"
#define NVS_KEY_SLEEP_SEC   "sleep_sec"
#define NVS_KEY_SAVER_EN    "saver_en"
#define NVS_KEY_SAVER_SEC   "saver_sec"
#define NVS_KEY_SAVER_FX    "saver_fx"

typedef enum {
    DISP_TAB_SLEEP = 0,
    DISP_TAB_SAVER = 1
} disp_tab_t;

/* Settings State */
static uint8_t  s_brightness       = 80;    /* 10% .. 100% */
static bool     s_sleep_enabled    = true;
static uint32_t s_sleep_timeout_sec= 600;   /* Default: 10 mins (600s) */
static bool     s_is_sleeping      = false;

/* Screensaver State */
static bool     s_saver_enabled    = true;
static uint32_t s_saver_timeout_sec= 120;   /* Default: 2 mins (120s) */
static screensaver_effect_t s_saver_fx = SCREENSAVER_FX_MATRIX;

static disp_tab_t s_active_tab = DISP_TAB_SLEEP;

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

static const sleep_preset_t s_saver_presets[4] = {
    {60,   "1 MIN"},
    {120,  "2 MIN"},
    {300,  "5 MIN"},
    {600,  "10 MIN"}
};

/* UI Widgets */
static lv_obj_t *s_view_disp        = NULL;
static lv_obj_t *s_cont_tab_sleep   = NULL;
static lv_obj_t *s_cont_tab_saver   = NULL;
static lv_obj_t *s_btn_tab_sleep    = NULL;
static lv_obj_t *s_btn_tab_saver    = NULL;

/* Sleep subwidgets */
static lv_obj_t *s_slider_bright    = NULL;
static lv_obj_t *s_lbl_bright_val   = NULL;
static lv_obj_t *s_sw_sleep         = NULL;
static lv_obj_t *s_lbl_sleep_status = NULL;
static lv_obj_t *s_btn_presets[4]   = {NULL};
static lv_obj_t *s_lbl_presets[4]   = {NULL};
static lv_obj_t *s_lbl_idle_telemetry = NULL;

/* Saver subwidgets */
static lv_obj_t *s_sw_saver         = NULL;
static lv_obj_t *s_lbl_saver_status = NULL;
static lv_obj_t *s_btn_saver_pre[4] = {NULL};
static lv_obj_t *s_lbl_saver_pre[4] = {NULL};
static lv_obj_t *s_btn_fx[3]        = {NULL};
static lv_obj_t *s_lbl_fx[3]        = {NULL};

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
        if (nvs_get_u8(handle, NVS_KEY_SAVER_EN, &u8_val) == ESP_OK) {
            s_saver_enabled = (u8_val != 0);
        }
        if (nvs_get_u32(handle, NVS_KEY_SAVER_SEC, &u32_val) == ESP_OK) {
            if (u32_val == 60 || u32_val == 120 || u32_val == 300 || u32_val == 600) {
                s_saver_timeout_sec = u32_val;
            }
        }
        if (nvs_get_u8(handle, NVS_KEY_SAVER_FX, &u8_val) == ESP_OK) {
            if (u8_val < SCREENSAVER_FX_COUNT) s_saver_fx = (screensaver_effect_t)u8_val;
        }
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Loaded: Brightness=%d%%, SleepEn=%d, Timeout=%lus, SaverEn=%d, SaverTime=%lus, SaverFx=%d",
             s_brightness, s_sleep_enabled, (unsigned long)s_sleep_timeout_sec,
             s_saver_enabled, (unsigned long)s_saver_timeout_sec, (int)s_saver_fx);
}

static void save_settings_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, NVS_KEY_BRIGHTNESS, s_brightness);
        nvs_set_u8(handle, NVS_KEY_SLEEP_EN, s_sleep_enabled ? 1 : 0);
        nvs_set_u32(handle, NVS_KEY_SLEEP_SEC, s_sleep_timeout_sec);
        nvs_set_u8(handle, NVS_KEY_SAVER_EN, s_saver_enabled ? 1 : 0);
        nvs_set_u32(handle, NVS_KEY_SAVER_SEC, s_saver_timeout_sec);
        nvs_set_u8(handle, NVS_KEY_SAVER_FX, (uint8_t)s_saver_fx);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

/* --------------------------------------------------------------------
 * Watchdog Timer: handles screen sleep & screensaver
 * -------------------------------------------------------------------- */
static void sleep_watchdog_cb(lv_timer_t *timer)
{
    (void)timer;

    uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);

    /* Update idle telemetry badge in UI */
    if (s_lbl_idle_telemetry && lv_obj_is_valid(s_lbl_idle_telemetry)) {
        char buf[80];
        uint32_t idle_sec = inactive_ms / 1000;
        if (s_saver_enabled) {
            uint32_t remain_sec = (s_saver_timeout_sec > idle_sec) ? (s_saver_timeout_sec - idle_sec) : 0;
            snprintf(buf, sizeof(buf), "IDLE: %lus / SAVER IN: %lus",
                     (unsigned long)idle_sec, (unsigned long)remain_sec);
        } else if (s_sleep_enabled) {
            uint32_t remain_sec = (s_sleep_timeout_sec > idle_sec) ? (s_sleep_timeout_sec - idle_sec) : 0;
            snprintf(buf, sizeof(buf), "IDLE: %lus / SLEEP IN: %lus",
                     (unsigned long)idle_sec, (unsigned long)remain_sec);
        } else {
            snprintf(buf, sizeof(buf), "IDLE: %lus (ALWAYS ON)", (unsigned long)idle_sec);
        }
        lv_label_set_text(s_lbl_idle_telemetry, buf);
    }

    /* Screen Saver Priority Check:
     * Rule: If screen saver is enabled, display NEVER turns off; screensaver runs instead! */
    if (s_saver_enabled) {
        if (s_is_sleeping) {
            s_is_sleeping = false;
            bsp_display_brightness_set(s_brightness);
        }
        if (!gui_screensaver_is_active()) {
            if (inactive_ms >= (s_saver_timeout_sec * 1000ULL)) {
                gui_screensaver_start(s_saver_fx);
            }
        }
        return;
    }

    /* Standard Display Standby (Sleep) Mode */
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
        if (inactive_ms < 1000) {
            s_is_sleeping = false;
            bsp_display_brightness_set(s_brightness);
            ESP_LOGI(TAG, "Display woke up from touch!");
        }
    }
}

void gui_display_wake_up(void)
{
    if (gui_screensaver_is_active()) {
        gui_screensaver_stop();
    }
    if (s_is_sleeping) {
        s_is_sleeping = false;
        bsp_display_brightness_set(s_brightness);
    }
    lv_disp_trig_activity(NULL);
}

/* --------------------------------------------------------------------
 * Getters & Setters
 * -------------------------------------------------------------------- */
uint8_t gui_display_get_brightness(void) { return s_brightness; }
void gui_display_set_brightness(uint8_t val)
{
    if (val < 10) val = 10;
    if (val > 100) val = 100;
    s_brightness = val;
    bsp_display_brightness_set(s_brightness);
    save_settings_to_nvs();
}

bool gui_display_get_sleep_enabled(void) { return s_sleep_enabled; }
void gui_display_set_sleep_enabled(bool enabled)
{
    s_sleep_enabled = enabled;
    save_settings_to_nvs();
}

uint32_t gui_display_get_sleep_timeout(void) { return s_sleep_timeout_sec; }
void gui_display_set_sleep_timeout(uint32_t timeout_sec)
{
    s_sleep_timeout_sec = timeout_sec;
    save_settings_to_nvs();
}

bool gui_display_get_screensaver_enabled(void) { return s_saver_enabled; }
void gui_display_set_screensaver_enabled(bool enabled)
{
    s_saver_enabled = enabled;
    save_settings_to_nvs();
}

uint32_t gui_display_get_screensaver_timeout(void) { return s_saver_timeout_sec; }
void gui_display_set_screensaver_timeout(uint32_t timeout_sec)
{
    s_saver_timeout_sec = timeout_sec;
    save_settings_to_nvs();
}

screensaver_effect_t gui_display_get_screensaver_fx(void) { return s_saver_fx; }
void gui_display_set_screensaver_fx(screensaver_effect_t fx)
{
    s_saver_fx = fx;
    save_settings_to_nvs();
}

/* --------------------------------------------------------------------
 * UI Event Handlers
 * -------------------------------------------------------------------- */
static void update_preset_buttons_ui(void)
{
    for (int i = 0; i < 4; i++) {
        if (!s_btn_presets[i] || !lv_obj_is_valid(s_btn_presets[i])) continue;
        if (s_presets[i].seconds == s_sleep_timeout_sec) {
            lv_obj_set_style_bg_color(s_btn_presets[i], COLOR_ORANGE, 0);
            lv_obj_set_style_border_color(s_btn_presets[i], COLOR_ORANGE, 0);
            if (s_lbl_presets[i]) lv_obj_set_style_text_color(s_lbl_presets[i], lv_color_hex(0x111413), 0);
        } else {
            lv_obj_set_style_bg_color(s_btn_presets[i], lv_color_make(0x12, 0x1c, 0x30), 0);
            lv_obj_set_style_border_color(s_btn_presets[i], COLOR_BORDER_LINE, 0);
            if (s_lbl_presets[i]) lv_obj_set_style_text_color(s_lbl_presets[i], COLOR_TEXT_WHITE, 0);
        }
    }
}

static void update_saver_preset_buttons_ui(void)
{
    for (int i = 0; i < 4; i++) {
        if (!s_btn_saver_pre[i] || !lv_obj_is_valid(s_btn_saver_pre[i])) continue;
        if (s_saver_presets[i].seconds == s_saver_timeout_sec) {
            lv_obj_set_style_bg_color(s_btn_saver_pre[i], COLOR_MINT, 0);
            lv_obj_set_style_border_color(s_btn_saver_pre[i], COLOR_MINT, 0);
            if (s_lbl_saver_pre[i]) lv_obj_set_style_text_color(s_lbl_saver_pre[i], lv_color_hex(0x111413), 0);
        } else {
            lv_obj_set_style_bg_color(s_btn_saver_pre[i], lv_color_make(0x12, 0x1c, 0x30), 0);
            lv_obj_set_style_border_color(s_btn_saver_pre[i], COLOR_BORDER_LINE, 0);
            if (s_lbl_saver_pre[i]) lv_obj_set_style_text_color(s_lbl_saver_pre[i], COLOR_TEXT_WHITE, 0);
        }
    }
}

static void update_fx_buttons_ui(void)
{
    for (int i = 0; i < 3; i++) {
        if (!s_btn_fx[i] || !lv_obj_is_valid(s_btn_fx[i])) continue;
        if ((screensaver_effect_t)i == s_saver_fx) {
            lv_obj_set_style_bg_color(s_btn_fx[i], COLOR_MINT, 0);
            lv_obj_set_style_border_color(s_btn_fx[i], COLOR_MINT, 0);
            if (s_lbl_fx[i]) lv_obj_set_style_text_color(s_lbl_fx[i], lv_color_hex(0x111413), 0);
        } else {
            lv_obj_set_style_bg_color(s_btn_fx[i], lv_color_make(0x12, 0x1c, 0x30), 0);
            lv_obj_set_style_border_color(s_btn_fx[i], COLOR_BORDER_LINE, 0);
            if (s_lbl_fx[i]) lv_obj_set_style_text_color(s_lbl_fx[i], COLOR_TEXT_WHITE, 0);
        }
    }
}

static void switch_tab(disp_tab_t tab)
{
    s_active_tab = tab;
    if (tab == DISP_TAB_SLEEP) {
        lv_obj_clear_flag(s_cont_tab_sleep, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cont_tab_saver, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_btn_tab_sleep, COLOR_ORANGE, 0);
        lv_obj_set_style_bg_color(s_btn_tab_saver, lv_color_make(0x14, 0x1a, 0x24), 0);
    } else {
        lv_obj_add_flag(s_cont_tab_sleep, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_cont_tab_saver, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_btn_tab_sleep, lv_color_make(0x14, 0x1a, 0x24), 0);
        lv_obj_set_style_bg_color(s_btn_tab_saver, COLOR_MINT, 0);
    }
}

static void tab_btn_click_cb(lv_event_t *e)
{
    disp_tab_t tab = (disp_tab_t)(uintptr_t)lv_event_get_user_data(e);
    switch_tab(tab);
}

static void slider_brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    s_brightness = (uint8_t)lv_slider_get_value(slider);
    bsp_display_brightness_set(s_brightness);
    if (s_lbl_bright_val) {
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
    save_settings_to_nvs();
}

static void switch_saver_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    s_saver_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_settings_to_nvs();
    if (s_lbl_saver_status) {
        char buf[64];
        if (s_saver_enabled) {
            snprintf(buf, sizeof(buf), "#33d17a [ ACTIVE ]# TRIGGERS IN %lus (SCREEN NEVER SLEEPS)",
                     (unsigned long)s_saver_timeout_sec);
        } else {
            snprintf(buf, sizeof(buf), "#ff5a2a [ DISABLED ]# STANDARD AUTO-SLEEP APPLIES");
        }
        lv_label_set_text(s_lbl_saver_status, buf);
    }
}

static void btn_preset_cb(lv_event_t *e)
{
    uint32_t sec = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    s_sleep_timeout_sec = sec;
    update_preset_buttons_ui();
    save_settings_to_nvs();
}

static void btn_saver_pre_cb(lv_event_t *e)
{
    uint32_t sec = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    s_saver_timeout_sec = sec;
    update_saver_preset_buttons_ui();
    save_settings_to_nvs();
    if (s_lbl_saver_status && s_saver_enabled) {
        char buf[64];
        snprintf(buf, sizeof(buf), "#33d17a [ ACTIVE ]# TRIGGERS IN %lus (SCREEN NEVER SLEEPS)",
                 (unsigned long)s_saver_timeout_sec);
        lv_label_set_text(s_lbl_saver_status, buf);
    }
}

static void btn_fx_select_cb(lv_event_t *e)
{
    screensaver_effect_t fx = (screensaver_effect_t)(uintptr_t)lv_event_get_user_data(e);
    s_saver_fx = fx;
    update_fx_buttons_ui();
    save_settings_to_nvs();
}

static void btn_preview_cb(lv_event_t *e)
{
    (void)e;
    gui_screensaver_start(s_saver_fx);
}

static void on_display_settings_close(void)
{
    s_view_disp = NULL;
    s_cont_tab_sleep = NULL;
    s_cont_tab_saver = NULL;
    s_slider_bright = NULL;
    s_lbl_bright_val = NULL;
    s_sw_sleep = NULL;
    s_lbl_sleep_status = NULL;
    s_lbl_idle_telemetry = NULL;
    s_sw_saver = NULL;
    s_lbl_saver_status = NULL;
    for (int i = 0; i < 4; i++) {
        s_btn_presets[i] = NULL;
        s_lbl_presets[i] = NULL;
        s_btn_saver_pre[i] = NULL;
        s_lbl_saver_pre[i] = NULL;
    }
    for (int i = 0; i < 3; i++) {
        s_btn_fx[i] = NULL;
        s_lbl_fx[i] = NULL;
    }
}

/* --------------------------------------------------------------------
 * Subsystem Initialization
 * -------------------------------------------------------------------- */
void gui_display_settings_init(void)
{
    load_settings_from_nvs();
    bsp_display_brightness_set(s_brightness);

    if (!s_sleep_watchdog_timer) {
        s_sleep_watchdog_timer = lv_timer_create(sleep_watchdog_cb, 1000, NULL);
    }
}

/* --------------------------------------------------------------------
 * Create Subpage View with Tabs
 * -------------------------------------------------------------------- */
void gui_display_settings_open(lv_obj_t *parent)
{
    (void)parent;
    if (s_view_disp) return;

    ui_page_config_t cfg = {
        .title = "DISPLAY",
        .breadcrumb = "DISPLAY // LUMINANCE & SAVER",
        .mode = UI_FRAME_DEFAULT,
        .on_close = on_display_settings_close,
        .user_data = NULL
    };
    ui_frame_t *frame = ui_frame_push(&cfg);
    if (!frame) return;
    s_view_disp = frame->root;

    /* Tab Switch Buttons inside frame->content */
    s_btn_tab_sleep = lv_btn_create(frame->content);
    lv_obj_set_size(s_btn_tab_sleep, 140, 26);
    lv_obj_set_pos(s_btn_tab_sleep, 0, 0);
    lv_obj_set_style_radius(s_btn_tab_sleep, 2, 0);
    lv_obj_set_style_border_width(s_btn_tab_sleep, 0, 0);
    lv_obj_add_event_cb(s_btn_tab_sleep, tab_btn_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)DISP_TAB_SLEEP);
    lv_obj_t *lbl_t1 = lv_label_create(s_btn_tab_sleep);
    lv_label_set_text(lbl_t1, "BRIGHT / SLEEP");
    lv_obj_set_style_text_font(lbl_t1, &font_orbitron_12, 0);
    lv_obj_center(lbl_t1);

    s_btn_tab_saver = lv_btn_create(frame->content);
    lv_obj_set_size(s_btn_tab_saver, 130, 26);
    lv_obj_set_pos(s_btn_tab_saver, 148, 0);
    lv_obj_set_style_radius(s_btn_tab_saver, 2, 0);
    lv_obj_set_style_border_width(s_btn_tab_saver, 0, 0);
    lv_obj_add_event_cb(s_btn_tab_saver, tab_btn_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)DISP_TAB_SAVER);
    lv_obj_t *lbl_t2 = lv_label_create(s_btn_tab_saver);
    lv_label_set_text(lbl_t2, "SCREEN SAVER");
    lv_obj_set_style_text_font(lbl_t2, &font_orbitron_12, 0);
    lv_obj_center(lbl_t2);

    /* ================================================================
     * TAB 1: BRIGHTNESS & SLEEP CONTAINER
     * ================================================================ */
    s_cont_tab_sleep = lv_obj_create(frame->content);
    lv_obj_set_size(s_cont_tab_sleep, 460, 192);
    lv_obj_set_pos(s_cont_tab_sleep, 0, 30);
    lv_obj_set_style_bg_color(s_cont_tab_sleep, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(s_cont_tab_sleep, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(s_cont_tab_sleep, 1, 0);
    lv_obj_set_style_radius(s_cont_tab_sleep, 3, 0);
    lv_obj_set_style_pad_all(s_cont_tab_sleep, 10, 0);
    lv_obj_clear_flag(s_cont_tab_sleep, LV_OBJ_FLAG_SCROLLABLE);

    /* Brightness Section */
    lv_obj_t *sec1_title = lv_label_create(s_cont_tab_sleep);
    lv_label_set_text(sec1_title, ":: BACKLIGHT LUMINANCE (PWM)");
    lv_obj_set_style_text_font(sec1_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(sec1_title, COLOR_ORANGE, 0);
    lv_obj_set_pos(sec1_title, 4, 2);

    s_lbl_bright_val = lv_label_create(s_cont_tab_sleep);
    char buf_brt[16];
    snprintf(buf_brt, sizeof(buf_brt), "%d%%", s_brightness);
    lv_label_set_text(s_lbl_bright_val, buf_brt);
    lv_obj_set_style_text_font(s_lbl_bright_val, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(s_lbl_bright_val, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(s_lbl_bright_val, 380, 0);

    s_slider_bright = lv_slider_create(s_cont_tab_sleep);
    lv_obj_set_size(s_slider_bright, 430, 14);
    lv_obj_set_pos(s_slider_bright, 4, 24);
    lv_slider_set_range(s_slider_bright, 10, 100);
    lv_slider_set_value(s_slider_bright, s_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_bright, lv_color_make(0x18, 0x24, 0x38), LV_PART_MAIN);
    lv_obj_set_style_radius(s_slider_bright, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider_bright, COLOR_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_slider_bright, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_bright, COLOR_TEXT_WHITE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider_bright, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider_bright, slider_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_slider_bright, slider_brightness_release_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *sep = lv_obj_create(s_cont_tab_sleep);
    lv_obj_set_size(sep, 430, 1);
    lv_obj_set_pos(sep, 4, 48);
    lv_obj_set_style_bg_color(sep, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    /* Sleep Section */
    lv_obj_t *sec2_title = lv_label_create(s_cont_tab_sleep);
    lv_label_set_text(sec2_title, ":: AUTO DISPLAY STANDBY (SLEEP)");
    lv_obj_set_style_text_font(sec2_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(sec2_title, COLOR_ORANGE, 0);
    lv_obj_set_pos(sec2_title, 4, 56);

    s_sw_sleep = lv_switch_create(s_cont_tab_sleep);
    lv_obj_set_size(s_sw_sleep, 48, 24);
    lv_obj_set_pos(s_sw_sleep, 386, 52);
    lv_obj_set_style_bg_color(s_sw_sleep, lv_color_make(0x18, 0x24, 0x38), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sw_sleep, COLOR_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(s_sw_sleep, 10);
    if (s_sleep_enabled) lv_obj_add_state(s_sw_sleep, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_sleep, switch_sleep_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_pre = lv_label_create(s_cont_tab_sleep);
    lv_label_set_text(lbl_pre, "STANDBY TIMEOUT (TURNS OFF BACKLIGHT):");
    lv_obj_set_style_text_font(lbl_pre, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_pre, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_pre, 4, 84);

    for (int i = 0; i < 4; i++) {
        s_btn_presets[i] = lv_btn_create(s_cont_tab_sleep);
        lv_obj_set_size(s_btn_presets[i], 100, 32);
        lv_obj_set_pos(s_btn_presets[i], 4 + i * 110, 104);
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

    lv_obj_t *lbl_info1 = lv_label_create(s_cont_tab_sleep);
    lv_label_set_text(lbl_info1, "NOTE: SCREEN SAVER OVERRIDES DISPLAY STANDBY TIMEOUT.");
    lv_obj_set_style_text_font(lbl_info1, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_info1, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_info1, 4, 146);

    /* ================================================================
     * TAB 2: SCREEN SAVER CONTAINER
     * ================================================================ */
    s_cont_tab_saver = lv_obj_create(frame->content);
    lv_obj_set_size(s_cont_tab_saver, 460, 192);
    lv_obj_set_pos(s_cont_tab_saver, 0, 30);
    lv_obj_set_style_bg_color(s_cont_tab_saver, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(s_cont_tab_saver, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(s_cont_tab_saver, 1, 0);
    lv_obj_set_style_radius(s_cont_tab_saver, 3, 0);
    lv_obj_set_style_pad_all(s_cont_tab_saver, 10, 0);
    lv_obj_clear_flag(s_cont_tab_saver, LV_OBJ_FLAG_SCROLLABLE);

    /* Header & Switch */
    lv_obj_t *sec_sav_title = lv_label_create(s_cont_tab_saver);
    lv_label_set_text(sec_sav_title, ":: SCREEN SAVER (NO-SLEEP MODE)");
    lv_obj_set_style_text_font(sec_sav_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(sec_sav_title, COLOR_MINT, 0);
    lv_obj_set_pos(sec_sav_title, 4, 2);

    s_sw_saver = lv_switch_create(s_cont_tab_saver);
    lv_obj_set_size(s_sw_saver, 48, 24);
    lv_obj_set_pos(s_sw_saver, 386, 0);
    lv_obj_set_style_bg_color(s_sw_saver, lv_color_make(0x18, 0x24, 0x38), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sw_saver, COLOR_MINT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(s_sw_saver, 10);
    if (s_saver_enabled) lv_obj_add_state(s_sw_saver, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_saver, switch_saver_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_saver_status = lv_label_create(s_cont_tab_saver);
    lv_label_set_recolor(s_lbl_saver_status, true);
    char buf_ss[80];
    if (s_saver_enabled) {
        snprintf(buf_ss, sizeof(buf_ss), "#33d17a [ ACTIVE ]# TRIGGERS IN %lus (SCREEN NEVER SLEEPS)",
                 (unsigned long)s_saver_timeout_sec);
    } else {
        snprintf(buf_ss, sizeof(buf_ss), "#ff5a2a [ DISABLED ]# STANDARD AUTO-SLEEP APPLIES");
    }
    lv_label_set_text(s_lbl_saver_status, buf_ss);
    lv_obj_set_style_text_font(s_lbl_saver_status, &font_orbitron_12, 0);
    lv_obj_set_pos(s_lbl_saver_status, 4, 24);

    /* Inactivity Timeout selection */
    lv_obj_t *lbl_sav_time = lv_label_create(s_cont_tab_saver);
    lv_label_set_text(lbl_sav_time, "INACTIVITY ACTIVATION DELAY:");
    lv_obj_set_style_text_font(lbl_sav_time, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_sav_time, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_sav_time, 4, 44);

    for (int i = 0; i < 4; i++) {
        s_btn_saver_pre[i] = lv_btn_create(s_cont_tab_saver);
        lv_obj_set_size(s_btn_saver_pre[i], 100, 30);
        lv_obj_set_pos(s_btn_saver_pre[i], 4 + i * 110, 62);
        lv_obj_set_style_radius(s_btn_saver_pre[i], 3, 0);
        lv_obj_set_style_border_width(s_btn_saver_pre[i], 1, 0);
        lv_obj_clear_flag(s_btn_saver_pre[i], LV_OBJ_FLAG_SCROLLABLE);

        s_lbl_saver_pre[i] = lv_label_create(s_btn_saver_pre[i]);
        lv_label_set_text(s_lbl_saver_pre[i], s_saver_presets[i].label);
        lv_obj_set_style_text_font(s_lbl_saver_pre[i], &font_orbitron_12, 0);
        lv_obj_center(s_lbl_saver_pre[i]);

        lv_obj_add_event_cb(s_btn_saver_pre[i], btn_saver_pre_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)s_saver_presets[i].seconds);
    }
    update_saver_preset_buttons_ui();

    /* Effect Selection */
    lv_obj_t *lbl_sav_fx = lv_label_create(s_cont_tab_saver);
    lv_label_set_text(lbl_sav_fx, "CYBERPUNK VISUAL EFFECT:");
    lv_obj_set_style_text_font(lbl_sav_fx, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_sav_fx, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_sav_fx, 4, 96);

    const char *fx_names[3] = {"MATRIX RAIN", "GAME OF LIFE", "STARFIELD WARP"};
    for (int i = 0; i < 3; i++) {
        s_btn_fx[i] = lv_btn_create(s_cont_tab_saver);
        lv_obj_set_size(s_btn_fx[i], 136, 30);
        lv_obj_set_pos(s_btn_fx[i], 4 + i * 148, 114);
        lv_obj_set_style_radius(s_btn_fx[i], 3, 0);
        lv_obj_set_style_border_width(s_btn_fx[i], 1, 0);
        lv_obj_clear_flag(s_btn_fx[i], LV_OBJ_FLAG_SCROLLABLE);

        s_lbl_fx[i] = lv_label_create(s_btn_fx[i]);
        lv_label_set_text(s_lbl_fx[i], fx_names[i]);
        lv_obj_set_style_text_font(s_lbl_fx[i], &font_orbitron_12, 0);
        lv_obj_center(s_lbl_fx[i]);

        lv_obj_add_event_cb(s_btn_fx[i], btn_fx_select_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    update_fx_buttons_ui();

    /* Preview Test Button */
    lv_obj_t *btn_test = lv_btn_create(s_cont_tab_saver);
    lv_obj_set_size(btn_test, 434, 30);
    lv_obj_set_pos(btn_test, 4, 150);
    lv_obj_set_style_bg_color(btn_test, lv_color_make(0x10, 0x24, 0x20), 0);
    lv_obj_set_style_border_color(btn_test, COLOR_MINT, 0);
    lv_obj_set_style_border_width(btn_test, 1, 0);
    lv_obj_set_style_radius(btn_test, 3, 0);
    lv_obj_add_event_cb(btn_test, btn_preview_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_test = lv_label_create(btn_test);
    lv_label_set_text(lbl_test, LV_SYMBOL_PLAY " TEST // PREVIEW SCREEN SAVER NOW");
    lv_obj_set_style_text_font(lbl_test, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_test, COLOR_MINT, 0);
    lv_obj_center(lbl_test);

    /* Footer Action Area for Countdown Telemetry */
    s_lbl_idle_telemetry = lv_label_create(frame->footer_action);
    lv_label_set_text(s_lbl_idle_telemetry, "IDLE TELEMETRY: READY");
    lv_obj_set_style_text_font(s_lbl_idle_telemetry, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_idle_telemetry, COLOR_MINT, 0);
    lv_obj_align(s_lbl_idle_telemetry, LV_ALIGN_LEFT_MID, 4, 0);

    /* Initial tab state */
    switch_tab(s_active_tab);
}

