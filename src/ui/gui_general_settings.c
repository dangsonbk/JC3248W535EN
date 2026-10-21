#include "gui_general_settings.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "gui_boot_screen.h"
#include "ui_frame.h"

static const char *TAG = "GUI_GEN_SET";

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

#define COLOR_PANEL_BG    lv_color_hex(0x181C1A)
#define COLOR_BORDER_LINE lv_color_hex(0x28332C)
#define COLOR_ORANGE      lv_color_hex(0xFF5A2A)
#define COLOR_MINT        lv_color_hex(0xC2DEC9)
#define COLOR_TEXT_WHITE  lv_color_hex(0xF0F5F1)
#define COLOR_TEXT_MUTED  lv_color_hex(0xA2B7AA)

static lv_obj_t *s_view_gen_set    = NULL;
static lv_obj_t *s_sw_boot         = NULL;
static lv_obj_t *s_lbl_boot_status = NULL;

static void sw_boot_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    gui_boot_screen_set_enabled(en);

    if (s_lbl_boot_status) {
        char buf[96];
        if (en) {
            snprintf(buf, sizeof(buf), "#33d17a [ ENABLED ]# CYBERPUNK DIAGNOSTIC SCREEN RUNS AT BOOT");
        } else {
            snprintf(buf, sizeof(buf), "#ff5a2a [ DISABLED ]# FAST BOOT DIRECTLY TO DASHBOARD");
        }
        lv_label_set_text(s_lbl_boot_status, buf);
    }
    ESP_LOGI(TAG, "Boot diagnostic screen set to: %d", en);
}

static void on_general_settings_close(void)
{
    s_view_gen_set = NULL;
    s_sw_boot = NULL;
    s_lbl_boot_status = NULL;
}

void gui_general_settings_open(lv_obj_t *parent)
{
    (void)parent;
    if (s_view_gen_set) return;

    ui_page_config_t cfg = {
        .title = "GENERAL",
        .breadcrumb = "SYSTEM SETTINGS // STARTUP",
        .mode = UI_FRAME_DEFAULT,
        .on_close = on_general_settings_close,
        .user_data = NULL
    };
    ui_frame_t *frame = ui_frame_push(&cfg);
    if (!frame) return;
    s_view_gen_set = frame->root;

    /* Container inside content area (460x224) */
    lv_obj_t *cont = lv_obj_create(frame->content);
    lv_obj_set_size(cont, 460, 224);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(cont, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 4, 0);
    lv_obj_set_style_pad_all(cont, 14, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Section 1: Boot Loading Screen */
    lv_obj_t *lbl_boot_title = lv_label_create(cont);
    lv_label_set_text(lbl_boot_title, ":: CYBERPUNK BOOT / LOADING SCREEN");
    lv_obj_set_style_text_font(lbl_boot_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_boot_title, COLOR_ORANGE, 0);
    lv_obj_set_pos(lbl_boot_title, 0, 4);

    bool boot_en = gui_boot_screen_is_enabled();
    s_sw_boot = lv_switch_create(cont);
    lv_obj_set_size(s_sw_boot, 48, 24);
    lv_obj_set_pos(s_sw_boot, 376, 0);
    lv_obj_set_style_bg_color(s_sw_boot, COLOR_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_ext_click_area(s_sw_boot, 10);
    if (boot_en) lv_obj_add_state(s_sw_boot, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sw_boot, sw_boot_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_boot_status = lv_label_create(cont);
    lv_label_set_recolor(s_lbl_boot_status, true);
    char buf_b[96];
    if (boot_en) {
        snprintf(buf_b, sizeof(buf_b), "#33d17a [ ENABLED ]# CYBERPUNK DIAGNOSTIC SCREEN RUNS AT BOOT");
    } else {
        snprintf(buf_b, sizeof(buf_b), "#ff5a2a [ DISABLED ]# FAST BOOT DIRECTLY TO DASHBOARD");
    }
    lv_label_set_text(s_lbl_boot_status, buf_b);
    lv_obj_set_style_text_font(s_lbl_boot_status, &font_orbitron_12, 0);
    lv_obj_set_pos(s_lbl_boot_status, 0, 36);

    /* Divider */
    lv_obj_t *sep = lv_obj_create(cont);
    lv_obj_set_size(sep, 430, 1);
    lv_obj_set_pos(sep, 0, 78);
    lv_obj_set_style_bg_color(sep, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    /* Reserved Slot for Future Settings */
    lv_obj_t *lbl_info = lv_label_create(cont);
    lv_label_set_text(lbl_info, "ADDITIONAL SYSTEM PREFERENCES WILL BE POPULATED HERE.\nSLOT RESERVED FOR SYSTEM LOCALIZATION & DEFAULTS.");
    lv_obj_set_style_text_font(lbl_info, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_info, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_info, 0, 96);
}
