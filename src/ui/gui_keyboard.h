/*
 * Reusable Cyberpunk Virtual Keyboard & Input Dialog Component
 * Target: JC3248W535EN (ESP32-S3, 480x320 Landscape, LVGL 8.4)
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Low-level: Create a Cyberpunk-styled reusable LVGL keyboard
 * Parent: usually the active screen or a container (size ~480x160)
 */
lv_obj_t *gui_keyboard_create(lv_obj_t *parent);

/* Apply Cyberpunk style to any existing lv_keyboard */
void gui_keyboard_apply_style(lv_obj_t *kb);

/*
 * High-level: Reusable Cyberpunk Input Modal Dialog
 * Used for: Wi-Fi password, Server IP, Device rename, Token input, etc.
 */
typedef void (*gui_input_dialog_cb_t)(const char *text, bool submitted, void *user_data);

typedef struct {
    const char *title;           /* e.g. "[ // WI-FI AUTHENTICATION // ]" */
    const char *target_info;     /* e.g. "TARGET: My_SSID  |  -55 dBm" */
    const char *initial_text;    /* initial text in textarea, or NULL */
    const char *placeholder;     /* placeholder text, or NULL */
    const char *action_btn_text; /* e.g. "CONNECT" or "OK" (default: "SUBMIT") */
    bool is_password;            /* if true, mask with •••• and show eye button */
    gui_input_dialog_cb_t cb;    /* callback when user presses Submit or Cancel */
    void *user_data;             /* user pointer passed to callback */
} gui_input_dialog_config_t;

/* Open the reusable input modal dialog */
lv_obj_t *gui_input_dialog_open(const gui_input_dialog_config_t *cfg);

/* Update status label inside dialog (supports LVGL recolor syntax) */
void gui_input_dialog_set_status(const char *fmt, ...);

/* Set dialog busy state (disables action button while processing) */
void gui_input_dialog_set_busy(bool busy);

/* Close and destroy the input modal dialog */
void gui_input_dialog_close(void);

/* Check if the input dialog is currently open */
bool gui_input_dialog_is_open(void);

#ifdef __cplusplus
}
#endif
