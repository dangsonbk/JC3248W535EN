#include "gui_clock_page.h"
#include "ui_frame.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "clock_service.h"
#include "weather_service.h"
#include "wifi_controller.h"

static const char *TAG = "GUI_CLOCK";

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

/* AMOLED Palette */
#define COLOR_AMOLED_BLACK  lv_color_hex(0x000000)
#define COLOR_ORANGE_PILL   lv_color_hex(0xFF5500)
#define COLOR_TEXT_WHITE    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_MUTED    lv_color_hex(0xA8B8C8)
#define COLOR_MINT          lv_color_hex(0x00E676)
#define COLOR_PANEL_BG      lv_color_hex(0x12161E)
#define COLOR_BORDER_LINE   lv_color_hex(0x242E3E)

static lv_obj_t *s_view_clock       = NULL;
static lv_obj_t *s_lbl_day_date     = NULL;
static lv_obj_t *s_lbl_main_time    = NULL;
static lv_obj_t *s_lbl_seconds      = NULL;
static lv_obj_t *s_lbl_alarm_badge  = NULL;
static lv_obj_t *s_lbl_world_us     = NULL;
static lv_obj_t *s_lbl_world_uk     = NULL;
static lv_obj_t *s_lbl_world_jp     = NULL;
static lv_obj_t *s_lbl_temperature  = NULL;
static lv_obj_t *s_lbl_status_icons = NULL;
static lv_obj_t *s_alarm_modal      = NULL;
static lv_obj_t *s_ringing_modal    = NULL;
static lv_timer_t *s_clock_timer    = NULL;

/* Alarm Modal Widgets */
static lv_obj_t *s_roller_h         = NULL;
static lv_obj_t *s_roller_m         = NULL;
static lv_obj_t *s_sw_modal_alarm   = NULL;

static void update_clock_display(void);
static void open_alarm_picker_modal(void);

/* --------------------------------------------------------------------
 * Clock Update Routine (every 500ms)
 * -------------------------------------------------------------------- */
static void btn_dismiss_alarm_cb(lv_event_t *e)
{
    (void)e;
    clock_service_stop_chime();
    if (s_ringing_modal) {
        lv_obj_del(s_ringing_modal);
        s_ringing_modal = NULL;
    }
}

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_view_clock) return;
    update_clock_display();

    /* Check if alarm is ringing and show dismiss modal if not open */
    if (clock_service_is_alarm_ringing()) {
        if (!s_ringing_modal) {
            s_ringing_modal = lv_obj_create(s_view_clock);
            lv_obj_set_size(s_ringing_modal, 420, 220);
            lv_obj_center(s_ringing_modal);
            lv_obj_set_style_bg_color(s_ringing_modal, lv_color_hex(0x200804), 0);
            lv_obj_set_style_border_color(s_ringing_modal, COLOR_ORANGE_PILL, 0);
            lv_obj_set_style_border_width(s_ringing_modal, 2, 0);
            lv_obj_set_style_radius(s_ringing_modal, 8, 0);

            lv_obj_t *lbl_ring = lv_label_create(s_ringing_modal);
            lv_label_set_text(lbl_ring, LV_SYMBOL_BELL " ALARM ACTIVE! // WAKE UP");
            lv_obj_set_style_text_font(lbl_ring, &font_orbitron_16, 0);
            lv_obj_set_style_text_color(lbl_ring, COLOR_ORANGE_PILL, 0);
            lv_obj_align(lbl_ring, LV_ALIGN_TOP_MID, 0, 16);

            lv_obj_t *lbl_sub = lv_label_create(s_ringing_modal);
            lv_label_set_text(lbl_sub, "SCHEDULED ALARM IS CURRENTLY RINGING.");
            lv_obj_set_style_text_font(lbl_sub, &font_orbitron_12, 0);
            lv_obj_set_style_text_color(lbl_sub, COLOR_TEXT_WHITE, 0);
            lv_obj_align(lbl_sub, LV_ALIGN_CENTER, 0, -10);

            lv_obj_t *btn_stop = lv_btn_create(s_ringing_modal);
            lv_obj_set_size(btn_stop, 240, 46);
            lv_obj_align(btn_stop, LV_ALIGN_BOTTOM_MID, 0, -16);
            lv_obj_set_style_bg_color(btn_stop, COLOR_ORANGE_PILL, 0);
            lv_obj_set_style_radius(btn_stop, 6, 0);

            lv_obj_t *lbl_btn = lv_label_create(btn_stop);
            lv_label_set_text(lbl_btn, LV_SYMBOL_OK " DISMISS ALARM");
            lv_obj_set_style_text_font(lbl_btn, &font_orbitron_16, 0);
            lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x111413), 0);
            lv_obj_center(lbl_btn);

            lv_obj_add_event_cb(btn_stop, btn_dismiss_alarm_cb, LV_EVENT_CLICKED, NULL);
        }
    } else if (s_ringing_modal) {
        lv_obj_del(s_ringing_modal);
        s_ringing_modal = NULL;
    }
}

static void update_clock_display(void)
{
    struct tm tm_loc;
    clock_service_get_local_time(&tm_loc);
    const clock_config_t *cfg = clock_service_get_config();

    /* 1. Day & Date (e.g. "Tuesday 17") */
    if (s_lbl_day_date) {
        static const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        char d_buf[48];
        snprintf(d_buf, sizeof(d_buf), "%s %02d", days[tm_loc.tm_wday % 7], tm_loc.tm_mday);
        lv_label_set_text(s_lbl_day_date, d_buf);
    }

    /* 2. Giant Digital Clock (08:26) & Seconds (43) */
    if (s_lbl_main_time && s_lbl_seconds) {
        int disp_hour = tm_loc.tm_hour;
        if (!cfg->use_24h_format) {
            if (disp_hour == 0) disp_hour = 12;
            else if (disp_hour > 12) disp_hour -= 12;
        }
        char time_buf[16];
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", disp_hour, tm_loc.tm_min);
        lv_label_set_text(s_lbl_main_time, time_buf);

        char sec_buf[8];
        snprintf(sec_buf, sizeof(sec_buf), "%02d", tm_loc.tm_sec);
        lv_label_set_text(s_lbl_seconds, sec_buf);
    }

    /* 3. Alarm Status Badge */
    if (s_lbl_alarm_badge) {
        char alm_buf[32];
        if (cfg->alarm.enabled) {
            snprintf(alm_buf, sizeof(alm_buf), "%02d:%02d [ON]", cfg->alarm.hour, cfg->alarm.minute);
        } else {
            snprintf(alm_buf, sizeof(alm_buf), "--:-- [OFF]");
        }
        lv_label_set_text(s_lbl_alarm_badge, alm_buf);
    }

    /* 4. World Clocks inside Orange Pill Bar (US, UK, JP) */
    int h_us, m_us, h_uk, m_uk, h_jp, m_jp;
    clock_service_get_world_time(cfg->world_tz_us, &h_us, &m_us);
    clock_service_get_world_time(cfg->world_tz_uk, &h_uk, &m_uk);
    clock_service_get_world_time(cfg->world_tz_jp, &h_jp, &m_jp);

    if (s_lbl_world_us) {
        char buf[32];
        snprintf(buf, sizeof(buf), "NEW YORK (US)\n%02d:%02d", h_us, m_us);
        lv_label_set_text(s_lbl_world_us, buf);
    }
    if (s_lbl_world_uk) {
        char buf[32];
        snprintf(buf, sizeof(buf), "LONDON (UK)\n%02d:%02d", h_uk, m_uk);
        lv_label_set_text(s_lbl_world_uk, buf);
    }
    if (s_lbl_world_jp) {
        char buf[32];
        snprintf(buf, sizeof(buf), "TOKYO (JP)\n%02d:%02d", h_jp, m_jp);
        lv_label_set_text(s_lbl_world_jp, buf);
    }

    /* 5. Temperature on bottom left */
    if (s_lbl_temperature) {
        const weather_data_t *w = weather_service_get_data();
        char temp_buf[16];
        if (w && w->valid) {
            snprintf(temp_buf, sizeof(temp_buf), "%d%cC", w->current_temp, 0xC2); // °C
        } else {
            snprintf(temp_buf, sizeof(temp_buf), "25%cC", 0xC2);
        }
        lv_label_set_text(s_lbl_temperature, temp_buf);
    }

    /* 6. Status Icons on bottom right */
    if (s_lbl_status_icons) {
        wifi_status_t wst = wifi_controller_get_status();
        char icon_buf[48];
        snprintf(icon_buf, sizeof(icon_buf), "%s  %s  %s",
                 (wst == WIFI_STATUS_CONNECTED) ? LV_SYMBOL_WIFI : "  ",
                 LV_SYMBOL_VOLUME_MAX,
                 cfg->alarm.enabled ? LV_SYMBOL_BELL : "");
        lv_label_set_text(s_lbl_status_icons, icon_buf);
    }
}

/* --------------------------------------------------------------------
 * Alarm Picker Modal
 * -------------------------------------------------------------------- */
static void modal_save_cb(lv_event_t *e)
{
    (void)e;
    if (!s_alarm_modal) return;

    clock_config_t cfg;
    memcpy(&cfg, clock_service_get_config(), sizeof(clock_config_t));

    if (s_roller_h) cfg.alarm.hour = (uint8_t)lv_roller_get_selected(s_roller_h);
    if (s_roller_m) cfg.alarm.minute = (uint8_t)lv_roller_get_selected(s_roller_m);
    if (s_sw_modal_alarm) cfg.alarm.enabled = lv_obj_has_state(s_sw_modal_alarm, LV_STATE_CHECKED);

    clock_service_save_config(&cfg);
    update_clock_display();

    lv_obj_del(s_alarm_modal);
    s_alarm_modal = NULL;
    s_roller_h = NULL;
    s_roller_m = NULL;
    s_sw_modal_alarm = NULL;
}

static void chime_test_stop_cb(lv_timer_t *tmr)
{
    (void)tmr;
    clock_service_stop_chime();
}

static void modal_test_chime_cb(lv_event_t *e)
{
    (void)e;
    clock_service_play_chime();
    /* Auto stop test after 2.5s */
    lv_timer_t *t = lv_timer_create(chime_test_stop_cb, 2500, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void modal_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (s_alarm_modal) {
        lv_obj_del(s_alarm_modal);
        s_alarm_modal = NULL;
        s_roller_h = NULL;
        s_roller_m = NULL;
        s_sw_modal_alarm = NULL;
    }
}

static void open_alarm_picker_modal(void)
{
    if (s_alarm_modal) return;

    const clock_config_t *cfg = clock_service_get_config();

    s_alarm_modal = lv_obj_create(s_view_clock ? s_view_clock : lv_scr_act());
    lv_obj_set_size(s_alarm_modal, 400, 260);
    lv_obj_center(s_alarm_modal);
    lv_obj_set_style_bg_color(s_alarm_modal, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(s_alarm_modal, COLOR_ORANGE_PILL, 0);
    lv_obj_set_style_border_width(s_alarm_modal, 2, 0);
    lv_obj_set_style_radius(s_alarm_modal, 8, 0);
    lv_obj_clear_flag(s_alarm_modal, LV_OBJ_FLAG_SCROLLABLE);

    /* Modal Title */
    lv_obj_t *lbl_t = lv_label_create(s_alarm_modal);
    lv_label_set_text(lbl_t, "ALARM SCHEDULER // TIME PICKER");
    lv_obj_set_style_text_font(lbl_t, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_t, COLOR_ORANGE_PILL, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 8, 6);

    /* Toggle Alarm Enable */
    lv_obj_t *lbl_en = lv_label_create(s_alarm_modal);
    lv_label_set_text(lbl_en, "ALARM ACTIVE:");
    lv_obj_set_style_text_font(lbl_en, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_en, COLOR_TEXT_WHITE, 0);
    lv_obj_align(lbl_en, LV_ALIGN_TOP_LEFT, 16, 38);

    s_sw_modal_alarm = lv_switch_create(s_alarm_modal);
    lv_obj_set_size(s_sw_modal_alarm, 46, 24);
    lv_obj_align(s_sw_modal_alarm, LV_ALIGN_TOP_RIGHT, -16, 34);
    lv_obj_set_style_bg_color(s_sw_modal_alarm, COLOR_ORANGE_PILL, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (cfg->alarm.enabled) lv_obj_add_state(s_sw_modal_alarm, LV_STATE_CHECKED);

    /* Hour & Minute Rollers */
    const char *hours_str = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";

    s_roller_h = lv_roller_create(s_alarm_modal);
    lv_roller_set_options(s_roller_h, hours_str, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller_h, 3);
    lv_roller_set_selected(s_roller_h, cfg->alarm.hour, LV_ANIM_OFF);
    lv_obj_set_size(s_roller_h, 90, 80);
    lv_obj_align(s_roller_h, LV_ALIGN_CENTER, -60, -8);
    lv_obj_set_style_text_font(s_roller_h, &font_orbitron_16, 0);
    lv_obj_set_style_bg_color(s_roller_h, lv_color_make(0x0a, 0x10, 0x1a), 0);
    lv_obj_set_style_border_color(s_roller_h, COLOR_BORDER_LINE, 0);

    lv_obj_t *lbl_colon = lv_label_create(s_alarm_modal);
    lv_label_set_text(lbl_colon, ":");
    lv_obj_set_style_text_font(lbl_colon, &font_orbitron_24, 0);
    lv_obj_set_style_text_color(lbl_colon, COLOR_ORANGE_PILL, 0);
    lv_obj_align(lbl_colon, LV_ALIGN_CENTER, 0, -8);

    s_roller_m = lv_roller_create(s_alarm_modal);
    /* Populate all 60 minutes for precision */
    char all_mins[256] = "";
    for (int m = 0; m < 60; m++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d%s", m, (m == 59) ? "" : "\n");
        strcat(all_mins, buf);
    }
    lv_roller_set_options(s_roller_m, all_mins, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_roller_m, 3);
    lv_roller_set_selected(s_roller_m, cfg->alarm.minute, LV_ANIM_OFF);
    lv_obj_set_size(s_roller_m, 90, 80);
    lv_obj_align(s_roller_m, LV_ALIGN_CENTER, 60, -8);
    lv_obj_set_style_text_font(s_roller_m, &font_orbitron_16, 0);
    lv_obj_set_style_bg_color(s_roller_m, lv_color_make(0x0a, 0x10, 0x1a), 0);
    lv_obj_set_style_border_color(s_roller_m, COLOR_BORDER_LINE, 0);

    /* Buttons: Test Sound, Cancel, Save */
    lv_obj_t *btn_test = lv_btn_create(s_alarm_modal);
    lv_obj_set_size(btn_test, 100, 34);
    lv_obj_align(btn_test, LV_ALIGN_BOTTOM_LEFT, 10, -12);
    lv_obj_set_style_bg_color(btn_test, lv_color_make(0x18, 0x22, 0x32), 0);
    lv_obj_set_style_border_color(btn_test, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(btn_test, 1, 0);
    lv_obj_add_event_cb(btn_test, modal_test_chime_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bt = lv_label_create(btn_test);
    lv_label_set_text(lbl_bt, "TEST");
    lv_obj_set_style_text_font(lbl_bt, &font_orbitron_12, 0);
    lv_obj_center(lbl_bt);

    lv_obj_t *btn_cancel = lv_btn_create(s_alarm_modal);
    lv_obj_set_size(btn_cancel, 110, 34);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_MID, 10, -12);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_make(0x18, 0x22, 0x32), 0);
    lv_obj_set_style_border_color(btn_cancel, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_add_event_cb(btn_cancel, modal_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bc = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_bc, "CANCEL");
    lv_obj_set_style_text_font(lbl_bc, &font_orbitron_12, 0);
    lv_obj_center(lbl_bc);

    lv_obj_t *btn_save = lv_btn_create(s_alarm_modal);
    lv_obj_set_size(btn_save, 130, 34);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -10, -12);
    lv_obj_set_style_bg_color(btn_save, COLOR_ORANGE_PILL, 0);
    lv_obj_add_event_cb(btn_save, modal_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_bs = lv_label_create(btn_save);
    lv_label_set_text(lbl_bs, LV_SYMBOL_OK " SET ALARM");
    lv_obj_set_style_text_font(lbl_bs, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_bs, lv_color_hex(0x111413), 0);
    lv_obj_center(lbl_bs);
}

static void on_alarm_badge_clicked_cb(lv_event_t *e)
{
    (void)e;
    open_alarm_picker_modal();
}

static void on_clock_frame_close(void)
{
    if (s_clock_timer) {
        lv_timer_del(s_clock_timer);
        s_clock_timer = NULL;
    }
    if (s_alarm_modal) {
        lv_obj_del(s_alarm_modal);
        s_alarm_modal = NULL;
    }
    if (s_ringing_modal) {
        lv_obj_del(s_ringing_modal);
        s_ringing_modal = NULL;
    }
    s_view_clock = NULL;
    s_lbl_day_date = NULL;
    s_lbl_main_time = NULL;
    s_lbl_seconds = NULL;
    s_lbl_alarm_badge = NULL;
    s_lbl_world_us = NULL;
    s_lbl_world_uk = NULL;
    s_lbl_world_jp = NULL;
    s_lbl_temperature = NULL;
    s_lbl_status_icons = NULL;
}

static void btn_clock_back_cb(lv_event_t *e)
{
    (void)e;
    ui_frame_pop();
}

/* --------------------------------------------------------------------
 * Create Clock Page View (Full-screen AMOLED Mode)
 * -------------------------------------------------------------------- */
void gui_clock_page_open(lv_obj_t *parent)
{
    (void)parent;
    if (s_view_clock) return;

    ui_frame_config_t cfg = {
        .title = "CHRONO",
        .category = "CLOCK & ALARMS",
        .mode = UI_FRAME_FULLSCREEN,
        .on_close = on_clock_frame_close,
        .user_data = NULL
    };
    ui_frame_handle_t *frame = ui_frame_push(&cfg);
    if (!frame) return;

    s_view_clock = frame->content;
    lv_obj_set_style_bg_color(s_view_clock, COLOR_AMOLED_BLACK, 0);
    lv_obj_set_style_bg_opa(s_view_clock, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_view_clock, 0, 0);
    lv_obj_clear_flag(s_view_clock, LV_OBJ_FLAG_SCROLLABLE);

    /* Top Left: Day & Date ("Tuesday 17") */
    s_lbl_day_date = lv_label_create(s_view_clock);
    lv_obj_set_style_text_font(s_lbl_day_date, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(s_lbl_day_date, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(s_lbl_day_date, 24, 16);

    /* Top Right: < LAUNCHER Return button */
    lv_obj_t *btn_back = lv_btn_create(s_view_clock);
    lv_obj_set_size(btn_back, 110, 28);
    lv_obj_align(btn_back, LV_ALIGN_TOP_RIGHT, -16, 12);
    lv_obj_set_style_bg_color(btn_back, lv_color_make(0x10, 0x16, 0x22), 0);
    lv_obj_set_style_border_color(btn_back, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 3, 0);
    lv_obj_add_event_cb(btn_back, btn_clock_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< LAUNCHER");
    lv_obj_set_style_text_font(lbl_back, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_back, COLOR_TEXT_MUTED, 0);
    lv_obj_center(lbl_back);

    /* Giant Digital Time: 08:26 (using Montserrat 48) */
    s_lbl_main_time = lv_label_create(s_view_clock);
    lv_obj_set_style_text_font(s_lbl_main_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_lbl_main_time, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(s_lbl_main_time, 24, 46);

    /* Seconds: small digits 43 aligned at upper-right of main time */
    s_lbl_seconds = lv_label_create(s_view_clock);
    lv_obj_set_style_text_font(s_lbl_seconds, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(s_lbl_seconds, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(s_lbl_seconds, 190, 52);

    /* ================================================================
     * SECTION 2: ALARM HEADER & BADGE
     * ================================================================ */
    lv_obj_t *lbl_alarm_title = lv_label_create(s_view_clock);
    lv_label_set_text(lbl_alarm_title, "Alarm");
    lv_obj_set_style_text_font(lbl_alarm_title, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(lbl_alarm_title, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_alarm_title, 24, 134);

    /* Orange Capsule Badge for Alarm (e.g. 8H 32M / 07:00 AM) */
    lv_obj_t *btn_alarm_badge = lv_btn_create(s_view_clock);
    lv_obj_set_size(btn_alarm_badge, 120, 26);
    lv_obj_align(btn_alarm_badge, LV_ALIGN_TOP_RIGHT, -24, 130);
    lv_obj_set_style_bg_color(btn_alarm_badge, COLOR_ORANGE_PILL, 0);
    lv_obj_set_style_radius(btn_alarm_badge, 13, 0); // Full capsule
    lv_obj_add_event_cb(btn_alarm_badge, on_alarm_badge_clicked_cb, LV_EVENT_CLICKED, NULL);

    s_lbl_alarm_badge = lv_label_create(btn_alarm_badge);
    lv_label_set_text(s_lbl_alarm_badge, "07:00 [ON]");
    lv_obj_set_style_text_font(s_lbl_alarm_badge, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_alarm_badge, lv_color_hex(0x111413), 0);
    lv_obj_center(s_lbl_alarm_badge);

    /* ================================================================
     * SECTION 3: VIBRANT ORANGE PILL CONTAINER (WORLD CLOCKS)
     * ================================================================ */
    lv_obj_t *pill = lv_obj_create(s_view_clock);
    lv_obj_set_size(pill, 432, 60);
    lv_obj_set_pos(pill, 24, 168);
    lv_obj_set_style_bg_color(pill, COLOR_ORANGE_PILL, 0);
    lv_obj_set_style_radius(pill, 16, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_pad_all(pill, 6, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pill, on_alarm_badge_clicked_cb, LV_EVENT_CLICKED, NULL);

    /* 3 World Cities Columns (US, UK, JP) */
    s_lbl_world_us = lv_label_create(pill);
    lv_label_set_text(s_lbl_world_us, "NEW YORK (US)\n03:26");
    lv_obj_set_style_text_font(s_lbl_world_us, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_world_us, lv_color_hex(0x111413), 0);
    lv_obj_align(s_lbl_world_us, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *div1 = lv_obj_create(pill);
    lv_obj_set_size(div1, 1, 38);
    lv_obj_set_pos(div1, 142, 6);
    lv_obj_set_style_bg_color(div1, lv_color_hex(0x9E3416), 0);
    lv_obj_set_style_border_width(div1, 0, 0);

    s_lbl_world_uk = lv_label_create(pill);
    lv_label_set_text(s_lbl_world_uk, "LONDON (UK)\n08:26");
    lv_obj_set_style_text_font(s_lbl_world_uk, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_world_uk, lv_color_hex(0x111413), 0);
    lv_obj_align(s_lbl_world_uk, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *div2 = lv_obj_create(pill);
    lv_obj_set_size(div2, 1, 38);
    lv_obj_set_pos(div2, 280, 6);
    lv_obj_set_style_bg_color(div2, lv_color_hex(0x9E3416), 0);
    lv_obj_set_style_border_width(div2, 0, 0);

    s_lbl_world_jp = lv_label_create(pill);
    lv_label_set_text(s_lbl_world_jp, "TOKYO (JP)\n17:26");
    lv_obj_set_style_text_font(s_lbl_world_jp, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_world_jp, lv_color_hex(0x111413), 0);
    lv_obj_align(s_lbl_world_jp, LV_ALIGN_RIGHT_MID, -16, 0);

    /* ================================================================
     * SECTION 4: BOTTOM ROW (TEMPERATURE & STATUS ICONS)
     * ================================================================ */
    s_lbl_temperature = lv_label_create(s_view_clock);
    lv_label_set_text(s_lbl_temperature, "25°C");
    lv_obj_set_style_text_font(s_lbl_temperature, &font_orbitron_24, 0);
    lv_obj_set_style_text_color(s_lbl_temperature, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(s_lbl_temperature, 24, 256);

    s_lbl_status_icons = lv_label_create(s_view_clock);
    lv_label_set_text(s_lbl_status_icons, LV_SYMBOL_WIFI "  " LV_SYMBOL_VOLUME_MAX "  " LV_SYMBOL_BELL);
    lv_obj_set_style_text_font(s_lbl_status_icons, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_lbl_status_icons, COLOR_ORANGE_PILL, 0);
    lv_obj_align(s_lbl_status_icons, LV_ALIGN_BOTTOM_RIGHT, -24, -24);

    update_clock_display();

    s_clock_timer = lv_timer_create(clock_timer_cb, 500, NULL);
    ESP_LOGI(TAG, "Clock application opened");
}

void gui_clock_page_close(void)
{
    if (s_view_clock) {
        ui_frame_pop();
    }
}
