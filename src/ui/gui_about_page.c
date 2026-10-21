#include "gui_about_page.h"
#include "gui_launcher.h"
#include "gui_keyboard.h"
#include "ui_frame.h"
#include <stdio.h>
#include <string.h>

#define COLOR_BG          lv_color_make(0x08, 0x0c, 0x14)
#define COLOR_PANEL_BG    lv_color_make(0x0e, 0x16, 0x26)
#define COLOR_CARD_BG     lv_color_make(0x13, 0x1e, 0x33)
#define COLOR_BORDER_LINE lv_color_make(0x1a, 0x2b, 0x44)
#define COLOR_ORANGE      lv_color_make(0xff, 0x5a, 0x2a)
#define COLOR_MINT        lv_color_make(0x33, 0xd1, 0x7a)
#define COLOR_TEXT_WHITE  lv_color_make(0xf0, 0xf4, 0xf8)
#define COLOR_TEXT_MUTED  lv_color_make(0x70, 0x82, 0x98)
#define COLOR_CYAN        lv_color_make(0x38, 0xbf, 0xd8)

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

static lv_obj_t *s_view_about = NULL;
static lv_obj_t *s_lbl_mission_val = NULL;

static void mission_rename_dialog_cb(const char *text, bool submitted, void *user_data)
{
    (void)user_data;
    if (submitted && text && strlen(text) > 0) {
        char buf[16];
        strncpy(buf, text, 8);
        buf[8] = '\0';
        gui_launcher_set_mission_name(buf);

        if (s_lbl_mission_val && lv_obj_is_valid(s_lbl_mission_val)) {
            lv_label_set_text(s_lbl_mission_val, gui_launcher_get_mission_name());
        }
    }
    gui_input_dialog_close();
}

static void btn_edit_mission_cb(lv_event_t *e)
{
    (void)e;
    gui_input_dialog_config_t cfg = {
        .title = "[ // RENAME MISSION IDENTITY // ]",
        .target_info = "CURRENT: [ KPARK K2 ]  |  LIMIT: 8 CHARACTERS",
        .initial_text = gui_launcher_get_mission_name(),
        .placeholder = "MISSION",
        .action_btn_text = "SAVE",
        .is_password = false,
        .cb = mission_rename_dialog_cb,
        .user_data = NULL
    };
    gui_input_dialog_open(&cfg);
}

static void on_about_page_close(void)
{
    s_view_about = NULL;
    s_lbl_mission_val = NULL;
}

void gui_about_page_open(lv_obj_t *parent)
{
    (void)parent;
    if (s_view_about) return;

    ui_page_config_t cfg = {
        .title = "ABOUT",
        .breadcrumb = "SYSTEM ABOUT // HARDWARE & ID",
        .mode = UI_FRAME_DEFAULT,
        .on_close = on_about_page_close,
        .user_data = NULL
    };
    ui_frame_t *frame = ui_frame_push(&cfg);
    if (!frame) return;
    s_view_about = frame->root;

    /* Container for cards inside frame->content (460 x 224) */
    lv_obj_t *cont = lv_obj_create(frame->content);
    lv_obj_set_size(cont, 460, 224);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cont, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 3, 0);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* ================================================================
     * CARD 1: MISSION IDENTITY CONFIGURATION (Max 8 characters)
     * ================================================================ */
    lv_obj_t *card_id = lv_obj_create(cont);
    lv_obj_set_size(card_id, 442, 74);
    lv_obj_set_pos(card_id, 0, 0);
    lv_obj_set_style_bg_color(card_id, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(card_id, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(card_id, 1, 0);
    lv_obj_set_style_radius(card_id, 3, 0);
    lv_obj_set_style_pad_all(card_id, 6, 0);
    lv_obj_clear_flag(card_id, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_card1_title = lv_label_create(card_id);
    lv_label_set_text(lbl_card1_title, ":: MISSION CONTROL CALLSIGN (MAX 8 CHARS)");
    lv_obj_set_style_text_font(lbl_card1_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_card1_title, COLOR_ORANGE, 0);
    lv_obj_set_pos(lbl_card1_title, 4, 2);

    /* Current Mission Name Value */
    s_lbl_mission_val = lv_label_create(card_id);
    lv_label_set_text(s_lbl_mission_val, gui_launcher_get_mission_name());
    lv_obj_set_style_text_font(s_lbl_mission_val, &font_orbitron_24, 0);
    lv_obj_set_style_text_color(s_lbl_mission_val, COLOR_MINT, 0);
    lv_obj_set_pos(s_lbl_mission_val, 8, 24);

    /* Subtext */
    lv_obj_t *lbl_sub = lv_label_create(card_id);
    lv_label_set_text(lbl_sub, "HUD TITLE & TELEMETRY IDENTIFIER");
    lv_obj_set_style_text_font(lbl_sub, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_sub, COLOR_TEXT_MUTED, 0);
    lv_obj_set_pos(lbl_sub, 8, 52);

    /* Edit Button */
    lv_obj_t *btn_edit = lv_btn_create(card_id);
    lv_obj_set_size(btn_edit, 120, 32);
    lv_obj_set_pos(btn_edit, 310, 20);
    lv_obj_set_style_bg_color(btn_edit, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_edit, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(btn_edit, 1, 0);
    lv_obj_set_style_radius(btn_edit, 2, 0);
    lv_obj_clear_flag(btn_edit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_edit, btn_edit_mission_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_btn_edit = lv_label_create(btn_edit);
    lv_label_set_text(lbl_btn_edit, LV_SYMBOL_EDIT " RENAME");
    lv_obj_set_style_text_font(lbl_btn_edit, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_btn_edit, COLOR_ORANGE, 0);
    lv_obj_center(lbl_btn_edit);

    /* ================================================================
     * CARD 2: HARDWARE ARCHITECTURE & SPECS
     * ================================================================ */
    lv_obj_t *card_spec = lv_obj_create(cont);
    lv_obj_set_size(card_spec, 442, 126);
    lv_obj_set_pos(card_spec, 0, 80);
    lv_obj_set_style_bg_color(card_spec, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(card_spec, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(card_spec, 1, 0);
    lv_obj_set_style_radius(card_spec, 3, 0);
    lv_obj_set_style_pad_all(card_spec, 6, 0);
    lv_obj_clear_flag(card_spec, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_card2_title = lv_label_create(card_spec);
    lv_label_set_text(lbl_card2_title, ":: HARDWARE ARCHITECTURE // SPECS");
    lv_obj_set_style_text_font(lbl_card2_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_card2_title, COLOR_CYAN, 0);
    lv_obj_set_pos(lbl_card2_title, 4, 2);

    lv_obj_t *lbl_specs = lv_label_create(card_spec);
    lv_label_set_recolor(lbl_specs, true);
    lv_label_set_text(lbl_specs,
        "#708298 PROCESSOR:# #f0f4f8 ESP32-S3 Dual-Core Xtensa LX7 @ 240MHz#\n"
        "#708298 MEMORY:# #f0f4f8 16MB Quad-SPI Flash | 8MB Octal-SPI PSRAM#\n"
        "#708298 DISPLAY:# #f0f4f8 3.5\" IPS 320x480 (AXS15231B Quad-SPI)#\n"
        "#708298 STORAGE:# #f0f4f8 11.87MB SPIFFS + Micro-SD (SDMMC 1-Bit)#\n"
        "#708298 FRAMEWORK:# #33d17a ESP-IDF 5.2.1 + LVGL 8.4 Mission HUD#");
    lv_obj_set_style_text_font(lbl_specs, &font_orbitron_12, 0);
    lv_obj_set_pos(lbl_specs, 4, 22);
}

