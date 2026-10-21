/*
 * Futuristic Sci-Fi / Industrial Mission Control Launcher
 * Inspired by Orion N5 Mars Expedition Design
 * Target: JC3248W535EN (ESP32-S3, 480x320 Landscape, LVGL 8.4)
 * Featuring: Orbitron Cyberpunk/Futuristic Display Typography
 */

#include "gui_launcher.h"
#include "lvgl.h"
#include <stdio.h>

/* Declare Orbitron Sci-Fi / Cyberpunk Fonts */
extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;
extern const lv_font_t font_orbitron_38;

/* Visual Palette matching reference image */
#define COLOR_BG lv_color_hex(0x111413)        // Deep dark industrial slate
#define COLOR_PANEL_BG lv_color_hex(0x191D1A)  // Technical panel surface
#define COLOR_CARD_DARK lv_color_hex(0x181C1A) // Dark card surface
#define COLOR_ORANGE lv_color_hex(0xFF5A2A)    // Vivid industrial hazard orange
#define COLOR_ORANGE_DARK lv_color_hex(0x9E3416) // Muted orange text
#define COLOR_MINT lv_color_hex(0xC2DEC9)        // Pale sage mint green
#define COLOR_MINT_DARK lv_color_hex(0x2E3F35)   // Deep sage text
#define COLOR_TEXT_WHITE lv_color_hex(0xF0F5F1)  // Crisp technical off-white
#define COLOR_TEXT_MUTED lv_color_hex(0x6E8376)  // Coordinate green-gray
#define COLOR_BORDER_LINE lv_color_hex(0x28332C) // Structural border lines
#define COLOR_FOOTER_BG lv_color_hex(0x0C0E0D)   // Ultra dark footer

static lv_obj_t *lbl_status_msg = NULL;
static lv_obj_t *s_lbl_top_right = NULL;
static lv_obj_t *s_lbl_terminal = NULL;
static lv_obj_t *s_lbl_date = NULL;
static lv_obj_t *s_lbl_p2 = NULL;
static lv_obj_t *s_lbl_ticks = NULL;

static lv_obj_t *s_card0_num = NULL;
static lv_obj_t *s_card0_icon = NULL;
static lv_obj_t *s_card0_icon_mid = NULL;
static lv_obj_t *s_card0_hl = NULL;
static lv_obj_t *s_card0_cond = NULL;
static lv_obj_t *s_card0_rain = NULL;

#include "gui_weather_page.h"
#include "weather_icons.h"
#include "weather_service.h"
#include "wifi_controller.h"
#include "sys_log.h"
#include <math.h>
#include <sys/time.h>
#include <time.h>

/* Sci-Fi Typewriter Terminal State */
static char s_target_log[128] = "INITIALIZING SYSTEM...";
static uint16_t s_typed_idx = 0;
static uint32_t s_last_log_ver = 0;
static uint8_t s_cursor_tick = 0;
static uint16_t s_hold_ticks = 0;

static void typewriter_timer_cb(lv_timer_t *t) {
  (void)t;
  if (!s_lbl_terminal)
    return;

  /* Check for priority event from sys_log */
  uint32_t ver = 0;
  const char *latest = sys_log_get(&ver);
  if (ver != s_last_log_ver && latest) {
    s_last_log_ver = ver;
    strncpy(s_target_log, latest, sizeof(s_target_log) - 1);
    s_target_log[sizeof(s_target_log) - 1] = '\0';
    s_typed_idx = 0;
    s_hold_ticks = 0;
  }

  size_t target_len = strlen(s_target_log);
  if (s_typed_idx < target_len) {
    s_typed_idx++;
    s_hold_ticks = 0;
  } else {
    /* Message fully typed: hold for 5 seconds (125 ticks @ 40ms) so user can comfortably read it */
    s_hold_ticks++;
    if (s_hold_ticks >= 125) {
      s_hold_ticks = 0;
      sys_log_get_next_idle(s_target_log, sizeof(s_target_log));
      s_typed_idx = 0;
      /* Synchronize last_log_ver with current sys_log to prevent immediate overwrite */
      sys_log_get(&s_last_log_ver);
    }
  }

  s_cursor_tick++;
  bool show_cursor = ((s_cursor_tick / 6) % 2 == 0);

  char disp[140];
  snprintf(disp, sizeof(disp), "> %.*s%s", (int)s_typed_idx, s_target_log,
           show_cursor ? "_" : " ");
  lv_label_set_text(s_lbl_terminal, disp);
}

static void update_launcher_status(void) {
  time_t now = 0;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  /* 1. Top Right Clock & Battery / Wi-Fi */
  if (s_lbl_top_right) {
    wifi_status_t st = wifi_controller_get_status();
    char buf[64];
    if (st == WIFI_STATUS_CONNECTED) {
      snprintf(buf, sizeof(buf),
               "%02d:%02d  #c2dec9 " LV_SYMBOL_WIFI
               "#  " LV_SYMBOL_BATTERY_FULL,
               timeinfo.tm_hour, timeinfo.tm_min);
      lv_label_set_recolor(s_lbl_top_right, true);
    } else {
      snprintf(buf, sizeof(buf), "%02d:%02d    " LV_SYMBOL_BATTERY_FULL,
               timeinfo.tm_hour, timeinfo.tm_min);
    }
    lv_label_set_text(s_lbl_top_right, buf);
    lv_obj_align(s_lbl_top_right, LV_ALIGN_TOP_RIGHT, -12, 4);
  }

  /* 2. Current Date Label (replacing Phase 1) */
  if (s_lbl_date) {
    const char *day_names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    const char *mon_names[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                               "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    int year = timeinfo.tm_year + 1900;
    char date_buf[64];
    snprintf(date_buf, sizeof(date_buf), "%s, %02d %s %04d",
             day_names[timeinfo.tm_wday % 7], timeinfo.tm_mday,
             mon_names[timeinfo.tm_mon % 12], year);
    lv_label_set_text(s_lbl_date, date_buf);
  }

  /* 3. Timeline Month Tick Highlight */
  if (s_lbl_ticks) {
    int cur_month = timeinfo.tm_mon + 1; // 1 to 12
    char ticks_buf[220] = "";
    for (int m = 1; m <= 12; m++) {
      char seg[24];
      if (m == cur_month) {
        snprintf(seg, sizeof(seg), "#ff5a2a %02d#", m);
      } else {
        snprintf(seg, sizeof(seg), "%02d", m);
      }
      strcat(ticks_buf, seg);
      if (m < 12) {
        strcat(ticks_buf, "       ");
      }
    }
    lv_label_set_recolor(s_lbl_ticks, true);
    lv_label_set_text(s_lbl_ticks, ticks_buf);
  }

  /* 4. Year Loaded % */
  if (s_lbl_p2) {
    int year = timeinfo.tm_year + 1900;
    int is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
    int total_days = is_leap ? 366 : 365;
    int year_pct = (timeinfo.tm_yday * 100) / total_days;
    char p2_buf[48];
    snprintf(p2_buf, sizeof(p2_buf), "Year Loaded %d%%", year_pct);
    lv_label_set_text(s_lbl_p2, p2_buf);
  }

  /* 5. Weather Card 01 Real-Time Update */
  const weather_data_t *w = weather_service_get_data();
  if (w && s_card0_num) {
    bool is_f = w->is_fahrenheit;
    int cur_t = is_f ? (int)roundf((w->current_temp * 9.0f / 5.0f) + 32.0f)
                     : w->current_temp;
    int max_t =
        is_f ? (int)roundf((w->temp_max * 9.0f / 5.0f) + 32.0f) : w->temp_max;
    int min_t =
        is_f ? (int)roundf((w->temp_min * 9.0f / 5.0f) + 32.0f) : w->temp_min;

    char t_buf[16];
    snprintf(t_buf, sizeof(t_buf), "%d", cur_t);
    lv_label_set_text(s_card0_num, t_buf);

    if (s_card0_icon && w->icon) {
      lv_label_set_text(s_card0_icon, w->icon);
    }
    if (s_card0_icon_mid) {
      lv_img_set_src(s_card0_icon_mid, weather_icon_get_dsc(w->weather_code));
      lv_obj_set_style_img_recolor(s_card0_icon_mid, lv_color_hex(0x111413), 0);
      lv_obj_set_style_img_recolor_opa(s_card0_icon_mid, LV_OPA_COVER, 0);
    }

    if (s_card0_hl) {
      char hl_buf[32];
      snprintf(hl_buf, sizeof(hl_buf), "H:%do  L:%do", max_t, min_t);
      lv_label_set_text(s_card0_hl, hl_buf);
    }

    if (s_card0_cond) {
      lv_label_set_text(s_card0_cond, w->condition_str);
    }

    if (s_card0_rain) {
      char r_buf[32];
      snprintf(r_buf, sizeof(r_buf), LV_SYMBOL_BULLET " RAIN: %d%%",
               w->rain_probability);
      lv_label_set_text(s_card0_rain, r_buf);
    }
  }
}

static void launcher_status_timer_cb(lv_timer_t *t) {
  (void)t;
  update_launcher_status();
}

/* Card metadata for interactive launcher tiles */
typedef struct {
  const char *tag;
  const char *icon;
  const char *number;
  const char *title;
  const char *sub;
  const char *stat;
  bool is_orange;
  bool is_mint;
  uint32_t border_hex;
} card_meta_t;

static const card_meta_t cards_data[] = {
    {.tag = "METEO // 01",
     .icon = LV_SYMBOL_GPS,
     .number = "28",
     .title = "H:32o  L:24o",
     .sub = "PARTLY CLOUDY",
     .stat = LV_SYMBOL_BULLET " RAIN: 20%",
     .is_orange = true,
     .is_mint = false,
     .border_hex = 0},
    {.tag = "RECON // 02",
     .icon = LV_SYMBOL_GPS,
     .number = "34",
     .title = "Surface Nav",
     .sub = "RADAR MAPPING",
     .stat = LV_SYMBOL_BULLET " 75 SECTORS",
     .is_orange = false,
     .is_mint = true,
     .border_hex = 0},
    {.tag = "ORBIT // 03",
     .icon = LV_SYMBOL_WIFI,
     .number = "79",
     .title = "Orbital Data",
     .sub = "TELEMETRY LINK",
     .stat = LV_SYMBOL_BULLET " 12 ORBITS",
     .is_orange = false,
     .is_mint = false,
     .border_hex = 0xC2DEC9},
    {.tag = "FILES // 04",
     .icon = LV_SYMBOL_DIRECTORY,
     .number = "DIR",
     .title = "File Explorer",
     .sub = "MEDIA REPO",
     .stat = LV_SYMBOL_BULLET " FLASH & SD",
     .is_orange = false,
     .is_mint = false,
     .border_hex = 0x38BFD8},
    {.tag = "CONFIG // 05",
     .icon = LV_SYMBOL_SETTINGS,
     .number = "SET",
     .title = "Settings",
     .sub = "CONTROL PANEL",
     .stat = LV_SYMBOL_BULLET " 10 MODULES",
     .is_orange = false,
     .is_mint = true,
     .border_hex = 0},
    {.tag = "SYSTEM // 06",
     .icon = LV_SYMBOL_SETTINGS,
     .number = "SYS",
     .title = "Diagnostics",
     .sub = "HARDWARE BUS",
     .stat = LV_SYMBOL_BULLET " 240MHz OK",
     .is_orange = false,
     .is_mint = false,
     .border_hex = 0x526359}};

#include "gui_settings_page.h"
#include "gui_system_page.h"
#include "gui_file_explorer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static char s_mission_name[16] = "KPARK K2";
static lv_obj_t *s_lbl_mission_title = NULL;

static void load_mission_name_nvs(void) {
  nvs_handle_t handle;
  if (nvs_open("sys_cfg", NVS_READONLY, &handle) == ESP_OK) {
    size_t len = sizeof(s_mission_name);
    if (nvs_get_str(handle, "mission", s_mission_name, &len) == ESP_OK) {
      s_mission_name[sizeof(s_mission_name) - 1] = '\0';
    }
    nvs_close(handle);
  }
}

const char *gui_launcher_get_mission_name(void) {
  return s_mission_name;
}

void gui_launcher_set_mission_name(const char *name) {
  if (!name || strlen(name) == 0) name = "KPARK K2";
  strncpy(s_mission_name, name, 8);
  s_mission_name[8] = '\0';

  nvs_handle_t handle;
  if (nvs_open("sys_cfg", NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_str(handle, "mission", s_mission_name);
    nvs_commit(handle);
    nvs_close(handle);
  }
  if (s_lbl_mission_title && lv_obj_is_valid(s_lbl_mission_title)) {
    lv_label_set_text(s_lbl_mission_title, s_mission_name);
  }
}

static void card_click_event_cb(lv_event_t *e) {
  const card_meta_t *meta = (const card_meta_t *)lv_event_get_user_data(e);
  if (lbl_status_msg && meta) {
    char buf[128];
    snprintf(buf, sizeof(buf), "#ff5a2a [ ENGAGED ]:# %s // %s", meta->tag,
             meta->title);
    lv_label_set_text(lbl_status_msg, buf);
  }

  if (meta && strcmp(meta->number, "SYS") == 0) {
    gui_system_page_open();
  } else if (meta && strcmp(meta->number, "SET") == 0) {
    gui_settings_page_open();
  } else if (meta && strcmp(meta->tag, "METEO // 01") == 0) {
    gui_weather_page_open();
  } else if (meta && strcmp(meta->number, "DIR") == 0) {
    gui_file_explorer_open(lv_scr_act());
  }
}

void gui_launcher_init(void) {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);

  /* Main background */
  lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  /* ====================================================================
   * SECTION 1: TOP MISSION CONTROL HEADER (y: 0 -> 48)
   * ==================================================================== */

  /* Glowing LED Indicator dot */
  lv_obj_t *led_dot = lv_obj_create(scr);
  lv_obj_set_size(led_dot, 6, 6);
  lv_obj_set_pos(led_dot, 12, 7);
  lv_obj_set_style_radius(led_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(led_dot, COLOR_MINT, 0);
  lv_obj_set_style_border_width(led_dot, 0, 0);
  lv_obj_clear_flag(led_dot, LV_OBJ_FLAG_SCROLLABLE);

  /* Micro title on top-left */
  lv_obj_t *lbl_top_left = lv_label_create(scr);
  lv_label_set_text(lbl_top_left, "MISSION CONTROL DASHBOARD");
  lv_obj_set_style_text_font(lbl_top_left, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_top_left, COLOR_TEXT_MUTED, 0);
  lv_obj_set_pos(lbl_top_left, 24, 4);

  /* Micro status on top-right (Dynamic Clock, Wi-Fi & Battery indicator) */
  s_lbl_top_right = lv_label_create(scr);
  lv_obj_set_style_text_font(s_lbl_top_right, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(s_lbl_top_right, COLOR_TEXT_MUTED, 0);

  load_mission_name_nvs();

  /* Main title: Mission Callsign in Orbitron bold */
  s_lbl_mission_title = lv_label_create(scr);
  lv_label_set_text(s_lbl_mission_title, s_mission_name);
  lv_obj_set_style_text_font(s_lbl_mission_title, &font_orbitron_24, 0);
  lv_obj_set_style_text_color(s_lbl_mission_title, COLOR_MINT, 0);
  lv_obj_set_pos(s_lbl_mission_title, 12, 17);

  /* Sci-Fi Typewriter Terminal Log Label (replaced // Mars Expedition & D350
   * badge) */
  s_lbl_terminal = lv_label_create(scr);
  lv_obj_set_pos(s_lbl_terminal, 168, 23);
  lv_obj_set_size(s_lbl_terminal, 300, 22);
  lv_obj_set_style_text_font(s_lbl_terminal, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(s_lbl_terminal, COLOR_ORANGE, 0);
  lv_label_set_text(s_lbl_terminal, "// INITIALIZING_");

  /* Fine separator line */
  lv_obj_t *line1 = lv_obj_create(scr);
  lv_obj_set_size(line1, 456, 1);
  lv_obj_set_pos(line1, 12, 48);
  lv_obj_set_style_bg_color(line1, COLOR_BORDER_LINE, 0);
  lv_obj_set_style_border_width(line1, 0, 0);
  lv_obj_clear_flag(line1, LV_OBJ_FLAG_SCROLLABLE);

  /* ====================================================================
   * SECTION 2: TECHNICAL PHASE TIMELINE STRIP (y: 52 -> 82)
   * ==================================================================== */

  /* Phase 1 Indicator -> Current Real-time Date */
  s_lbl_date = lv_label_create(scr);
  lv_obj_set_style_text_font(s_lbl_date, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(s_lbl_date, COLOR_ORANGE, 0);
  lv_obj_set_pos(s_lbl_date, 12, 51);

  /* Phase 2 Indicator -> Year Loaded % */
  s_lbl_p2 = lv_label_create(scr);
  lv_label_set_text(s_lbl_p2, "Year Loaded 81%");
  lv_obj_set_style_text_font(s_lbl_p2, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(s_lbl_p2, COLOR_MINT, 0);
  lv_obj_set_pos(s_lbl_p2, 240, 51);

  /* Phase 1 Orange Accent Bar */
  lv_obj_t *bar_p1 = lv_obj_create(scr);
  lv_obj_set_size(bar_p1, 218, 3);
  lv_obj_set_pos(bar_p1, 12, 66);
  lv_obj_set_style_bg_color(bar_p1, COLOR_ORANGE, 0);
  lv_obj_set_style_border_width(bar_p1, 0, 0);
  lv_obj_set_style_radius(bar_p1, 0, 0);
  lv_obj_clear_flag(bar_p1, LV_OBJ_FLAG_SCROLLABLE);

  /* Phase 2 Mint Accent Bar */
  lv_obj_t *bar_p2 = lv_obj_create(scr);
  lv_obj_set_size(bar_p2, 228, 3);
  lv_obj_set_pos(bar_p2, 240, 66);
  lv_obj_set_style_bg_color(bar_p2, COLOR_MINT, 0);
  lv_obj_set_style_border_width(bar_p2, 0, 0);
  lv_obj_set_style_radius(bar_p2, 0, 0);
  lv_obj_clear_flag(bar_p2, LV_OBJ_FLAG_SCROLLABLE);

  /* Timeline coordinate tick line (Active Month highlighted in Orange) */
  s_lbl_ticks = lv_label_create(scr);
  lv_obj_set_style_text_font(s_lbl_ticks, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(s_lbl_ticks, COLOR_TEXT_MUTED, 0);
  lv_obj_set_pos(s_lbl_ticks, 20, 71);

  /* Trigger initial update */
  update_launcher_status();

  /* Fast 500ms timer for Clock and Date (drift < 1s) */
  lv_timer_create(launcher_status_timer_cb, 500, NULL);

  /* Fluid 40ms timer for Sci-Fi Terminal Typewriter effect */
  lv_timer_create(typewriter_timer_cb, 40, NULL);

  /* ====================================================================
   * SECTION 3: APP LAUNCHER MODULAR CARDS (y: 86 -> 284, height: 198)
   * ==================================================================== */

  /* Scrollable horizontal container for app cards */
  lv_obj_t *cards_cont = lv_obj_create(scr);
  lv_obj_set_size(cards_cont, 480, 202);
  lv_obj_set_pos(cards_cont, 0, 85);
  lv_obj_set_style_bg_opa(cards_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cards_cont, 0, 0);
  lv_obj_set_style_pad_hor(cards_cont, 12, 0);
  lv_obj_set_style_pad_ver(cards_cont, 4, 0);
  lv_obj_set_style_pad_column(cards_cont, 10, 0); // Spacing between cards
  lv_obj_set_flex_flow(cards_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cards_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(cards_cont, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(cards_cont, LV_SCROLLBAR_MODE_OFF);

  /* Create each modular launcher card */
  size_t num_cards = sizeof(cards_data) / sizeof(cards_data[0]);
  for (size_t i = 0; i < num_cards; i++) {
    const card_meta_t *meta = &cards_data[i];

    lv_obj_t *btn = lv_btn_create(cards_cont);
    lv_obj_set_size(btn, 134, 186);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_set_style_pad_all(btn, 8, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    /* Distinct futuristic styling */
    lv_color_t text_primary;
    lv_color_t text_secondary;

    if (meta->is_orange) {
      lv_obj_set_style_bg_color(btn, COLOR_ORANGE, 0);
      lv_obj_set_style_border_width(btn, 0, 0);
      text_primary = lv_color_hex(0x111413);
      text_secondary = COLOR_ORANGE_DARK;
    } else if (meta->is_mint) {
      lv_obj_set_style_bg_color(btn, COLOR_MINT, 0);
      lv_obj_set_style_border_width(btn, 0, 0);
      text_primary = lv_color_hex(0x111413);
      text_secondary = COLOR_MINT_DARK;
    } else {
      lv_obj_set_style_bg_color(btn, COLOR_CARD_DARK, 0);
      lv_obj_set_style_border_color(btn, lv_color_hex(meta->border_hex), 0);
      lv_obj_set_style_border_width(btn, 1, 0);
      text_primary = COLOR_TEXT_WHITE;
      text_secondary = COLOR_TEXT_MUTED;
    }

    /* Top row: Module Tag & Icon */
    lv_obj_t *lbl_tag = lv_label_create(btn);
    lv_label_set_text(lbl_tag, meta->tag);
    lv_obj_set_style_text_font(lbl_tag, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_tag, text_secondary, 0);
    lv_obj_align(lbl_tag, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_icon = lv_label_create(btn);
    lv_label_set_text(lbl_icon, meta->icon);
    lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_icon, text_primary, 0);
    lv_obj_align(lbl_icon, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* Large Hero Number in Orbitron 38 (like '95', '34', '79') */
    lv_obj_t *lbl_num = lv_label_create(btn);
    lv_label_set_text(lbl_num, meta->number);
    lv_obj_set_style_text_font(lbl_num, &font_orbitron_38, 0);
    lv_obj_set_style_text_color(lbl_num, text_primary, 0);
    lv_obj_align(lbl_num, LV_ALIGN_LEFT_MID, 0, -12);

    /* App Title */
    lv_obj_t *lbl_t = lv_label_create(btn);
    lv_label_set_text(lbl_t, meta->title);
    lv_obj_set_style_text_font(lbl_t, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(lbl_t, text_primary, 0);
    lv_obj_align(lbl_t, LV_ALIGN_BOTTOM_LEFT, 0, -32);

    /* Subtitle */
    lv_obj_t *lbl_s = lv_label_create(btn);
    lv_label_set_text(lbl_s, meta->sub);
    lv_obj_set_style_text_font(lbl_s, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_s, text_secondary, 0);
    lv_obj_align(lbl_s, LV_ALIGN_BOTTOM_LEFT, 0, -18);

    /* Bottom stat badge */
    lv_obj_t *lbl_stat = lv_label_create(btn);
    lv_label_set_text(lbl_stat, meta->stat);
    lv_obj_set_style_text_font(lbl_stat, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_stat, text_primary, 0);
    lv_obj_align(lbl_stat, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* Hook dynamic pointers for Weather Card 01 */
    if (i == 0) {
      s_card0_num = lbl_num;
      s_card0_icon = lbl_icon;
      s_card0_hl = lbl_t;
      s_card0_cond = lbl_s;
      s_card0_rain = lbl_stat;

      /* Weather status icon beside the center temperature number */
      s_card0_icon_mid = lv_img_create(btn);
      lv_img_set_src(s_card0_icon_mid, weather_icon_get_dsc(0));
      lv_obj_set_style_img_recolor(s_card0_icon_mid, text_primary, 0);
      lv_obj_set_style_img_recolor_opa(s_card0_icon_mid, LV_OPA_COVER, 0);
      lv_obj_align(s_card0_icon_mid, LV_ALIGN_LEFT_MID, 76, -12);
    }

    /* Event callback for touch interaction */
    lv_obj_add_event_cb(btn, card_click_event_cb, LV_EVENT_CLICKED,
                        (void *)meta);
  }

  /* ====================================================================
   * SECTION 4: BOTTOM TELEMETRY FOOTER (y: 290 -> 320, height: 30)
   * ==================================================================== */
  lv_obj_t *footer = lv_obj_create(scr);
  lv_obj_set_size(footer, 480, 32);
  lv_obj_set_pos(footer, 0, 288);
  lv_obj_set_style_bg_color(footer, COLOR_FOOTER_BG, 0);
  lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(footer, COLOR_BORDER_LINE, 0);
  lv_obj_set_style_border_width(footer, 1, 0);
  lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_radius(footer, 0, 0);
  lv_obj_set_style_pad_hor(footer, 12, 0);
  lv_obj_set_style_pad_ver(footer, 0, 0);
  lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

  /* Dynamic interactive status text */
  lbl_status_msg = lv_label_create(footer);
  lv_label_set_recolor(lbl_status_msg, true);
  lv_label_set_text(lbl_status_msg,
                    "#c2dec9 [ READY ]:# SWIPE / TOUCH MODULE CARDS");
  lv_obj_set_style_text_font(lbl_status_msg, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_status_msg, COLOR_TEXT_MUTED, 0);
  lv_obj_align(lbl_status_msg, LV_ALIGN_LEFT_MID, 0, 0);

  /* Hardware telemetry info on right */
  lv_obj_t *lbl_hw = lv_label_create(footer);
  lv_label_set_text(lbl_hw, "ESP32-S3 | 240MHz");
  lv_obj_set_style_text_font(lbl_hw, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(lbl_hw, COLOR_TEXT_MUTED, 0);
  lv_obj_align(lbl_hw, LV_ALIGN_RIGHT_MID, 0, 0);
}
