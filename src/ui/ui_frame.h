#ifndef UI_FRAME_H
#define UI_FRAME_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sci-Fi Cyberpunk Color Palette */
#define UI_COLOR_BG            lv_color_hex(0x111413)
#define UI_COLOR_PANEL_BG      lv_color_hex(0x181C1A)
#define UI_COLOR_ORANGE        lv_color_hex(0xFF5A2A)
#define UI_COLOR_ORANGE_DARK   lv_color_hex(0x9E3416)
#define UI_COLOR_MINT          lv_color_hex(0xC2DEC9)
#define UI_COLOR_TEXT_WHITE    lv_color_hex(0xF0F5F1)
#define UI_COLOR_TEXT_MUTED    lv_color_hex(0xA2B7AA)
#define UI_COLOR_BORDER_LINE   lv_color_hex(0x28332C)

typedef enum {
    UI_FRAME_DEFAULT = 0,    /* Header (0..46) + Content (48..272) + Footer (276..316) */
    UI_FRAME_NO_FOOTER,      /* Header (0..46) + Content (48..316) */
    UI_FRAME_NO_HEADER,      /* Content (0..272) + Footer (276..316) */
    UI_FRAME_FULLSCREEN      /* Content (0..320 Full screen, no Header/Footer) */
} ui_frame_mode_t;

typedef struct {
    const char *title;       /* Subpage Big Title (MAX 8 CHARS, e.g. "SETTINGS", "WI-FI", "POWER") */
    const char *breadcrumb;  /* Micro title at top-left (e.g. "MISSION CONTROL DASHBOARD") */
    const char *category;    /* Optional category / breadcrumb subtitle */
    ui_frame_mode_t mode;    /* Frame layout mode */
    void (*on_close)(void);  /* Optional callback called when page is popped */
    void *user_data;
} ui_page_config_t;

typedef ui_page_config_t ui_frame_config_t;

typedef struct {
    lv_obj_t *root;          /* Root full-screen container (480x320) */
    lv_obj_t *header;        /* Header container (NULL if NO_HEADER / FULLSCREEN) */
    lv_obj_t *content;       /* Main content area for subpage widgets */
    lv_obj_t *footer;        /* Footer container (NULL if NO_FOOTER / FULLSCREEN) */
    lv_obj_t *footer_action; /* Action container on left of footer */
    lv_obj_t *btn_back;      /* Standard Back button on right of footer */
    lv_obj_t *lbl_title;     /* Big Title (max 8 chars) */
    lv_obj_t *lbl_telemetry; /* Orange telemetry string (> RAM: ... :: PSRAM: ... FREE) */
    ui_page_config_t cfg;
} ui_frame_t;

typedef ui_frame_t ui_frame_handle_t;

/**
 * @brief Initialize UI Frame manager (registers 1-second live telemetry & clock timer)
 */
void ui_frame_init(void);

/**
 * @brief Push a new page onto the Navigation Stack and return its UI Frame
 * @param cfg Configuration for the page
 * @return Pointer to the active ui_frame_t (use frame->content to mount your widgets)
 */
ui_frame_t *ui_frame_push(const ui_page_config_t *cfg);

/**
 * @brief Pop the active page from the Navigation Stack and restore previous page
 */
void ui_frame_pop(void);

/**
 * @brief Get the currently active top frame
 */
ui_frame_t *ui_frame_get_active(void);

/**
 * @brief Set custom telemetry text in header (if NULL, defaults to live RAM/PSRAM stats)
 */
void ui_frame_set_telemetry(ui_frame_t *frame, const char *fmt, ...);

/**
 * @brief Update the big title text of the current frame (max 8 characters)
 */
void ui_frame_set_title(ui_frame_t *frame, const char *title);

#ifdef __cplusplus
}
#endif

#endif // UI_FRAME_H
