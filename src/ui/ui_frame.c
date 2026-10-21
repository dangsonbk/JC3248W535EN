#include "ui_frame.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "wifi_controller.h"

static const char *TAG = "UI_FRAME";

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

#define MAX_FRAME_STACK 8

static ui_frame_t s_stack[MAX_FRAME_STACK];
static int s_stack_top = -1;
static lv_timer_t *s_timer_telemetry = NULL;

static void update_header_telemetry(ui_frame_t *frame);
static void telemetry_timer_cb(lv_timer_t *t);
static void btn_back_click_cb(lv_event_t *e);

void ui_frame_init(void)
{
    if (s_timer_telemetry) return;
    s_timer_telemetry = lv_timer_create(telemetry_timer_cb, 1000, NULL);
    ESP_LOGI(TAG, "UI Frame manager initialized");
}

static void btn_back_click_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_SHORT_CLICKED) {
        ui_frame_pop();
    }
}

static void update_header_telemetry(ui_frame_t *frame)
{
    if (!frame || !frame->header) return;

    /* 1. Update Top-Right Clock & Battery / Wi-Fi */
    lv_obj_t *lbl_status = (lv_obj_t *)lv_obj_get_user_data(frame->header);
    if (lbl_status && lv_obj_is_valid(lbl_status)) {
        time_t now = 0;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        wifi_status_t st = wifi_controller_get_status();
        char buf_status[64];
        if (st == WIFI_STATUS_CONNECTED) {
            snprintf(buf_status, sizeof(buf_status),
                     "%02d:%02d  #c2dec9 " LV_SYMBOL_WIFI "#  " LV_SYMBOL_BATTERY_FULL,
                     timeinfo.tm_hour, timeinfo.tm_min);
            lv_label_set_recolor(lbl_status, true);
        } else {
            snprintf(buf_status, sizeof(buf_status),
                     "%02d:%02d    " LV_SYMBOL_BATTERY_FULL,
                     timeinfo.tm_hour, timeinfo.tm_min);
        }
        lv_label_set_text(lbl_status, buf_status);
    }

    /* 2. Update RAM & PSRAM live telemetry */
    if (frame->lbl_telemetry && lv_obj_is_valid(frame->lbl_telemetry)) {
        uint32_t free_ram = esp_get_free_heap_size() / 1024;
        uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;

        char buf_mem[80];
        snprintf(buf_mem, sizeof(buf_mem), "> RAM: %luK :: PSRAM: %luK FREE",
                 (unsigned long)free_ram, (unsigned long)free_psram);
        lv_label_set_text(frame->lbl_telemetry, buf_mem);
    }
}

static void telemetry_timer_cb(lv_timer_t *t)
{
    (void)t;
    ui_frame_t *curr = ui_frame_get_active();
    if (curr) {
        update_header_telemetry(curr);
    }
}

ui_frame_t *ui_frame_get_active(void)
{
    if (s_stack_top < 0 || s_stack_top >= MAX_FRAME_STACK) {
        return NULL;
    }
    return &s_stack[s_stack_top];
}

ui_frame_t *ui_frame_push(const ui_page_config_t *cfg)
{
    if (!s_timer_telemetry) {
        ui_frame_init();
    }

    if (s_stack_top >= MAX_FRAME_STACK - 1) {
        ESP_LOGE(TAG, "Navigation stack overflow (max %d)", MAX_FRAME_STACK);
        return NULL;
    }

    /* Hide the previous screen if one exists */
    if (s_stack_top >= 0) {
        ui_frame_t *prev = &s_stack[s_stack_top];
        if (prev->root && lv_obj_is_valid(prev->root)) {
            lv_obj_add_flag(prev->root, LV_OBJ_FLAG_HIDDEN);
        }
    }

    s_stack_top++;
    ui_frame_t *frame = &s_stack[s_stack_top];
    memset(frame, 0, sizeof(ui_frame_t));
    if (cfg) {
        memcpy(&frame->cfg, cfg, sizeof(ui_page_config_t));
    }

    /* 1. Root full-screen container */
    frame->root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(frame->root, 480, 320);
    lv_obj_set_pos(frame->root, 0, 0);
    lv_obj_set_style_bg_color(frame->root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(frame->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(frame->root, 0, 0);
    lv_obj_set_style_pad_all(frame->root, 0, 0);
    lv_obj_clear_flag(frame->root, LV_OBJ_FLAG_SCROLLABLE);

    bool has_header = (frame->cfg.mode == UI_FRAME_DEFAULT || frame->cfg.mode == UI_FRAME_NO_FOOTER);
    bool has_footer = (frame->cfg.mode == UI_FRAME_DEFAULT || frame->cfg.mode == UI_FRAME_NO_HEADER);

    /* 2. Main Content Container created FIRST (so Header and Footer stay on top in z-order) */
    frame->content = lv_obj_create(frame->root);
    lv_coord_t cx = 10, cy = 0, cw = 460, ch = 320;

    if (frame->cfg.mode == UI_FRAME_DEFAULT) {
        cy = 48;
        ch = 224; /* 48 to 272 */
    } else if (frame->cfg.mode == UI_FRAME_NO_FOOTER) {
        cy = 48;
        ch = 268; /* 48 to 316 */
    } else if (frame->cfg.mode == UI_FRAME_NO_HEADER) {
        cy = 4;
        ch = 268; /* 4 to 272 */
    } else { /* UI_FRAME_FULLSCREEN */
        cx = 0;
        cy = 0;
        cw = 480;
        ch = 320;
    }

    lv_obj_set_pos(frame->content, cx, cy);
    lv_obj_set_size(frame->content, cw, ch);
    lv_obj_set_style_bg_opa(frame->content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(frame->content, 0, 0);
    lv_obj_set_style_pad_all(frame->content, 0, 0);
    lv_obj_clear_flag(frame->content, LV_OBJ_FLAG_SCROLLABLE);

    /* 3. Header Component (Matching Image 1) */
    if (has_header) {
        frame->header = lv_obj_create(frame->root);
        lv_obj_set_size(frame->header, 480, 46);
        lv_obj_set_pos(frame->header, 0, 0);
        lv_obj_set_style_bg_color(frame->header, UI_COLOR_BG, 0);
        lv_obj_set_style_border_width(frame->header, 0, 0);
        lv_obj_set_style_pad_all(frame->header, 0, 0);
        lv_obj_clear_flag(frame->header, LV_OBJ_FLAG_SCROLLABLE);

        /* Glowing LED indicator dot */
        lv_obj_t *led_dot = lv_obj_create(frame->header);
        lv_obj_set_size(led_dot, 6, 6);
        lv_obj_set_pos(led_dot, 12, 7);
        lv_obj_set_style_radius(led_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(led_dot, UI_COLOR_MINT, 0);
        lv_obj_set_style_border_width(led_dot, 0, 0);
        lv_obj_clear_flag(led_dot, LV_OBJ_FLAG_SCROLLABLE);

        /* Micro title on top-left */
        lv_obj_t *lbl_crumb = lv_label_create(frame->header);
        const char *crumb = "MISSION CONTROL DASHBOARD";
        if (cfg) {
            if (cfg->breadcrumb && strlen(cfg->breadcrumb) > 0) {
                crumb = cfg->breadcrumb;
            } else if (cfg->category && strlen(cfg->category) > 0) {
                crumb = cfg->category;
            }
        }
        lv_label_set_text(lbl_crumb, crumb);
        lv_obj_set_style_text_font(lbl_crumb, &font_orbitron_12, 0);
        lv_obj_set_style_text_color(lbl_crumb, UI_COLOR_TEXT_MUTED, 0);
        lv_obj_set_pos(lbl_crumb, 24, 4);

        /* Top-Right Micro Status (Clock, Wi-Fi, Battery) */
        lv_obj_t *lbl_top_right = lv_label_create(frame->header);
        lv_obj_set_style_text_font(lbl_top_right, &font_orbitron_12, 0);
        lv_obj_set_style_text_color(lbl_top_right, UI_COLOR_TEXT_MUTED, 0);
        lv_obj_align(lbl_top_right, LV_ALIGN_TOP_RIGHT, -12, 4);
        /* Store pointer in user_data of header for live updating */
        lv_obj_set_user_data(frame->header, lbl_top_right);

        /* Big Title (Max 8 characters) */
        char title_buf[10] = "KPARK K2";
        if (cfg && cfg->title && strlen(cfg->title) > 0) {
            snprintf(title_buf, sizeof(title_buf), "%.8s", cfg->title);
        }
        frame->lbl_title = lv_label_create(frame->header);
        lv_label_set_text(frame->lbl_title, title_buf);
        lv_obj_set_style_text_font(frame->lbl_title, &font_orbitron_24, 0);
        lv_obj_set_style_text_color(frame->lbl_title, UI_COLOR_TEXT_WHITE, 0);
        lv_obj_set_pos(frame->lbl_title, 12, 14);

        /* Telemetry label next to Big Title (in orange) */
        frame->lbl_telemetry = lv_label_create(frame->header);
        lv_label_set_text(frame->lbl_telemetry, "> INITIALIZING TELEMETRY...");
        lv_obj_set_style_text_font(frame->lbl_telemetry, &font_orbitron_12, 0);
        lv_obj_set_style_text_color(frame->lbl_telemetry, UI_COLOR_ORANGE, 0);
        lv_obj_set_pos(frame->lbl_telemetry, 162, 22);

        /* Bottom Separator Line */
        lv_obj_t *line_hdr = lv_obj_create(frame->header);
        lv_obj_set_size(line_hdr, 460, 1);
        lv_obj_set_pos(line_hdr, 10, 44);
        lv_obj_set_style_bg_color(line_hdr, UI_COLOR_BORDER_LINE, 0);
        lv_obj_set_style_border_width(line_hdr, 0, 0);
        lv_obj_clear_flag(line_hdr, LV_OBJ_FLAG_SCROLLABLE);

        /* Initial telemetry draw */
        update_header_telemetry(frame);
    }

    /* 4. Footer Component */
    if (has_footer) {
        frame->footer = lv_obj_create(frame->root);
        lv_obj_set_size(frame->footer, 460, 38);
        lv_obj_set_pos(frame->footer, 10, 276);
        lv_obj_set_style_bg_color(frame->footer, UI_COLOR_PANEL_BG, 0);
        lv_obj_set_style_border_color(frame->footer, UI_COLOR_BORDER_LINE, 0);
        lv_obj_set_style_border_width(frame->footer, 1, 0);
        lv_obj_set_style_radius(frame->footer, 3, 0);
        lv_obj_set_style_pad_all(frame->footer, 2, 0);
        lv_obj_clear_flag(frame->footer, LV_OBJ_FLAG_SCROLLABLE);

        /* Left Action Container for Custom Controls (Wi-Fi Switch, stats, etc.) */
        frame->footer_action = lv_obj_create(frame->footer);
        lv_obj_set_size(frame->footer_action, 310, 32);
        lv_obj_align(frame->footer_action, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_set_style_bg_opa(frame->footer_action, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(frame->footer_action, 0, 0);
        lv_obj_set_style_pad_all(frame->footer_action, 0, 0);
        lv_obj_clear_flag(frame->footer_action, LV_OBJ_FLAG_SCROLLABLE);

        /* Standard Back Button on Right Side */
        frame->btn_back = lv_btn_create(frame->footer);
        lv_obj_set_size(frame->btn_back, 115, 32);
        lv_obj_align(frame->btn_back, LV_ALIGN_RIGHT_MID, -2, 0);
        lv_obj_set_style_bg_color(frame->btn_back, UI_COLOR_ORANGE, 0);
        lv_obj_set_style_border_width(frame->btn_back, 0, 0);
        lv_obj_set_style_radius(frame->btn_back, 3, 0);
        lv_obj_clear_flag(frame->btn_back, LV_OBJ_FLAG_SCROLLABLE);
        /* 15px extended touch area for instantaneous touch response */
        lv_obj_set_ext_click_area(frame->btn_back, 15);
        lv_obj_add_event_cb(frame->btn_back, btn_back_click_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(frame->btn_back, btn_back_click_cb, LV_EVENT_SHORT_CLICKED, NULL);

        lv_obj_t *lbl_back = lv_label_create(frame->btn_back);
        lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " BACK");
        lv_obj_set_style_text_font(lbl_back, &font_orbitron_12, 0);
        lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x111413), 0);
        lv_obj_clear_flag(lbl_back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(lbl_back);
    }

    ESP_LOGI(TAG, "Pushed page '%s' (mode=%d, depth=%d)",
             cfg && cfg->title ? cfg->title : "UNTITLED", frame->cfg.mode, s_stack_top);

    return frame;
}

void ui_frame_pop(void)
{
    if (s_stack_top < 0) return;

    ui_frame_t *curr = &s_stack[s_stack_top];
    if (curr->cfg.on_close) {
        curr->cfg.on_close();
    }

    if (curr->root && lv_obj_is_valid(curr->root)) {
        lv_obj_del(curr->root);
        curr->root = NULL;
    }

    s_stack_top--;

    /* Unhide and restore previous screen if one exists */
    if (s_stack_top >= 0) {
        ui_frame_t *prev = &s_stack[s_stack_top];
        if (prev->root && lv_obj_is_valid(prev->root)) {
            lv_obj_clear_flag(prev->root, LV_OBJ_FLAG_HIDDEN);
            update_header_telemetry(prev);
        }
        ESP_LOGI(TAG, "Popped to page '%s' (depth=%d)",
                 prev->cfg.title ? prev->cfg.title : "UNTITLED", s_stack_top);
    } else {
        ESP_LOGI(TAG, "Navigation stack empty (at Launcher)");
    }
}

void ui_frame_set_telemetry(ui_frame_t *frame, const char *fmt, ...)
{
    if (!frame || !frame->lbl_telemetry || !lv_obj_is_valid(frame->lbl_telemetry)) return;

    if (!fmt) {
        update_header_telemetry(frame);
        return;
    }

    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    lv_label_set_text(frame->lbl_telemetry, buf);
}

void ui_frame_set_title(ui_frame_t *frame, const char *title)
{
    if (!frame || !frame->lbl_title || !lv_obj_is_valid(frame->lbl_title)) return;
    char buf[10];
    snprintf(buf, sizeof(buf), "%.8s", title ? title : "TITLE");
    lv_label_set_text(frame->lbl_title, buf);
}
