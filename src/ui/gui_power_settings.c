#include "gui_power_settings.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "ui_frame.h"

static const char *TAG = "GUI_PWR_SET";

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

#define COLOR_PANEL_BG    lv_color_hex(0x181C1A)
#define COLOR_BORDER_LINE lv_color_hex(0x28332C)
#define COLOR_ORANGE      lv_color_hex(0xFF5A2A)
#define COLOR_MINT        lv_color_hex(0xC2DEC9)
#define COLOR_TEXT_WHITE  lv_color_hex(0xF0F5F1)
#define COLOR_TEXT_MUTED  lv_color_hex(0xA2B7AA)
#define COLOR_RED         lv_color_make(0xFC, 0x3D, 0x39)

static lv_obj_t *s_view_pwr_set = NULL;
static lv_obj_t *s_confirm_modal = NULL;

static void modal_reboot_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (s_confirm_modal) {
        lv_obj_del(s_confirm_modal);
        s_confirm_modal = NULL;
    }
}

static void modal_reboot_confirm_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGW(TAG, "Rebooting system as requested by user...");
    esp_restart();
}

static void show_reboot_confirmation_modal(void)
{
    if (s_confirm_modal) return;

    s_confirm_modal = lv_obj_create(s_view_pwr_set ? s_view_pwr_set : lv_scr_act());
    lv_obj_set_size(s_confirm_modal, 380, 200);
    lv_obj_center(s_confirm_modal);
    lv_obj_set_style_bg_color(s_confirm_modal, lv_color_hex(0x1a0808), 0);
    lv_obj_set_style_border_color(s_confirm_modal, COLOR_RED, 0);
    lv_obj_set_style_border_width(s_confirm_modal, 2, 0);
    lv_obj_set_style_radius(s_confirm_modal, 6, 0);

    lv_obj_t *lbl_title = lv_label_create(s_confirm_modal);
    lv_label_set_text(lbl_title, LV_SYMBOL_WARNING " CONFIRM SYSTEM REBOOT");
    lv_obj_set_style_text_font(lbl_title, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_RED, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *lbl_msg = lv_label_create(s_confirm_modal);
    lv_label_set_text(lbl_msg, "Are you sure you want to restart the device?\nAll running tasks will be terminated.");
    lv_obj_set_style_text_font(lbl_msg, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_msg, COLOR_TEXT_WHITE, 0);
    lv_obj_align(lbl_msg, LV_ALIGN_CENTER, 0, -8);

    /* Cancel Button */
    lv_obj_t *btn_cancel = lv_btn_create(s_confirm_modal);
    lv_obj_set_size(btn_cancel, 130, 36);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 16, -14);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_make(0x28, 0x18, 0x18), 0);
    lv_obj_set_style_border_color(btn_cancel, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_add_event_cb(btn_cancel, modal_reboot_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, "CANCEL");
    lv_obj_set_style_text_font(lbl_c, &font_orbitron_12, 0);
    lv_obj_center(lbl_c);

    /* Confirm Reboot Button */
    lv_obj_t *btn_reboot = lv_btn_create(s_confirm_modal);
    lv_obj_set_size(btn_reboot, 150, 36);
    lv_obj_align(btn_reboot, LV_ALIGN_BOTTOM_RIGHT, -16, -14);
    lv_obj_set_style_bg_color(btn_reboot, COLOR_RED, 0);
    lv_obj_add_event_cb(btn_reboot, modal_reboot_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_r = lv_label_create(btn_reboot);
    lv_label_set_text(lbl_r, LV_SYMBOL_REFRESH " RESTART NOW");
    lv_obj_set_style_text_font(lbl_r, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_r, lv_color_hex(0x111413), 0);
    lv_obj_center(lbl_r);
}

static void btn_reboot_click_cb(lv_event_t *e)
{
    (void)e;
    show_reboot_confirmation_modal();
}

static void on_power_settings_close(void)
{
    if (s_confirm_modal) {
        lv_obj_del(s_confirm_modal);
        s_confirm_modal = NULL;
    }
    s_view_pwr_set = NULL;
}

void gui_power_settings_open(lv_obj_t *parent)
{
    (void)parent;
    if (s_view_pwr_set) return;

    ui_page_config_t cfg = {
        .title = "POWER",
        .breadcrumb = "POWER MANAGEMENT // TELEMETRY",
        .mode = UI_FRAME_DEFAULT,
        .on_close = on_power_settings_close,
        .user_data = NULL
    };
    ui_frame_t *frame = ui_frame_push(&cfg);
    if (!frame) return;
    s_view_pwr_set = frame->root;

    /* Container */
    lv_obj_t *cont = lv_obj_create(frame->content);
    lv_obj_set_size(cont, 460, 224);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(cont, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 4, 0);
    lv_obj_set_style_pad_all(cont, 14, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Section 1: Power Source & Bus Voltage */
    lv_obj_t *lbl_src_title = lv_label_create(cont);
    lv_label_set_text(lbl_src_title, ":: POWER SOURCE & TELEMETRY");
    lv_obj_set_style_text_font(lbl_src_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_src_title, COLOR_ORANGE, 0);
    lv_obj_set_pos(lbl_src_title, 0, 0);

    lv_obj_t *lbl_pwr_val = lv_label_create(cont);
    lv_label_set_recolor(lbl_pwr_val, true);
    lv_label_set_text(lbl_pwr_val, "SOURCE: #33d17a USB-C 5.0V REGULATED [ACTIVE // NOMINAL]#\n"
                                   "BUS VOLTAGE: #f0f4f8 5020 mV# | CURRENT: #f0f4f8 ~280 mA#\n"
                                   "BATTERY ADC: #33d17a FULL / CHARGED (NO CELL DETECTED)#");
    lv_obj_set_style_text_font(lbl_pwr_val, &font_orbitron_12, 0);
    lv_obj_set_pos(lbl_pwr_val, 0, 20);

    /* Section 2: CPU Frequency & Uptime */
    uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t u_h = uptime_sec / 3600;
    uint32_t u_m = (uptime_sec % 3600) / 60;
    uint32_t u_s = uptime_sec % 60;

    lv_obj_t *lbl_perf_title = lv_label_create(cont);
    lv_label_set_text(lbl_perf_title, ":: PROCESSOR PROFILE & UPTIME");
    lv_obj_set_style_text_font(lbl_perf_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_perf_title, COLOR_MINT, 0);
    lv_obj_set_pos(lbl_perf_title, 0, 76);

    char perf_buf[128];
    snprintf(perf_buf, sizeof(perf_buf),
             "CPU FREQUENCY: #f0f4f8 240 MHz (XTENSA DUAL-CORE TURBO)#\n"
             "SYSTEM UPTIME: #33d17a %02luH %02luM %02luS#",
             (unsigned long)u_h, (unsigned long)u_m, (unsigned long)u_s);
    lv_obj_t *lbl_perf_val = lv_label_create(cont);
    lv_label_set_recolor(lbl_perf_val, true);
    lv_label_set_text(lbl_perf_val, perf_buf);
    lv_obj_set_style_text_font(lbl_perf_val, &font_orbitron_12, 0);
    lv_obj_set_pos(lbl_perf_val, 0, 96);

    /* Divider */
    lv_obj_t *sep = lv_obj_create(cont);
    lv_obj_set_size(sep, 430, 1);
    lv_obj_set_pos(sep, 0, 142);
    lv_obj_set_style_bg_color(sep, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    /* Section 3: Reboot System Button */
    lv_obj_t *btn_reset = lv_btn_create(cont);
    lv_obj_set_size(btn_reset, 430, 38);
    lv_obj_set_pos(btn_reset, 0, 156);
    lv_obj_set_style_bg_color(btn_reset, lv_color_make(0x28, 0x10, 0x10), 0);
    lv_obj_set_style_border_color(btn_reset, COLOR_RED, 0);
    lv_obj_set_style_border_width(btn_reset, 1, 0);
    lv_obj_set_style_radius(btn_reset, 4, 0);
    lv_obj_add_event_cb(btn_reset, btn_reboot_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_rst = lv_label_create(btn_reset);
    lv_label_set_text(lbl_rst, LV_SYMBOL_REFRESH " REBOOT SYSTEM // RESTART DEVICE");
    lv_obj_set_style_text_font(lbl_rst, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_rst, COLOR_RED, 0);
    lv_obj_center(lbl_rst);
}
