/*
 * Reusable Cyberpunk Virtual Keyboard & Input Dialog Component
 * Target: JC3248W535EN (ESP32-S3, 480x320 Landscape, LVGL 8.4)
 */

#include "gui_keyboard.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* External Orbitron Fonts */
extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;

/* Cyberpunk Color Palette */
#define COLOR_BG            lv_color_hex(0x111413)
#define COLOR_PANEL_BG      lv_color_hex(0x181C1A)
#define COLOR_ORANGE        lv_color_hex(0xFF5A2A)
#define COLOR_ORANGE_GLOW   lv_color_hex(0xFFAA55)
#define COLOR_MINT          lv_color_hex(0xC2DEC9)
#define COLOR_TEXT_WHITE    lv_color_hex(0xF0F5F1)
#define COLOR_TEXT_MUTED    lv_color_hex(0x6E8376)
#define COLOR_BORDER_LINE   lv_color_hex(0x28332C)

/* Modal State */
static lv_obj_t *dialog_overlay = NULL;
static lv_obj_t *dialog_ta = NULL;
static lv_obj_t *dialog_kb = NULL;
static lv_obj_t *dialog_lbl_status = NULL;
static lv_obj_t *dialog_btn_action = NULL;
static lv_obj_t *dialog_btn_eye = NULL;

static gui_input_dialog_cb_t dialog_user_cb = NULL;
static void *dialog_user_data = NULL;

/* ====================================================================
 * CYBERPUNK KEYBOARD STYLING
 * ==================================================================== */

void gui_keyboard_apply_style(lv_obj_t *kb)
{
    if (!kb) return;

    /* Main Container Style */
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x0E1310), 0);
    lv_obj_set_style_border_color(kb, COLOR_ORANGE, 0);
    lv_obj_set_style_border_side(kb, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(kb, 1, 0);
    lv_obj_set_style_pad_all(kb, 4, 0);

    /* Key Items Style */
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x1B231F), LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(0x2D3D34), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 3, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(0xECF4EE), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, LV_PART_ITEMS);

    /* Visual Haptics: Neon Orange Flash on Press */
    lv_obj_set_style_bg_color(kb, COLOR_ORANGE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, lv_color_hex(0x000000), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(kb, COLOR_ORANGE_GLOW, LV_PART_ITEMS | LV_STATE_PRESSED);
}

lv_obj_t *gui_keyboard_create(lv_obj_t *parent)
{
    lv_obj_t *kb = lv_keyboard_create(parent);
    gui_keyboard_apply_style(kb);
    return kb;
}

/* ====================================================================
 * REUSABLE INPUT MODAL DIALOG
 * ==================================================================== */

static void trigger_dialog_submit(void)
{
    if (!dialog_ta) return;
    const char *txt = lv_textarea_get_text(dialog_ta);
    if (dialog_user_cb) {
        dialog_user_cb(txt, true, dialog_user_data);
    }
}

static void trigger_dialog_cancel(void)
{
    if (dialog_user_cb) {
        dialog_user_cb(NULL, false, dialog_user_data);
    }
    gui_input_dialog_close();
}

static void btn_dialog_action_cb(lv_event_t *e)
{
    (void)e;
    trigger_dialog_submit();
}

static void btn_dialog_cancel_cb(lv_event_t *e)
{
    (void)e;
    trigger_dialog_cancel();
}

static void btn_dialog_eye_cb(lv_event_t *e)
{
    (void)e;
    if (!dialog_ta || !dialog_btn_eye) return;
    bool is_pwd = lv_textarea_get_password_mode(dialog_ta);
    lv_textarea_set_password_mode(dialog_ta, !is_pwd);

    lv_obj_t *lbl = lv_obj_get_child(dialog_btn_eye, 0);
    if (lbl) {
        lv_label_set_text(lbl, (!is_pwd) ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
    }
}

static void dialog_kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        trigger_dialog_submit();
    } else if (code == LV_EVENT_CANCEL) {
        trigger_dialog_cancel();
    }
}

lv_obj_t *gui_input_dialog_open(const gui_input_dialog_config_t *cfg)
{
    if (!cfg) return NULL;
    if (dialog_overlay) gui_input_dialog_close();

    dialog_user_cb = cfg->cb;
    dialog_user_data = cfg->user_data;

    /* Full-screen dark translucent backdrop */
    dialog_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(dialog_overlay, 480, 320);
    lv_obj_set_pos(dialog_overlay, 0, 0);
    lv_obj_set_style_bg_color(dialog_overlay, lv_color_hex(0x080C0A), 0);
    lv_obj_set_style_bg_opa(dialog_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(dialog_overlay, 0, 0);
    lv_obj_set_style_pad_all(dialog_overlay, 0, 0);
    lv_obj_clear_flag(dialog_overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* ----------------------------------------------------------------
     * TOP DIALOG FRAME (Height: 150px)
     * ---------------------------------------------------------------- */
    lv_obj_t *dialog = lv_obj_create(dialog_overlay);
    lv_obj_set_size(dialog, 468, 150);
    lv_obj_align(dialog, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_bg_color(dialog, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dialog, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(dialog, 1, 0);
    lv_obj_set_style_radius(dialog, 3, 0);
    lv_obj_set_style_pad_all(dialog, 8, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    /* Header Tag */
    lv_obj_t *lbl_hdr = lv_label_create(dialog);
    lv_label_set_text(lbl_hdr, cfg->title ? cfg->title : "[ // SYSTEM INPUT // ]");
    lv_obj_set_style_text_font(lbl_hdr, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_hdr, COLOR_ORANGE, 0);
    lv_obj_align(lbl_hdr, LV_ALIGN_TOP_LEFT, 2, 2);

    /* Close / Cancel Button */
    lv_obj_t *btn_close = lv_btn_create(dialog);
    lv_obj_set_size(btn_close, 36, 22);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_close, COLOR_BG, 0);
    lv_obj_set_style_border_color(btn_close, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(btn_close, 1, 0);
    lv_obj_set_style_radius(btn_close, 2, 0);
    lv_obj_clear_flag(btn_close, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_close, btn_dialog_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(lbl_close, COLOR_TEXT_MUTED, 0);
    lv_obj_center(lbl_close);

    /* Target / Context Info */
    lv_obj_t *lbl_info = lv_label_create(dialog);
    lv_label_set_recolor(lbl_info, true);
    lv_label_set_text(lbl_info, cfg->target_info ? cfg->target_info : "");
    lv_obj_set_style_text_font(lbl_info, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_info, COLOR_TEXT_WHITE, 0);
    lv_obj_align(lbl_info, LV_ALIGN_TOP_LEFT, 2, 28);

    /* Input Row Container (Text Area + Eye + Action Button) */
    lv_obj_t *input_row = lv_obj_create(dialog);
    lv_obj_set_size(input_row, 452, 42);
    lv_obj_align(input_row, LV_ALIGN_TOP_LEFT, 0, 52);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_row, 0, 0);
    lv_obj_set_style_pad_all(input_row, 0, 0);
    lv_obj_clear_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);

    /* Calculate dynamic widths based on password eye presence */
    lv_coord_t ta_w = cfg->is_password ? 305 : 348;

    /* Text Area */
    dialog_ta = lv_textarea_create(input_row);
    lv_obj_set_size(dialog_ta, ta_w, 38);
    lv_obj_align(dialog_ta, LV_ALIGN_LEFT_MID, 0, 0);
    lv_textarea_set_one_line(dialog_ta, true);
    lv_textarea_set_password_mode(dialog_ta, cfg->is_password);
    if (cfg->initial_text) lv_textarea_set_text(dialog_ta, cfg->initial_text);
    if (cfg->placeholder) lv_textarea_set_placeholder_text(dialog_ta, cfg->placeholder);

    lv_obj_set_style_bg_color(dialog_ta, COLOR_BG, 0);
    lv_obj_set_style_border_color(dialog_ta, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_color(dialog_ta, COLOR_ORANGE, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(dialog_ta, 1, 0);
    lv_obj_set_style_radius(dialog_ta, 2, 0);
    lv_obj_set_style_text_color(dialog_ta, COLOR_TEXT_WHITE, 0);
    lv_obj_set_style_text_font(dialog_ta, &font_orbitron_12, 0);

    /* Password Eye Button */
    if (cfg->is_password) {
        dialog_btn_eye = lv_btn_create(input_row);
        lv_obj_set_size(dialog_btn_eye, 42, 38);
        lv_obj_align(dialog_btn_eye, LV_ALIGN_LEFT_MID, 312, 0);
        lv_obj_set_style_bg_color(dialog_btn_eye, COLOR_BG, 0);
        lv_obj_set_style_border_color(dialog_btn_eye, COLOR_BORDER_LINE, 0);
        lv_obj_set_style_border_width(dialog_btn_eye, 1, 0);
        lv_obj_set_style_radius(dialog_btn_eye, 2, 0);
        lv_obj_clear_flag(dialog_btn_eye, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(dialog_btn_eye, btn_dialog_eye_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl_eye = lv_label_create(dialog_btn_eye);
        lv_label_set_text(lbl_eye, LV_SYMBOL_EYE_OPEN);
        lv_obj_set_style_text_color(lbl_eye, COLOR_MINT, 0);
        lv_obj_center(lbl_eye);
    } else {
        dialog_btn_eye = NULL;
    }

    /* Action / Submit Button */
    dialog_btn_action = lv_btn_create(input_row);
    lv_obj_set_size(dialog_btn_action, cfg->is_password ? 92 : 98, 38);
    lv_obj_align(dialog_btn_action, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(dialog_btn_action, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(dialog_btn_action, 0, 0);
    lv_obj_set_style_radius(dialog_btn_action, 2, 0);
    lv_obj_clear_flag(dialog_btn_action, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(dialog_btn_action, btn_dialog_action_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_btn = lv_label_create(dialog_btn_action);
    lv_label_set_text(lbl_btn, cfg->action_btn_text ? cfg->action_btn_text : "SUBMIT");
    lv_obj_set_style_text_font(lbl_btn, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x111413), 0);
    lv_obj_center(lbl_btn);

    /* Status Label */
    dialog_lbl_status = lv_label_create(dialog);
    lv_label_set_recolor(dialog_lbl_status, true);
    lv_label_set_text(dialog_lbl_status, "ENTER INPUT AND PRESS SUBMIT OR KEYBOARD ENTER");
    lv_obj_set_style_text_font(dialog_lbl_status, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(dialog_lbl_status, COLOR_TEXT_MUTED, 0);
    lv_obj_align(dialog_lbl_status, LV_ALIGN_BOTTOM_LEFT, 2, -2);

    /* ----------------------------------------------------------------
     * BOTTOM CYBERPUNK VIRTUAL KEYBOARD (Height: 160px)
     * ---------------------------------------------------------------- */
    dialog_kb = gui_keyboard_create(dialog_overlay);
    lv_obj_set_size(dialog_kb, 480, 160);
    lv_obj_align(dialog_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(dialog_kb, dialog_ta);
    lv_obj_add_event_cb(dialog_kb, dialog_kb_event_cb, LV_EVENT_ALL, NULL);

    return dialog_overlay;
}

void gui_input_dialog_set_status(const char *fmt, ...)
{
    if (!dialog_lbl_status) return;
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lv_label_set_text(dialog_lbl_status, buf);
}

void gui_input_dialog_set_busy(bool busy)
{
    if (dialog_btn_action) {
        if (busy) {
            lv_obj_add_state(dialog_btn_action, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(dialog_btn_action, LV_STATE_DISABLED);
        }
    }
}

void gui_input_dialog_close(void)
{
    if (dialog_overlay) {
        lv_obj_del(dialog_overlay);
        dialog_overlay = NULL;
        dialog_ta = NULL;
        dialog_kb = NULL;
        dialog_lbl_status = NULL;
        dialog_btn_action = NULL;
        dialog_btn_eye = NULL;
        dialog_user_cb = NULL;
        dialog_user_data = NULL;
    }
}

bool gui_input_dialog_is_open(void)
{
    return (dialog_overlay != NULL);
}
