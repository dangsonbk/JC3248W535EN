#include "gui_boot_screen.h"
#include "gui_launcher.h"
#include "weather_service.h"
#include "wifi_controller.h"
#include <stdio.h>
#include <string.h>

#define COLOR_BOOT_BLACK lv_color_make(0x00, 0x00, 0x00)
#define COLOR_BOOT_RED lv_color_make(0xff, 0x33, 0x33)
#define COLOR_BOOT_GREEN lv_color_make(0x33, 0xd1, 0x7a)
#define COLOR_BOOT_WHITE lv_color_make(0xf0, 0xf4, 0xf8)
#define COLOR_BOOT_MUTED lv_color_make(0x60, 0x72, 0x80)
#define COLOR_BOOT_CYAN lv_color_make(0x38, 0xbf, 0xd8)

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

static lv_obj_t *s_scr_boot = NULL;
static lv_obj_t *s_bar_boot = NULL;
static lv_obj_t *s_lbl_pct = NULL;
static lv_obj_t *s_lbl_console = NULL;
static lv_timer_t *s_timer_boot = NULL;

static gui_boot_done_cb_t s_on_boot_done = NULL;
static int s_boot_progress = 0; // 0 to 100
static int s_boot_hold_ticks = 0;

static char s_console_text[1024];

static void finish_boot(void) {
  if (s_timer_boot) {
    lv_timer_del(s_timer_boot);
    s_timer_boot = NULL;
  }

  gui_boot_done_cb_t cb = s_on_boot_done;
  s_on_boot_done = NULL;

  if (s_scr_boot) {
    lv_obj_del(s_scr_boot);
    s_scr_boot = NULL;
    s_bar_boot = NULL;
    s_lbl_pct = NULL;
    s_lbl_console = NULL;
  }

  if (cb) {
    cb();
  }
}

static void screen_tap_skip_cb(lv_event_t *e) {
  (void)e;
  finish_boot();
}

static void render_boot_text(int progress) {
  /* Query actual system configurations from NVS and services */
  const char *mission_id = gui_launcher_get_mission_name();

  char saved_ssid[33] = "NONE";
  char saved_pass[64];
  if (!wifi_controller_load_saved_credentials(saved_ssid, sizeof(saved_ssid),
                                              saved_pass, sizeof(saved_pass))) {
    strcpy(saved_ssid, "NONE (UNCONFIGURED)");
  }

  const weather_data_t *w = weather_service_get_data();
  float lat = (w && w->latitude != 0.0f) ? w->latitude : 21.0285f;
  float lon = (w && w->longitude != 0.0f) ? w->longitude : 105.8542f;

  char buf[1024];
  buf[0] = '\0';

  /* Header (Image 2 style: >> ROOT.ACCESS v1.0) */
  strcat(buf, "#ff3333 >> ROOT.ACCESS v1.0#\n\n");
  strcat(buf, "#ff3333 I AM NOT#\n");
  strcat(buf, "#ff3333 A USER.#\n");
  strcat(buf, "#ff3333 I AM ADMIN.#\n\n");

  if (progress >= 15) {
    strcat(buf, "#33d17a > PRIVILEGE ESCALATION: SUCCESS#\n");
  }
  if (progress >= 30) {
    char m_line[80];
    snprintf(m_line, sizeof(m_line),
             "#38bfd8 > MISSION CALLSIGN:# #f0f4f8 [ %s ]#\n", mission_id);
    strcat(buf, m_line);
  }
  if (progress >= 45) {
    char g_line[100];
    snprintf(g_line, sizeof(g_line),
             "#38bfd8 > GEO-COORDINATE: # #f0f4f8 [ %.2f N, %.2f E ]#\n", lat,
             lon);
    strcat(buf, g_line);
  }
  if (progress >= 50) {
    char w_line[80];
    snprintf(w_line, sizeof(w_line),
             "#38bfd8 > STORED WI-FI:   # #f0f4f8 [ %s ]#\n", saved_ssid);
    strcat(buf, w_line);
  }
  if (progress >= 60) {
    strcat(buf, "\n#33d17a KERNEL CONTROL       [+++++++++++++++++] 100%#\n");
  }
  if (progress >= 70) {
    strcat(buf, "#33d17a SYSTEM STORAGE       [+++++++++++++++++] 100%#\n");
  }
  if (progress >= 80) {
    strcat(buf, "#33d17a SECURITY BYPASS      [+++++++++++++++  ]  92.0%#\n\n");
  }
  if (progress >= 90) {
    strcat(buf, "#ff3333 NOTHING IS LOCKED.#\n");
    strcat(buf, "#ff3333 NOTHING IS SAFE.#\n");
  }
  if (progress >= 98) {
    strcat(buf, "#33d17a >> BOOT COMPLETE :: LAUNCHING MISSION ...#");
  }

  strncpy(s_console_text, buf, sizeof(s_console_text) - 1);
  s_console_text[sizeof(s_console_text) - 1] = '\0';

  if (s_lbl_console && lv_obj_is_valid(s_lbl_console)) {
    lv_label_set_text(s_lbl_console, s_console_text);
  }
}

static void boot_timer_cb(lv_timer_t *timer) {
  (void)timer;

  if (s_boot_progress < 100) {
    s_boot_progress += 1; // ~50 ticks * 50ms = 2.5 seconds loading
    if (s_boot_progress > 100)
      s_boot_progress = 100;

    if (s_bar_boot && lv_obj_is_valid(s_bar_boot)) {
      lv_bar_set_value(s_bar_boot, s_boot_progress, LV_ANIM_OFF);
    }

    if (s_lbl_pct && lv_obj_is_valid(s_lbl_pct)) {
      char pct_str[16];
      snprintf(pct_str, sizeof(pct_str), "%d%%", s_boot_progress);
      lv_label_set_text(s_lbl_pct, pct_str);
    }

    render_boot_text(s_boot_progress);
    return;
  }

  /* Wait 5s (100 ticks * 50ms = 5000ms) after loading final line before
   * launching HUD */
  s_boot_hold_ticks++;
  if (s_boot_hold_ticks >= 50) {
    finish_boot();
  }
}

#include "nvs_flash.h"
#include "nvs.h"

static bool s_boot_enabled = true;
static bool s_boot_nvs_loaded = false;

static void load_boot_nvs(void) {
  if (s_boot_nvs_loaded) return;
  s_boot_nvs_loaded = true;
  nvs_handle_t handle;
  if (nvs_open("sys_cfg", NVS_READONLY, &handle) == ESP_OK) {
    uint8_t val = 1;
    if (nvs_get_u8(handle, "boot_en", &val) == ESP_OK) {
      s_boot_enabled = (val != 0);
    }
    nvs_close(handle);
  }
}

bool gui_boot_screen_is_enabled(void) {
  load_boot_nvs();
  return s_boot_enabled;
}

void gui_boot_screen_set_enabled(bool enabled) {
  s_boot_enabled = enabled;
  s_boot_nvs_loaded = true;
  nvs_handle_t handle;
  if (nvs_open("sys_cfg", NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_u8(handle, "boot_en", enabled ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

void gui_boot_screen_start(gui_boot_done_cb_t on_done) {
  load_boot_nvs();
  if (!s_boot_enabled) {
    if (on_done) on_done();
    return;
  }

  s_on_boot_done = on_done;
  s_boot_progress = 0;
  s_boot_hold_ticks = 0;

  s_scr_boot = lv_obj_create(lv_scr_act());
  lv_obj_set_size(s_scr_boot, 480, 320);
  lv_obj_set_pos(s_scr_boot, 0, 0);
  lv_obj_set_style_bg_color(s_scr_boot, COLOR_BOOT_BLACK, 0);
  lv_obj_set_style_bg_opa(s_scr_boot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_scr_boot, 0, 0);
  lv_obj_set_style_pad_all(s_scr_boot, 10, 0);
  lv_obj_clear_flag(s_scr_boot, LV_OBJ_FLAG_SCROLLABLE);

  /* Tap anywhere on screen to skip boot */
  lv_obj_add_event_cb(s_scr_boot, screen_tap_skip_cb, LV_EVENT_CLICKED, NULL);

  /* Hacker Terminal Console Label (Image 2 style) */
  s_lbl_console = lv_label_create(s_scr_boot);
  lv_label_set_recolor(s_lbl_console, true);
  lv_obj_set_style_text_font(s_lbl_console, &font_orbitron_12, 0);
  lv_obj_set_pos(s_lbl_console, 8, 4);
  lv_obj_set_size(s_lbl_console, 460, 275);
  lv_obj_clear_flag(s_lbl_console, LV_OBJ_FLAG_CLICKABLE);

  /* Bottom Loading Bar */
  s_bar_boot = lv_bar_create(s_scr_boot);
  lv_obj_set_size(s_bar_boot, 390, 8);
  lv_obj_set_pos(s_bar_boot, 8, 298);
  lv_obj_set_style_bg_color(s_bar_boot, lv_color_make(0x11, 0x18, 0x14),
                            LV_PART_MAIN);
  lv_obj_set_style_radius(s_bar_boot, 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_bar_boot, COLOR_BOOT_GREEN, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_bar_boot, 2, LV_PART_INDICATOR);
  lv_bar_set_range(s_bar_boot, 0, 100);
  lv_bar_set_value(s_bar_boot, 0, LV_ANIM_OFF);

  /* Percentage Badge */
  s_lbl_pct = lv_label_create(s_scr_boot);
  lv_label_set_text(s_lbl_pct, "0%");
  lv_obj_set_style_text_font(s_lbl_pct, &font_orbitron_12, 0);
  lv_obj_set_style_text_color(s_lbl_pct, COLOR_BOOT_GREEN, 0);
  lv_obj_set_pos(s_lbl_pct, 410, 294);

  /* Initial text render */
  render_boot_text(0);

  /* Timer running every 50ms */
  s_timer_boot = lv_timer_create(boot_timer_cb, 50, NULL);
}
