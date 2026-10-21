/*
 * Detailed Atmospheric Telemetry & Solar Cycle Page
 * Features Real-time Weather, Animated Sync Button, Sunrise/Sunset Dome Arc,
 * and 4-Day Forecast Target: JC3248W535EN (ESP32-S3, 480x320 Landscape,
 * LVGL 8.4)
 */

#include "gui_weather_page.h"
#include "ui_frame.h"
#include "lvgl.h"
#include "weather_icons.h"
#include "weather_service.h"
#include "wifi_controller.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* External Orbitron Fonts */
extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;
extern const lv_font_t font_orbitron_38;

/* Cyberpunk Palette */
#define COLOR_BG lv_color_hex(0x111413)
#define COLOR_PANEL_BG lv_color_hex(0x181C1A)
#define COLOR_ORANGE lv_color_hex(0xFF5A2A)
#define COLOR_ORANGE_GLOW lv_color_hex(0xFFAA55)
#define COLOR_MINT lv_color_hex(0xC2DEC9)
#define COLOR_TEXT_WHITE lv_color_hex(0xF0F5F1)
#define COLOR_TEXT_MUTED lv_color_hex(0x6E8376)
#define COLOR_BORDER_LINE lv_color_hex(0x28332C)
#define COLOR_SUN_GOLD lv_color_hex(0xFFB800)

static lv_obj_t *view_weather = NULL;
static lv_obj_t *s_content_cont = NULL;
static lv_obj_t *s_lbl_hdr_sync_time = NULL;
static lv_obj_t *s_lbl_sync_icon = NULL;
static lv_obj_t *s_lbl_sync_txt = NULL;

static lv_timer_t *timer_refresh = NULL;
static bool s_sync_anim = false;
static int16_t s_sync_angle = 0;
static uint32_t s_sync_start_count = 0;
static uint16_t s_sync_timeout_ticks = 0;

static void on_weather_page_close(void) {
  if (timer_refresh) {
    lv_timer_del(timer_refresh);
    timer_refresh = NULL;
  }
  view_weather = NULL;
  s_content_cont = NULL;
  s_lbl_hdr_sync_time = NULL;
  s_lbl_sync_icon = NULL;
  s_lbl_sync_txt = NULL;
}

static void render_weather_content(void);

static void btn_refresh_click_cb(lv_event_t *e) {
  (void)e;
  weather_service_fetch_async();
  s_sync_anim = true;
  s_sync_angle = 0;
  s_sync_timeout_ticks = 0;
  s_sync_start_count = weather_service_get_update_count();
  if (s_lbl_sync_txt) {
    lv_label_set_text(s_lbl_sync_txt, "BUSY");
  }
}

static int to_display_temp(int temp_c, bool is_f) {
  if (is_f) {
    return (int)roundf((temp_c * 9.0f / 5.0f) + 32.0f);
  }
  return temp_c;
}

static lv_obj_t *create_sub_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                  lv_coord_t w, lv_coord_t h, const char *title,
                                  lv_color_t accent) {
  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_set_pos(panel, x, y);
  lv_obj_set_size(panel, w, h);
  lv_obj_set_style_bg_color(panel, COLOR_PANEL_BG, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(panel, COLOR_BORDER_LINE, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, 3, 0);
  lv_obj_set_style_pad_all(panel, 6, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  /* Header title */
  lv_obj_t *lbl = lv_label_create(panel);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_font(lbl, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl, accent, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

  /* Divider */
  lv_obj_t *line = lv_obj_create(panel);
  lv_obj_set_size(line, w - 12, 1);
  lv_obj_set_pos(line, 0, 16);
  lv_obj_set_style_bg_color(line, COLOR_BORDER_LINE, 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

  return panel;
}

static void render_weather_content(void) {
  if (!s_content_cont)
    return;
  lv_obj_clean(s_content_cont);

  const weather_data_t *w = weather_service_get_data();
  bool is_f = w->is_fahrenheit;
  const char *unit_sym = is_f ? "F" : "C";

  time_t now = 0;
  time(&now);
  struct tm ti;
  localtime_r(&now, &ti);
  int current_min_of_day = ti.tm_hour * 60 + ti.tm_min;

  /* ====================================================================
   * PANEL 1: CURRENT ATMOSPHERE (Top Left: 226x136)
   * ==================================================================== */
  char p1_title[64];
  snprintf(p1_title, sizeof(p1_title), "[01] SECTOR: %s", w->city_name);
  lv_obj_t *p1 =
      create_sub_panel(s_content_cont, 0, 0, 226, 136, p1_title, COLOR_ORANGE);

  /* Giant Current Temperature */
  lv_obj_t *lbl_cur_temp = lv_label_create(p1);
  char temp_str[32];
  snprintf(temp_str, sizeof(temp_str), "%d",
           to_display_temp(w->current_temp, is_f));
  lv_label_set_text(lbl_cur_temp, temp_str);
  lv_obj_set_style_text_font(lbl_cur_temp, &font_orbitron_38, 0);
  lv_obj_set_style_text_color(lbl_cur_temp, COLOR_ORANGE, 0);
  lv_obj_set_pos(lbl_cur_temp, 4, 18);

  /* Degree Unit Symbol */
  lv_obj_t *lbl_unit = lv_label_create(p1);
  char u_str[16];
  snprintf(u_str, sizeof(u_str), "o%s", unit_sym);
  lv_label_set_text(lbl_unit, u_str);
  lv_obj_set_style_text_font(lbl_unit, &font_orbitron_16, 0);
  lv_obj_set_style_text_color(lbl_unit, COLOR_TEXT_MUTED, 0);
  lv_obj_set_pos(lbl_unit, 72, 22);

  /* Weather Condition Icon (Hero) */
  lv_obj_t *img_w_ico = lv_img_create(p1);
  lv_img_set_src(img_w_ico, weather_icon_get_dsc(w->weather_code));
  lv_obj_set_style_img_recolor(img_w_ico,
                               weather_icon_get_color(w->weather_code), 0);
  lv_obj_set_style_img_recolor_opa(img_w_ico, LV_OPA_COVER, 0);
  lv_img_set_zoom(img_w_ico, 320);
  lv_obj_align(img_w_ico, LV_ALIGN_TOP_RIGHT, -6, 18);

  /* Weather Condition Text */
  lv_obj_t *lbl_cond = lv_label_create(p1);
  lv_label_set_text(lbl_cond, w->condition_str);
  lv_obj_set_style_text_font(lbl_cond, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_cond, COLOR_TEXT_WHITE, 0);
  lv_obj_set_pos(lbl_cond, 4, 62);

  /* Max / Min line */
  lv_obj_t *lbl_hl = lv_label_create(p1);
  char hl_str[64];
  snprintf(hl_str, sizeof(hl_str), "HIGH: %d o%s  |  LOW: %d o%s",
           to_display_temp(w->temp_max, is_f), unit_sym,
           to_display_temp(w->temp_min, is_f), unit_sym);
  lv_label_set_text(lbl_hl, hl_str);
  lv_obj_set_style_text_font(lbl_hl, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_hl, COLOR_MINT, 0);
  lv_obj_set_pos(lbl_hl, 4, 80);

  /* Atmospheric telemetry sub-data */
  lv_obj_t *lbl_substats = lv_label_create(p1);
  char sub_str[80];
  snprintf(sub_str, sizeof(sub_str), "HUMIDITY: %d%%   PRECIP: %d%%",
           w->humidity, w->rain_probability);
  lv_label_set_text(lbl_substats, sub_str);
  lv_obj_set_style_text_font(lbl_substats, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_substats, COLOR_TEXT_MUTED, 0);
  lv_obj_set_pos(lbl_substats, 4, 98);

  /* Coordinates */
  lv_obj_t *lbl_coords = lv_label_create(p1);
  char coord_str[64];
  snprintf(coord_str, sizeof(coord_str), "GEO: %.2f N, %.2f E", w->latitude,
           w->longitude);
  lv_label_set_text(lbl_coords, coord_str);
  lv_obj_set_style_text_font(lbl_coords, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_coords, COLOR_BORDER_LINE, 0);
  lv_obj_set_pos(lbl_coords, 4, 114);

  /* ====================================================================
   * PANEL 2: SOLAR CYCLE DOME ARC (Top Right: 226x136)
   * Faithfully matching reference diagram (Sunrise / Sunset Arc with Sun, Moon
   * & Directional Indicators)
   * ==================================================================== */
  lv_obj_t *p2 = create_sub_panel(s_content_cont, 234, 0, 226, 136,
                                  "[02] SOLAR CYCLE // DAYLIGHT", COLOR_MINT);

  /* Dome Arc */
  lv_obj_t *arc_solar = lv_arc_create(p2);
  lv_obj_set_size(arc_solar, 120, 120);
  lv_obj_set_pos(arc_solar, 47, 32);
  lv_arc_set_angles(arc_solar, 180, 360);
  lv_arc_set_bg_angles(arc_solar, 180, 360);
  lv_obj_set_style_arc_width(arc_solar, 2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc_solar, lv_color_hex(0x405549), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_solar, 0, LV_PART_INDICATOR);
  lv_obj_remove_style(arc_solar, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc_solar, LV_OBJ_FLAG_CLICKABLE);

  /* Calculate sun position along the 180..360 arc */
  float day_prog = 0.0f;
  int sr = w->sunrise_minutes;
  int ss = w->sunset_minutes;
  if (ss <= sr)
    ss = sr + 12 * 60;

  bool is_day = false;
  if (current_min_of_day >= sr && current_min_of_day <= ss) {
    day_prog = (float)(current_min_of_day - sr) / (float)(ss - sr);
    if (day_prog < 0.0f)
      day_prog = 0.0f;
    if (day_prog > 1.0f)
      day_prog = 1.0f;
    is_day = true;
  } else if (current_min_of_day < sr) {
    day_prog = 0.0f;
  } else {
    day_prog = 1.0f;
  }

  /* Arc geometry: Center at (107, 92), Radius = 60 */
  float angle_deg = 180.0f + day_prog * 180.0f;
  float angle_rad = angle_deg * 3.14159265f / 180.0f;
  float cx = 107.0f;
  float cy = 92.0f;
  float r = 60.0f;
  lv_coord_t sun_x = (lv_coord_t)roundf(cx + r * cosf(angle_rad)) - 7;
  lv_coord_t sun_y = (lv_coord_t)roundf(cy + r * sinf(angle_rad)) - 7;

  /* Glowing Celestial Sun Marker riding precisely along orbital curve */
  lv_obj_t *sun_dot = lv_obj_create(p2);
  lv_obj_set_size(sun_dot, 14, 14);
  lv_obj_set_pos(sun_dot, sun_x, sun_y);
  lv_obj_set_style_radius(sun_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(sun_dot, is_day ? COLOR_SUN_GOLD : lv_color_hex(0x667766), 0);
  lv_obj_set_style_bg_opa(sun_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(sun_dot, is_day ? COLOR_ORANGE : COLOR_BORDER_LINE, 0);
  lv_obj_set_style_border_width(sun_dot, 2, 0);
  lv_obj_set_style_shadow_width(sun_dot, 6, 0);
  lv_obj_set_style_shadow_color(sun_dot, COLOR_SUN_GOLD, 0);
  lv_obj_set_style_shadow_opa(sun_dot, is_day ? LV_OPA_80 : LV_OPA_30, 0);
  lv_obj_set_style_pad_all(sun_dot, 0, 0);
  lv_obj_clear_flag(sun_dot, LV_OBJ_FLAG_SCROLLABLE);

  /* Inner White Core for high visibility glow */
  lv_obj_t *sun_core = lv_obj_create(sun_dot);
  lv_obj_set_size(sun_core, 4, 4);
  lv_obj_set_style_radius(sun_core, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(sun_core, lv_color_white(), 0);
  lv_obj_set_style_border_width(sun_core, 0, 0);
  lv_obj_center(sun_core);
  lv_obj_clear_flag(sun_core, LV_OBJ_FLAG_SCROLLABLE);

  /* Center Moon Symbol */
  lv_obj_t *moon_img = lv_img_create(p2);
  lv_img_set_src(moon_img, &img_weather_moon);
  lv_obj_set_style_img_recolor(moon_img, is_day ? COLOR_MINT : COLOR_TEXT_WHITE,
                               0);
  lv_obj_set_style_img_recolor_opa(moon_img, LV_OPA_COVER, 0);
  lv_obj_set_pos(moon_img, 95, 70);

  /* Sunrise Block (Left) */
  lv_obj_t *sr_img = lv_img_create(p2);
  lv_img_set_src(sr_img, &img_weather_sunrise);
  lv_obj_set_style_img_recolor(sr_img, COLOR_MINT, 0);
  lv_obj_set_style_img_recolor_opa(sr_img, LV_OPA_COVER, 0);
  lv_obj_set_pos(sr_img, 10, 72);

  lv_obj_t *lbl_sr_time = lv_label_create(p2);
  lv_label_set_text(lbl_sr_time, w->sunrise_str);
  lv_obj_set_style_text_font(lbl_sr_time, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_sr_time, COLOR_TEXT_WHITE, 0);
  lv_obj_set_pos(lbl_sr_time, 2, 94);

  /* Sunset Block (Right) */
  lv_obj_t *ss_img = lv_img_create(p2);
  lv_img_set_src(ss_img, &img_weather_sunset);
  lv_obj_set_style_img_recolor(ss_img, COLOR_ORANGE, 0);
  lv_obj_set_style_img_recolor_opa(ss_img, LV_OPA_COVER, 0);
  lv_obj_set_pos(ss_img, 178, 72);

  lv_obj_t *lbl_ss_time = lv_label_create(p2);
  lv_label_set_text(lbl_ss_time, w->sunset_str);
  lv_obj_set_style_text_font(lbl_ss_time, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_ss_time, COLOR_TEXT_WHITE, 0);
  lv_obj_set_pos(lbl_ss_time, 164, 94);

  /* Daylight Cycle Subtitle */
  lv_obj_t *lbl_cycle_stat = lv_label_create(p2);
  lv_label_set_text(lbl_cycle_stat, is_day ? "DAYLIGHT" : "NIGHT ORBIT");
  lv_obj_set_style_text_font(lbl_cycle_stat, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_cycle_stat,
                              is_day ? COLOR_MINT : COLOR_TEXT_MUTED, 0);
  lv_obj_set_pos(lbl_cycle_stat, 64, 108);

  /* ====================================================================
   * PANEL 3: 4-DAY FORECAST MATRIX (Bottom: 460x84)
   * Real dynamic dates (TODAY, +1D, +2D, +3D) with pure white icons
   * ==================================================================== */
  lv_obj_t *p3 = create_sub_panel(s_content_cont, 0, 140, 460, 84,
                                  "[03] MULTI-DAY ATMOSPHERIC FORECAST MATRIX",
                                  COLOR_ORANGE);

  /* 4 Forecast Cards */
  for (int i = 0; i < 4; i++) {
    const weather_forecast_day_t *fc = &w->forecast[i];

    lv_obj_t *card = lv_obj_create(p3);
    lv_obj_set_size(card, 106, 56);
    lv_obj_set_pos(card, 4 + i * 111, 20);
    lv_obj_set_style_bg_color(card, COLOR_BG, 0);
    lv_obj_set_style_border_color(card, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 2, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Day Name */
    lv_obj_t *lbl_d = lv_label_create(card);
    lv_label_set_text(lbl_d, fc->day_name);
    lv_obj_set_style_text_font(lbl_d, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_d,
                                (i == 0) ? COLOR_ORANGE : COLOR_TEXT_WHITE, 0);
    lv_obj_align(lbl_d, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Weather Icon - Changed to pure white as requested */
    lv_obj_t *img_i = lv_img_create(card);
    lv_img_set_src(img_i, weather_icon_get_dsc(fc->weather_code));
    lv_obj_set_style_img_recolor(img_i, lv_color_white(), 0);
    lv_obj_set_style_img_recolor_opa(img_i, LV_OPA_COVER, 0);
    lv_obj_align(img_i, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* High / Low Temp */
    lv_obj_t *lbl_t = lv_label_create(card);
    char t_buf[32];
    snprintf(t_buf, sizeof(t_buf), "%do / %do",
             to_display_temp(fc->temp_max, is_f),
             to_display_temp(fc->temp_min, is_f));
    lv_label_set_text(lbl_t, t_buf);
    lv_obj_set_style_text_font(lbl_t, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_t, COLOR_ORANGE, 0);
    lv_obj_set_pos(lbl_t, 0, 18);

    /* Rain Probability */
    lv_obj_t *lbl_r = lv_label_create(card);
    char r_buf[32];
    snprintf(r_buf, sizeof(r_buf), "RAIN: %d%%", fc->rain_prob);
    lv_label_set_text(lbl_r, r_buf);
    lv_obj_set_style_text_font(lbl_r, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_r, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_r, 0, 36);
  }
}

static void weather_page_timer_cb(lv_timer_t *t) {
  (void)t;
  if (!view_weather)
    return;

  if (s_sync_anim) {
    s_sync_timeout_ticks++;
    s_sync_angle = (s_sync_angle + 250) % 3600;
    if (s_lbl_sync_icon) {
      lv_obj_set_style_transform_angle(s_lbl_sync_icon, s_sync_angle, 0);
    }

    bool busy = weather_service_is_busy();
    uint32_t cur_cnt = weather_service_get_update_count();

    /* Finish animation when fetch completes or after 10s timeout */
    if (!busy &&
        (cur_cnt != s_sync_start_count || s_sync_timeout_ticks > 150)) {
      s_sync_anim = false;
      if (s_lbl_sync_icon) {
        lv_obj_set_style_transform_angle(s_lbl_sync_icon, 0, 0);
      }
      if (s_lbl_sync_txt) {
        lv_label_set_text(s_lbl_sync_txt, "SYNC");
      }
      if (s_lbl_hdr_sync_time) {
        char buf[48];
        snprintf(buf, sizeof(buf), "SYNC: %s",
                 weather_service_get_last_sync_time());
        lv_label_set_text(s_lbl_hdr_sync_time, buf);
      }
      /* Re-render UI with newly fetched telemetry */
      render_weather_content();
    }
  }
}

void gui_weather_page_open(void) {
  if (view_weather)
    return;

  /* Trigger background refresh */
  weather_service_fetch_async();

  ui_page_config_t cfg = {
      .title = "WEATHER",
      .breadcrumb = "ATMOSPHERIC TELEMETRY",
      .mode = UI_FRAME_DEFAULT,
      .on_close = on_weather_page_close,
      .user_data = NULL
  };
  ui_frame_t *frame = ui_frame_push(&cfg);
  if (!frame) return;
  view_weather = frame->root;

  /* Animated Refresh Button in frame->footer_action */
  lv_obj_t *btn_ref = lv_btn_create(frame->footer_action);
  lv_obj_set_size(btn_ref, 84, 26);
  lv_obj_align(btn_ref, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_ref, COLOR_PANEL_BG, 0);
  lv_obj_set_style_border_color(btn_ref, COLOR_ORANGE, 0);
  lv_obj_set_style_border_width(btn_ref, 1, 0);
  lv_obj_set_style_radius(btn_ref, 2, 0);
  lv_obj_clear_flag(btn_ref, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn_ref, btn_refresh_click_cb, LV_EVENT_CLICKED, NULL);

  s_lbl_sync_icon = lv_label_create(btn_ref);
  lv_label_set_text(s_lbl_sync_icon, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_font(s_lbl_sync_icon, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_lbl_sync_icon, COLOR_ORANGE, 0);
  lv_obj_set_style_transform_pivot_x(s_lbl_sync_icon, 6, 0);
  lv_obj_set_style_transform_pivot_y(s_lbl_sync_icon, 6, 0);
  lv_obj_align(s_lbl_sync_icon, LV_ALIGN_LEFT_MID, -4, 0);

  s_lbl_sync_txt = lv_label_create(btn_ref);
  lv_label_set_text(s_lbl_sync_txt, "SYNC");
  lv_obj_set_style_text_font(s_lbl_sync_txt, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(s_lbl_sync_txt, COLOR_ORANGE, 0);
  lv_obj_align(s_lbl_sync_txt, LV_ALIGN_LEFT_MID, 16, 0);

  /* Last Sync Status Label beside button */
  s_lbl_hdr_sync_time = lv_label_create(frame->footer_action);
  char sync_init[48];
  snprintf(sync_init, sizeof(sync_init), "SYNC: %s",
           weather_service_get_last_sync_time());
  lv_label_set_text(s_lbl_hdr_sync_time, sync_init);
  lv_obj_set_style_text_font(s_lbl_hdr_sync_time, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(s_lbl_hdr_sync_time, COLOR_TEXT_MUTED, 0);
  lv_obj_align(s_lbl_hdr_sync_time, LV_ALIGN_LEFT_MID, 94, 0);

  /* Content container inside frame->content */
  s_content_cont = lv_obj_create(frame->content);
  lv_obj_set_size(s_content_cont, 460, 224);
  lv_obj_set_pos(s_content_cont, 0, 0);
  lv_obj_set_style_bg_opa(s_content_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_content_cont, 0, 0);
  lv_obj_set_style_pad_all(s_content_cont, 0, 0);
  lv_obj_clear_flag(s_content_cont, LV_OBJ_FLAG_SCROLLABLE);

  /* Render panels */
  render_weather_content();

  /* 60ms timer for smooth sync rotation & completion polling */
  timer_refresh = lv_timer_create(weather_page_timer_cb, 60, NULL);
}

void gui_weather_page_close(void) {
  if (view_weather) {
    ui_frame_pop();
  }
}

