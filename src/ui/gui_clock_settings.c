#include "gui_clock_settings.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "clock_service.h"
#include "ui_frame.h"

static const char *TAG = "GUI_CLK_SET";

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

#define COLOR_PANEL_BG    lv_color_hex(0x181C1A)
#define COLOR_BORDER_LINE lv_color_hex(0x28332C)
#define COLOR_ORANGE      lv_color_hex(0xFF5A2A)
#define COLOR_MINT        lv_color_hex(0xC2DEC9)
#define COLOR_TEXT_WHITE  lv_color_hex(0xF0F5F1)
#define COLOR_TEXT_MUTED  lv_color_hex(0xA2B7AA)

static lv_obj_t *s_view_clk_set   = NULL;
static lv_obj_t *s_dd_timezone    = NULL;
static lv_obj_t *s_sw_24h         = NULL;
static lv_obj_t *s_sw_alarm       = NULL;
static lv_obj_t *s_lbl_alarm_time = NULL;

/* Timezone options */
typedef struct {
    int8_t offset;
    const char *label;
} tz_option_t;

static const tz_option_t s_tz_options[] = {
    {-8, "UTC -08:00 (US PACIFIC / LA)"},
    {-5, "UTC -05:00 (US EASTERN / NYC)"},
    { 0, "UTC +00:00 (GMT / LONDON)"},
    { 1, "UTC +01:00 (CET / BERLIN / PARIS)"},
    { 7, "UTC +07:00 (VIETNAM / BANGKOK)"},
    { 8, "UTC +08:00 (SINGAPORE / BEIJING)"},
    { 9, "UTC +09:00 (TOKYO / SEOUL)"}
};
static const size_t s_tz_count = sizeof(s_tz_options) / sizeof(s_tz_options[0]);

static void dd_tz_changed_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    if (sel < s_tz_count) {
        clock_config_t cfg;
        memcpy(&cfg, clock_service_get_config(), sizeof(clock_config_t));
        cfg.tz_offset_hours = s_tz_options[sel].offset;
        clock_service_save_config(&cfg);
        ESP_LOGI(TAG, "Selected timezone offset: %d", cfg.tz_offset_hours);
    }
}

static void sw_24h_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    clock_config_t cfg;
    memcpy(&cfg, clock_service_get_config(), sizeof(clock_config_t));
    cfg.use_24h_format = lv_obj_has_state(sw, LV_STATE_CHECKED);
    clock_service_save_config(&cfg);
}

static void sw_alarm_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    clock_config_t cfg;
    memcpy(&cfg, clock_service_get_config(), sizeof(clock_config_t));
    cfg.alarm.enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    clock_service_save_config(&cfg);
}

static void chime_timer_stop_cb(lv_timer_t *tmr)
{
    (void)tmr;
    clock_service_stop_chime();
}

static void btn_test_chime_cb(lv_event_t *e)
{
    (void)e;
    clock_service_play_chime();
    lv_timer_t *t = lv_timer_create(chime_timer_stop_cb, 2500, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void on_clock_settings_close(void)
{
    s_view_clk_set = NULL;
    s_dd_timezone = NULL;
    s_sw_24h = NULL;
    s_sw_alarm = NULL;
    s_lbl_alarm_time = NULL;
}

void gui_clock_settings_open(lv_obj_t *parent)
{
    (void)parent;
    if (s_view_clk_set) return;

    ui_page_config_t page_cfg = {
        .title = "CHRONO",
        .breadcrumb = "CLOCK & CHIME // SETTINGS",
        .mode = UI_FRAME_DEFAULT,
        .on_close = on_clock_settings_close,
        .user_data = NULL
    };
    ui_frame_t *frame = ui_frame_push(&page_cfg);
    if (!frame) return;
    s_view_clk_set = frame->root;

    const clock_config_t *cfg = clock_service_get_config();

    /* Main Container */
    lv_obj_t *cont = lv_obj_create(frame->content);
    lv_obj_set_size(cont, 460, 224);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(cont, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 4, 0);
    lv_obj_set_style_pad_all(cont, 14, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Timezone Dropdown */
    lv_obj_t *lbl_tz = lv_label_create(cont);
    lv_label_set_text(lbl_tz, "LOCAL TIMEZONE (UTC OFFSET):");
    lv_obj_set_style_text_font(lbl_tz, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_tz, COLOR_ORANGE, 0);
    lv_obj_set_pos(lbl_tz, 0, 0);

    s_dd_timezone = lv_dropdown_create(cont);
    lv_obj_set_size(s_dd_timezone, 430, 32);
    lv_obj_set_pos(s_dd_timezone, 0, 20);
    lv_obj_set_style_text_font(s_dd_timezone, &font_orbitron_12, 0);

    /* Populate Dropdown */
    char tz_opts_str[512] = "";
    uint16_t initial_sel = 4; // default UTC+7
    for (size_t i = 0; i < s_tz_count; i++) {
        if (i > 0) strcat(tz_opts_str, "\n");
        strcat(tz_opts_str, s_tz_options[i].label);
        if (s_tz_options[i].offset == cfg->tz_offset_hours) {
            initial_sel = i;
        }
    }
    lv_dropdown_set_options(s_dd_timezone, tz_opts_str);
    lv_dropdown_set_selected(s_dd_timezone, initial_sel);
    lv_obj_add_event_cb(s_dd_timezone, dd_tz_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 2. Format 24H Toggle */
    lv_obj_t *lbl_fmt = lv_label_create(cont);
    lv_label_set_text(lbl_fmt, "24-HOUR MILITARY TIME FORMAT:");
    lv_obj_set_style_text_font(lbl_fmt, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_fmt, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_fmt, 0, 68);

    s_sw_24h = lv_switch_create(cont);
    lv_obj_set_size(s_sw_24h, 48, 24);
    lv_obj_set_pos(s_sw_24h, 380, 64);
    lv_obj_set_style_bg_color(s_sw_24h, COLOR_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(s_sw_24h, 10);
    if (cfg->use_24h_format) lv_obj_add_state(s_sw_24h, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_24h, sw_24h_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Divider */
    lv_obj_t *sep = lv_obj_create(cont);
    lv_obj_set_size(sep, 430, 1);
    lv_obj_set_pos(sep, 0, 100);
    lv_obj_set_style_bg_color(sep, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    /* 3. Alarm Status Toggle */
    lv_obj_t *lbl_alm = lv_label_create(cont);
    lv_label_set_text(lbl_alm, "BUZZER & CHIME ALARM ACTIVE:");
    lv_obj_set_style_text_font(lbl_alm, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_alm, COLOR_MINT, 0);
    lv_obj_set_pos(lbl_alm, 0, 112);

    s_sw_alarm = lv_switch_create(cont);
    lv_obj_set_size(s_sw_alarm, 48, 24);
    lv_obj_set_pos(s_sw_alarm, 380, 108);
    lv_obj_set_style_bg_color(s_sw_alarm, COLOR_MINT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(s_sw_alarm, 10);
    if (cfg->alarm.enabled) lv_obj_add_state(s_sw_alarm, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_alarm, sw_alarm_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_alarm_time = lv_label_create(cont);
    char buf_at[64];
    snprintf(buf_at, sizeof(buf_at), "SCHEDULED FOR: %02d:%02d // I2S SPEAKER CHIME",
             cfg->alarm.hour, cfg->alarm.minute);
    lv_label_set_text(s_lbl_alarm_time, buf_at);
    lv_obj_set_style_text_font(s_lbl_alarm_time, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_alarm_time, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(s_lbl_alarm_time, 0, 138);

    /* Test Chime Button */
    lv_obj_t *btn_chime = lv_btn_create(cont);
    lv_obj_set_size(btn_chime, 430, 32);
    lv_obj_set_pos(btn_chime, 0, 162);
    lv_obj_set_style_bg_color(btn_chime, lv_color_make(0x16, 0x24, 0x1e), 0);
    lv_obj_set_style_border_color(btn_chime, COLOR_MINT, 0);
    lv_obj_set_style_border_width(btn_chime, 1, 0);
    lv_obj_set_style_radius(btn_chime, 3, 0);
    lv_obj_add_event_cb(btn_chime, btn_test_chime_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_tc = lv_label_create(btn_chime);
    lv_label_set_text(lbl_tc, LV_SYMBOL_BELL " TEST AUDIBLE ALARM CHIME NOW");
    lv_obj_set_style_text_font(lbl_tc, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_tc, COLOR_MINT, 0);
    lv_obj_center(lbl_tc);
}
