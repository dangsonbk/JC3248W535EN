#include "gui_file_explorer.h"
#include "ui_frame.h"
#include "gui_storage_page.h"
#include "audio_player.h"
#include "media_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "GUI_EXPLORER";

/* Cyberpunk & Phosphor Palettes */
#define COLOR_BG          lv_color_make(0x08, 0x0c, 0x14)
#define COLOR_PANEL_BG    lv_color_make(0x0e, 0x16, 0x26)
#define COLOR_CARD_BG     lv_color_make(0x13, 0x1e, 0x33)
#define COLOR_BORDER_LINE lv_color_make(0x1a, 0x2b, 0x44)
#define COLOR_ORANGE      lv_color_make(0xff, 0x5a, 0x2a)
#define COLOR_MINT        lv_color_make(0x33, 0xd1, 0x7a)
#define COLOR_TEXT_WHITE  lv_color_make(0xf0, 0xf4, 0xf8)
#define COLOR_TEXT_MUTED  lv_color_make(0x70, 0x82, 0x98)
#define COLOR_CYAN        lv_color_make(0x38, 0xbf, 0xd8)

/* Phosphor Green Palette (Matching Image 1) */
#define COLOR_PHOSPHOR_BG     lv_color_make(0x04, 0x0c, 0x08)
#define COLOR_PHOSPHOR_GREEN  lv_color_make(0x33, 0xd1, 0x7a)
#define COLOR_PHOSPHOR_DARK   lv_color_make(0x12, 0x3d, 0x24)

extern const lv_font_t font_orbitron_12;
extern const lv_font_t font_orbitron_16;
extern const lv_font_t font_orbitron_24;

typedef enum {
    EXP_TAB_INTERNAL = 0,
    EXP_TAB_SDCARD   = 1
} exp_tab_t;

typedef enum {
    FILE_TYPE_UNKNOWN = 0,
    FILE_TYPE_TEXT,
    FILE_TYPE_IMAGE,
    FILE_TYPE_AUDIO,
    FILE_TYPE_VIDEO,
    FILE_TYPE_DIR
} file_type_t;

#define MAX_EXP_FILES 64

typedef struct {
    char name[64];
    size_t size;
    file_type_t type;
} exp_file_item_t;

static exp_tab_t s_curr_tab = EXP_TAB_INTERNAL;
static char s_current_dir[512] = "/storage";
static exp_file_item_t s_files[MAX_EXP_FILES];
static int s_num_files = 0;

/* UI Widgets */
static lv_obj_t *s_view_exp         = NULL;
static lv_obj_t *s_btn_tab_int      = NULL;
static lv_obj_t *s_btn_tab_sd       = NULL;
static lv_obj_t *s_lbl_tab_int      = NULL;
static lv_obj_t *s_lbl_tab_sd       = NULL;
static lv_obj_t *s_lbl_drive_info   = NULL;
static lv_obj_t *s_file_list_cont   = NULL;
static lv_obj_t *s_modal_popup      = NULL;

/* Media Rendering Buffer in PSRAM (480 x 320 x 2 = 300 KB) */
static uint16_t *s_media_rgb_buf    = NULL;
static lv_img_dsc_t s_media_img_dsc;
static lv_obj_t *s_media_img_obj    = NULL;

/* Audio playback state */
#define AUDIO_WAVE_BARS_COUNT 32
static lv_timer_t *s_audio_timer    = NULL;
static bool s_audio_playing         = false;
static lv_obj_t *s_lbl_audio_time   = NULL;
static lv_obj_t *s_lbl_audio_meta   = NULL;
static lv_obj_t *s_bar_audio        = NULL;
static lv_obj_t *s_wave_bars[AUDIO_WAVE_BARS_COUNT] = {NULL};
static const uint8_t s_wave_base_heights[AUDIO_WAVE_BARS_COUNT] = {
    6, 14, 26, 18, 22, 12, 30, 24, 10, 16,
    28, 34, 20, 12, 18, 32, 26, 14, 8, 16,
    30, 36, 22, 12, 18, 26, 20, 14, 24, 16, 10, 6
};

/* Video playback state */
static lv_timer_t *s_video_timer    = NULL;
static bool s_video_playing         = false;
static lv_obj_t *s_lbl_video_stat   = NULL;
static lv_obj_t *s_bar_video        = NULL;

/* Media Fullscreen state */
static bool s_is_fullscreen         = false;
static lv_obj_t *s_media_box        = NULL;
static lv_obj_t *s_media_ctrls[8]   = {NULL};
static int s_media_ctrls_count      = 0;

/* Forward Declarations */
static void scan_and_render_files(void);
static void open_file_viewer(const char *filename, file_type_t type);
static void show_warning_modal_ex(const char *filename, const char *reason);
static void show_warning_modal(const char *filename);

/* --------------------------------------------------------------------
 * Extension Classifier
 * -------------------------------------------------------------------- */
static file_type_t detect_file_type(const char *filename, bool is_dir)
{
    if (is_dir) return FILE_TYPE_DIR;

    const char *dot = strrchr(filename, '.');
    if (!dot) return FILE_TYPE_UNKNOWN;

    char ext[16];
    size_t i = 0;
    while (dot[i] && i < sizeof(ext) - 1) {
        ext[i] = (char)tolower((unsigned char)dot[i]);
        i++;
    }
    ext[i] = '\0';

    if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".log") == 0 ||
        strcmp(ext, ".json") == 0 || strcmp(ext, ".c") == 0 ||
        strcmp(ext, ".h") == 0 || strcmp(ext, ".csv") == 0 ||
        strcmp(ext, ".md") == 0 || strcmp(ext, ".ini") == 0) {
        return FILE_TYPE_TEXT;
    }
    if (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".jpg") == 0 ||
        strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".png") == 0 ||
        strcmp(ext, ".bin") == 0) {
        return FILE_TYPE_IMAGE;
    }
    if (strcmp(ext, ".mp3") == 0 || strcmp(ext, ".wav") == 0) {
        return FILE_TYPE_AUDIO;
    }
    if (strcmp(ext, ".mjp") == 0 || strcmp(ext, ".mjpeg") == 0 ||
        strcmp(ext, ".avi") == 0 || strcmp(ext, ".mp4") == 0) {
        return FILE_TYPE_VIDEO;
    }

    return FILE_TYPE_UNKNOWN;
}

static const char *get_icon_for_type(file_type_t type)
{
    switch (type) {
        case FILE_TYPE_DIR:   return LV_SYMBOL_DIRECTORY;
        case FILE_TYPE_TEXT:  return LV_SYMBOL_FILE;
        case FILE_TYPE_IMAGE: return LV_SYMBOL_IMAGE;
        case FILE_TYPE_AUDIO: return LV_SYMBOL_AUDIO;
        case FILE_TYPE_VIDEO: return LV_SYMBOL_VIDEO;
        default:              return LV_SYMBOL_FILE;
    }
}

static lv_color_t get_color_for_type(file_type_t type)
{
    switch (type) {
        case FILE_TYPE_DIR:   return COLOR_ORANGE;
        case FILE_TYPE_TEXT:  return COLOR_MINT;
        case FILE_TYPE_IMAGE: return lv_color_make(0xff, 0xc1, 0x07);
        case FILE_TYPE_AUDIO: return COLOR_CYAN;
        case FILE_TYPE_VIDEO: return COLOR_ORANGE;
        default:              return COLOR_TEXT_MUTED;
    }
}

/* --------------------------------------------------------------------
 * Sort items: ".." first, then directories, then files alphabetically
 * -------------------------------------------------------------------- */
static int compare_file_items(const void *a, const void *b)
{
    const exp_file_item_t *fa = (const exp_file_item_t *)a;
    const exp_file_item_t *fb = (const exp_file_item_t *)b;

    /* ".." always comes first */
    if (strcmp(fa->name, "..") == 0) return -1;
    if (strcmp(fb->name, "..") == 0) return 1;

    /* Directories before files */
    if (fa->type == FILE_TYPE_DIR && fb->type != FILE_TYPE_DIR) return -1;
    if (fa->type != FILE_TYPE_DIR && fb->type == FILE_TYPE_DIR) return 1;

    /* Alphabetical order */
    return strcasecmp(fa->name, fb->name);
}

/* --------------------------------------------------------------------
 * Scan Directory
 * -------------------------------------------------------------------- */
static void scan_explorer_dir(const char *dir_path)
{
    s_num_files = 0;

    const char *root_dir = (s_curr_tab == EXP_TAB_INTERNAL) ? "/storage" : "/sdcard";
    bool is_subfolder = (strcmp(dir_path, root_dir) != 0);

    /* If inside a subfolder, provide [..] navigation item */
    if (is_subfolder && s_num_files < MAX_EXP_FILES) {
        exp_file_item_t *parent_item = &s_files[s_num_files++];
        strncpy(parent_item->name, "..", sizeof(parent_item->name) - 1);
        parent_item->name[sizeof(parent_item->name) - 1] = '\0';
        parent_item->size = 0;
        parent_item->type = FILE_TYPE_DIR;
    }

    /* Ensure SD card is mounted if accessing SD card tab */
    if (s_curr_tab == EXP_TAB_SDCARD && !gui_storage_is_sdcard_mounted()) {
        gui_storage_mount_sdcard();
    }

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_num_files < MAX_EXP_FILES) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Ignore OS system/hidden folders and metadata files */
        if (entry->d_name[0] == '.' ||
            strcasecmp(entry->d_name, "System Volume Information") == 0 ||
            strcasecmp(entry->d_name, "$RECYCLE.BIN") == 0) {
            continue;
        }

        exp_file_item_t *item = &s_files[s_num_files];
        strncpy(item->name, entry->d_name, sizeof(item->name) - 1);
        item->name[sizeof(item->name) - 1] = '\0';

        char fullpath[600];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) == 0) {
            bool is_d = S_ISDIR(st.st_mode);
            item->size = is_d ? 0 : st.st_size;
            item->type = detect_file_type(item->name, is_d);
        } else {
            bool is_d = (entry->d_type == DT_DIR);
            item->size = 0;
            item->type = detect_file_type(item->name, is_d);
        }
        s_num_files++;
    }
    closedir(dir);

    if (s_num_files > 1) {
        qsort(s_files, s_num_files, sizeof(exp_file_item_t), compare_file_items);
    }
}

/* --------------------------------------------------------------------
 * Asynchronous file list render callback
 * -------------------------------------------------------------------- */
static void async_scan_and_render_cb(void *param)
{
    (void)param;
    scan_and_render_files();
}

/* --------------------------------------------------------------------
 * File Click Event
 * -------------------------------------------------------------------- */
static void file_row_click_cb(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_num_files) return;

    exp_file_item_t *item = &s_files[idx];

    /* Directory Traversal */
    if (item->type == FILE_TYPE_DIR) {
        const char *root_dir = (s_curr_tab == EXP_TAB_INTERNAL) ? "/storage" : "/sdcard";
        if (strcmp(item->name, "..") == 0) {
            /* Go up one directory level */
            char *last_slash = strrchr(s_current_dir, '/');
            if (last_slash && last_slash != s_current_dir) {
                *last_slash = '\0';
                /* Ensure path doesn't go higher than root_dir */
                if (strncmp(s_current_dir, root_dir, strlen(root_dir)) != 0 || strlen(s_current_dir) < strlen(root_dir)) {
                    strncpy(s_current_dir, root_dir, sizeof(s_current_dir) - 1);
                }
            } else {
                strncpy(s_current_dir, root_dir, sizeof(s_current_dir) - 1);
            }
        } else {
            /* Enter subfolder */
            char next_dir[600];
            snprintf(next_dir, sizeof(next_dir), "%s/%s", s_current_dir, item->name);
            strncpy(s_current_dir, next_dir, sizeof(s_current_dir) - 1);
            s_current_dir[sizeof(s_current_dir) - 1] = '\0';
        }
        /* Call scan_and_render_files asynchronously to let LVGL finish row click handling safely */
        lv_async_call(async_scan_and_render_cb, NULL);
        return;
    }

    if (item->type == FILE_TYPE_UNKNOWN) {
        show_warning_modal(item->name);
    } else {
        open_file_viewer(item->name, item->type);
    }
}

/* --------------------------------------------------------------------
 * Modal Popup: 80% Screen (400 x 260)
 * -------------------------------------------------------------------- */
static void close_modal_cb(lv_event_t *e)
{
    (void)e;
    audio_player_stop();
    media_decoder_close_mjpeg();
    s_audio_playing = false;
    s_video_playing = false;
    s_media_img_obj = NULL;
    s_lbl_audio_time = NULL;
    s_lbl_audio_meta = NULL;
    s_bar_audio = NULL;
    s_lbl_video_stat = NULL;
    s_bar_video = NULL;
    s_is_fullscreen = false;
    s_media_box = NULL;
    s_media_ctrls_count = 0;
    for (int i = 0; i < 8; i++) s_media_ctrls[i] = NULL;
    for (int i = 0; i < AUDIO_WAVE_BARS_COUNT; i++) {
        s_wave_bars[i] = NULL;
    }

    if (s_audio_timer) {
        lv_timer_del(s_audio_timer);
        s_audio_timer = NULL;
    }
    if (s_video_timer) {
        lv_timer_del(s_video_timer);
        s_video_timer = NULL;
    }
    if (s_modal_popup) {
        lv_obj_del_async(s_modal_popup);
        s_modal_popup = NULL;
    }
}

static void toggle_fullscreen_cb(lv_event_t *e)
{
    (void)e;
    if (!s_modal_popup || !lv_obj_is_valid(s_modal_popup) || !s_media_box || !lv_obj_is_valid(s_media_box)) return;

    s_is_fullscreen = !s_is_fullscreen;

    if (s_is_fullscreen) {
        lv_obj_set_size(s_modal_popup, 480, 320);
        lv_obj_set_pos(s_modal_popup, 0, 0);
        lv_obj_set_style_border_width(s_modal_popup, 0, 0);
        lv_obj_set_style_radius(s_modal_popup, 0, 0);
        lv_obj_set_style_bg_color(s_modal_popup, lv_color_black(), 0);

        lv_obj_set_size(s_media_box, 480, 320);
        lv_obj_set_pos(s_media_box, 0, 0);
        lv_obj_set_style_border_width(s_media_box, 0, 0);
        lv_obj_set_style_radius(s_media_box, 0, 0);

        for (int i = 0; i < s_media_ctrls_count; i++) {
            if (s_media_ctrls[i] && lv_obj_is_valid(s_media_ctrls[i])) {
                lv_obj_add_flag(s_media_ctrls[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        lv_obj_set_size(s_modal_popup, 400, 260);
        lv_obj_center(s_modal_popup);
        lv_obj_set_style_border_width(s_modal_popup, 2, 0);
        lv_obj_set_style_radius(s_modal_popup, 6, 0);
        lv_obj_set_style_bg_color(s_modal_popup, COLOR_PANEL_BG, 0);

        lv_obj_set_size(s_media_box, 360, 150);
        lv_obj_align(s_media_box, LV_ALIGN_TOP_MID, 0, 28);
        lv_obj_set_style_border_width(s_media_box, 1, 0);
        lv_obj_set_style_radius(s_media_box, 3, 0);

        for (int i = 0; i < s_media_ctrls_count; i++) {
            if (s_media_ctrls[i] && lv_obj_is_valid(s_media_ctrls[i])) {
                lv_obj_clear_flag(s_media_ctrls[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (s_media_img_obj && lv_obj_is_valid(s_media_img_obj)) {
        lv_obj_center(s_media_img_obj);
    }
}

/* 1. TEXT VIEWER (Styled exactly matching Image 1 - Phosphor Green Terminal) */
static void open_text_viewer_modal(const char *filename, const char *fullpath)
{
    /* Read actual file contents (up to 1024 bytes) without stack allocation */
    static char s_txt_content[1024];
    memset(s_txt_content, 0, sizeof(s_txt_content));

    FILE *f = fopen(fullpath, "r");
    if (!f) {
        show_warning_modal_ex(filename, "Cannot open file stream.\nVerify storage media is mounted.");
        return;
    }

    size_t r = fread(s_txt_content, 1, sizeof(s_txt_content) - 1, f);
    s_txt_content[r] = '\0';
    fclose(f);

    /* Check for unsupported / strange characters (outside ASCII 0x20..0x7E, \r, \n, \t) */
    bool has_strange_chars = false;
    for (size_t i = 0; i < r; i++) {
        unsigned char c = (unsigned char)s_txt_content[i];
        if (c >= 0x80 || (c < 0x20 && c != '\n' && c != '\r' && c != '\t')) {
            has_strange_chars = true;
            break;
        }
    }

    if (has_strange_chars) {
        show_warning_modal_ex(filename, "File contains unsupported characters (non-ASCII / binary).\nCannot display text without font corruption.");
        return;
    }

    if (s_audio_timer) {
        lv_timer_del(s_audio_timer);
        s_audio_timer = NULL;
    }
    if (s_video_timer) {
        lv_timer_del(s_video_timer);
        s_video_timer = NULL;
    }
    if (s_modal_popup && lv_obj_is_valid(s_modal_popup)) {
        lv_obj_del(s_modal_popup);
        s_modal_popup = NULL;
    }

    lv_obj_t *parent = s_view_exp ? s_view_exp : lv_scr_act();
    s_modal_popup = lv_obj_create(parent);
    lv_obj_set_size(s_modal_popup, 400, 260);
    lv_obj_center(s_modal_popup);
    lv_obj_set_style_bg_color(s_modal_popup, COLOR_PHOSPHOR_BG, 0);
    lv_obj_set_style_bg_opa(s_modal_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_modal_popup, COLOR_PHOSPHOR_GREEN, 0);
    lv_obj_set_style_border_width(s_modal_popup, 2, 0);
    lv_obj_set_style_radius(s_modal_popup, 8, 0);
    lv_obj_set_style_pad_all(s_modal_popup, 8, 0);
    lv_obj_clear_flag(s_modal_popup, LV_OBJ_FLAG_SCROLLABLE);

    /* Header matching Image 1: Gear Icon + Title */
    lv_obj_t *lbl_gear = lv_label_create(s_modal_popup);
    lv_label_set_text(lbl_gear, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(lbl_gear, COLOR_PHOSPHOR_GREEN, 0);
    lv_obj_align(lbl_gear, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *lbl_top = lv_label_create(s_modal_popup);
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "File Stream: %s", filename);
    lv_label_set_text(lbl_top, title_buf);
    lv_obj_set_style_text_font(lbl_top, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(lbl_top, COLOR_PHOSPHOR_GREEN, 0);
    lv_obj_align(lbl_top, LV_ALIGN_TOP_MID, 0, 24);

    /* Simulated Progress Header Bar like Image 1 */
    lv_obj_t *pbar = lv_bar_create(s_modal_popup);
    lv_obj_set_size(pbar, 340, 8);
    lv_obj_align(pbar, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(pbar, COLOR_PHOSPHOR_DARK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pbar, COLOR_PHOSPHOR_GREEN, LV_PART_INDICATOR);
    lv_bar_set_value(pbar, 100, LV_ANIM_OFF);

    /* Subtext "Please Wait... / Stream Ready" */
    lv_obj_t *lbl_sub = lv_label_create(s_modal_popup);
    lv_label_set_text(lbl_sub, "STREAM BUFFER ACTIVE [OK]");
    lv_obj_set_style_text_font(lbl_sub, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_sub, COLOR_PHOSPHOR_GREEN, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 58);

    /* Scrollable Terminal Area */
    lv_obj_t *text_cont = lv_obj_create(s_modal_popup);
    lv_obj_set_size(text_cont, 376, 125);
    lv_obj_set_pos(text_cont, 4, 76);
    lv_obj_set_style_bg_color(text_cont, lv_color_make(0x02, 0x06, 0x04), 0);
    lv_obj_set_style_border_color(text_cont, COLOR_PHOSPHOR_DARK, 0);
    lv_obj_set_style_border_width(text_cont, 1, 0);
    lv_obj_set_style_radius(text_cont, 3, 0);
    lv_obj_set_style_pad_all(text_cont, 6, 0);
    lv_obj_set_scroll_dir(text_cont, LV_DIR_VER);

    lv_obj_t *lbl_content = lv_label_create(text_cont);
    lv_label_set_text(lbl_content, s_txt_content);
    lv_obj_set_style_text_font(lbl_content, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_content, COLOR_PHOSPHOR_GREEN, 0);
    lv_obj_set_width(lbl_content, 350);
    lv_label_set_long_mode(lbl_content, LV_LABEL_LONG_WRAP);

    /* Bottom Button matching Image 1 badge: "✓ Read Complete" */
    lv_obj_t *btn_done = lv_btn_create(s_modal_popup);
    lv_obj_set_size(btn_done, 190, 30);
    lv_obj_align(btn_done, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(btn_done, COLOR_PHOSPHOR_BG, 0);
    lv_obj_set_style_border_color(btn_done, COLOR_PHOSPHOR_GREEN, 0);
    lv_obj_set_style_border_width(btn_done, 1, 0);
    lv_obj_set_style_radius(btn_done, 6, 0);
    lv_obj_add_event_cb(btn_done, close_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_btn = lv_label_create(btn_done);
    lv_label_set_text(lbl_btn, LV_SYMBOL_OK " Read Complete");
    lv_obj_set_style_text_font(lbl_btn, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_btn, COLOR_PHOSPHOR_GREEN, 0);
    lv_obj_center(lbl_btn);
}

/* 2. AUDIO PLAYER POPUP */
static void audio_timer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t pos = audio_player_get_position_sec();
    uint32_t dur = audio_player_get_duration_sec();
    bool is_playing = audio_player_is_playing();

    if (s_lbl_audio_time && lv_obj_is_valid(s_lbl_audio_time)) {
        char t_buf[48];
        if (dur > 0) {
            snprintf(t_buf, sizeof(t_buf), "%02lu:%02lu / %02lu:%02lu",
                     (unsigned long)(pos / 60), (unsigned long)(pos % 60),
                     (unsigned long)(dur / 60), (unsigned long)(dur % 60));
        } else {
            snprintf(t_buf, sizeof(t_buf), "%02lu:%02lu / --:--",
                     (unsigned long)(pos / 60), (unsigned long)(pos % 60));
        }
        lv_label_set_text(s_lbl_audio_time, t_buf);
    }

    /* Calculate playback fraction (0 to 32) */
    int active_bars = 0;
    if (dur > 0) {
        active_bars = (int)((pos * AUDIO_WAVE_BARS_COUNT) / dur);
    } else if (is_playing) {
        active_bars = (int)((pos % 16) * 2);
    }

    /* Animate waveform pill bars only when playing */
    if (is_playing) {
        for (int i = 0; i < AUDIO_WAVE_BARS_COUNT; i++) {
            if (s_wave_bars[i] && lv_obj_is_valid(s_wave_bars[i])) {
                int h = s_wave_base_heights[i];
                int delta = (rand() % 7) - 3;
                h += delta;
                if (h < 4) h = 4;
                if (h > 38) h = 38;
                lv_obj_set_height(s_wave_bars[i], h);
                lv_obj_set_y(s_wave_bars[i], (46 - h) / 2);

                if (i <= active_bars) {
                    lv_obj_set_style_bg_color(s_wave_bars[i], lv_color_make(0xfa, 0xfa, 0xfa), 0);
                } else {
                    lv_obj_set_style_bg_color(s_wave_bars[i], lv_color_make(0x35, 0x3d, 0x4c), 0);
                }
            }
        }
    }
}

static void audio_play_toggle_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);

    if (audio_player_is_playing()) {
        audio_player_pause();
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PLAY " PLAY");
    } else {
        audio_player_resume();
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_PAUSE " PAUSE");
    }
}

static void open_audio_player_modal(const char *filename, const char *fullpath)
{
    if (s_audio_timer) {
        lv_timer_del(s_audio_timer);
        s_audio_timer = NULL;
    }
    if (s_video_timer) {
        lv_timer_del(s_video_timer);
        s_video_timer = NULL;
    }
    if (s_modal_popup && lv_obj_is_valid(s_modal_popup)) {
        lv_obj_del(s_modal_popup);
        s_modal_popup = NULL;
    }

    /* Start real audio playback via I2S */
    esp_err_t err = audio_player_play(fullpath);
    if (err != ESP_OK) {
        show_warning_modal_ex(filename, "Audio decoder error.\nCannot play audio via I2S bus.");
        return;
    }

    lv_obj_t *parent = s_view_exp ? s_view_exp : lv_scr_act();
    s_modal_popup = lv_obj_create(parent);
    lv_obj_set_size(s_modal_popup, 400, 260);
    lv_obj_center(s_modal_popup);
    lv_obj_set_style_bg_color(s_modal_popup, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_modal_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_modal_popup, COLOR_CYAN, 0);
    lv_obj_set_style_border_width(s_modal_popup, 2, 0);
    lv_obj_set_style_radius(s_modal_popup, 6, 0);
    lv_obj_clear_flag(s_modal_popup, LV_OBJ_FLAG_SCROLLABLE);

    /* Top Title */
    lv_obj_t *lbl_t = lv_label_create(s_modal_popup);
    lv_label_set_text(lbl_t, "AUDIO DECODER // I2S BUS (DAC MAX98357A)");
    lv_obj_set_style_text_font(lbl_t, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_t, COLOR_CYAN, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 10, 10);

    /* Track Name */
    lv_obj_t *lbl_fn = lv_label_create(s_modal_popup);
    lv_label_set_text(lbl_fn, filename);
    lv_obj_set_style_text_font(lbl_fn, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_fn, COLOR_TEXT_WHITE, 0);
    lv_obj_align(lbl_fn, LV_ALIGN_TOP_LEFT, 16, 36);

    /* Stylized Waveform Container */
    lv_obj_t *wave_box = lv_obj_create(s_modal_popup);
    lv_obj_set_size(wave_box, 364, 60);
    lv_obj_align(wave_box, LV_ALIGN_TOP_MID, 0, 74);
    lv_obj_set_style_bg_color(wave_box, lv_color_make(0x10, 0x14, 0x20), 0);
    lv_obj_set_style_border_color(wave_box, lv_color_make(0x28, 0x34, 0x48), 0);
    lv_obj_set_style_border_width(wave_box, 1, 0);
    lv_obj_set_style_radius(wave_box, 8, 0);
    lv_obj_set_style_pad_all(wave_box, 0, 0);
    lv_obj_clear_flag(wave_box, LV_OBJ_FLAG_SCROLLABLE);

    /* Music Note Icon Badge on Left */
    lv_obj_t *badge = lv_obj_create(wave_box);
    lv_obj_set_size(badge, 40, 40);
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(badge, lv_color_make(0x20, 0x27, 0x38), 0);
    lv_obj_set_style_border_color(badge, lv_color_make(0x38, 0x44, 0x5c), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_radius(badge, 8, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_badge = lv_label_create(badge);
    lv_label_set_text(lbl_badge, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(lbl_badge, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_badge, lv_color_make(0x38, 0xbf, 0xd8), 0);
    lv_obj_center(lbl_badge);

    /* 32 Vertical Pill Bars */
    const int start_x = 56;
    const int bar_w = 4;
    const int bar_gap = 5;
    for (int i = 0; i < AUDIO_WAVE_BARS_COUNT; i++) {
        s_wave_bars[i] = lv_obj_create(wave_box);
        int h = s_wave_base_heights[i];
        lv_obj_set_size(s_wave_bars[i], bar_w, h);
        lv_obj_set_pos(s_wave_bars[i], start_x + i * (bar_w + bar_gap), (46 - h) / 2);
        lv_obj_set_style_bg_color(s_wave_bars[i], lv_color_make(0x35, 0x3d, 0x4c), 0);
        lv_obj_set_style_border_width(s_wave_bars[i], 0, 0);
        lv_obj_set_style_radius(s_wave_bars[i], 2, 0);
        lv_obj_clear_flag(s_wave_bars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    /* Time label */
    s_lbl_audio_time = lv_label_create(s_modal_popup);
    lv_label_set_text(s_lbl_audio_time, "00:00 / --:--");
    lv_obj_set_style_text_font(s_lbl_audio_time, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_audio_time, COLOR_TEXT_WHITE, 0);
    lv_obj_align(s_lbl_audio_time, LV_ALIGN_TOP_LEFT, 24, 146);

    /* Hardware info */
    s_lbl_audio_meta = lv_label_create(s_modal_popup);
    lv_label_set_text(s_lbl_audio_meta, "I2S 16-bit PCM | Vol 80%");
    lv_obj_set_style_text_font(s_lbl_audio_meta, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_audio_meta, COLOR_TEXT_MUTED, 0);
    lv_obj_align(s_lbl_audio_meta, LV_ALIGN_TOP_RIGHT, -24, 146);

    /* Play/Pause Button */
    s_audio_playing = true;
    lv_obj_t *btn_play = lv_btn_create(s_modal_popup);
    lv_obj_set_size(btn_play, 130, 36);
    lv_obj_align(btn_play, LV_ALIGN_BOTTOM_LEFT, 24, -14);
    lv_obj_set_style_bg_color(btn_play, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(btn_play, COLOR_MINT, 0);
    lv_obj_set_style_border_width(btn_play, 1, 0);
    lv_obj_set_style_radius(btn_play, 4, 0);
    lv_obj_add_event_cb(btn_play, audio_play_toggle_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_play = lv_label_create(btn_play);
    lv_label_set_text(lbl_play, LV_SYMBOL_PAUSE " PAUSE");
    lv_obj_set_style_text_font(lbl_play, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_play, COLOR_MINT, 0);
    lv_obj_center(lbl_play);

    /* Stop & Close Button */
    lv_obj_t *btn_close = lv_btn_create(s_modal_popup);
    lv_obj_set_size(btn_close, 130, 36);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, -24, -14);
    lv_obj_set_style_bg_color(btn_close, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(btn_close, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(btn_close, 1, 0);
    lv_obj_set_style_radius(btn_close, 4, 0);
    lv_obj_add_event_cb(btn_close, close_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, LV_SYMBOL_STOP " STOP");
    lv_obj_set_style_text_font(lbl_close, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_close, COLOR_ORANGE, 0);
    lv_obj_center(lbl_close);

    s_audio_timer = lv_timer_create(audio_timer_cb, 250, NULL);
}

/* 3. VIDEO VIEWER POPUP */
static void video_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_video_playing || !s_media_img_obj) return;

    int fw = 0, fh = 0;
    bool ok = media_decoder_read_next_frame(s_media_rgb_buf, &fw, &fh);
    if (!ok) {
        /* Loop video stream */
        media_decoder_rewind_mjpeg();
        ok = media_decoder_read_next_frame(s_media_rgb_buf, &fw, &fh);
    }

    if (ok) {
        lv_obj_invalidate(s_media_img_obj);
    }

    uint32_t fc = media_decoder_get_frame_count();
    if (s_lbl_video_stat && lv_obj_is_valid(s_lbl_video_stat)) {
        char v_buf[64];
        snprintf(v_buf, sizeof(v_buf), "#38bfd8 FRAME #%lu# (25 FPS)", (unsigned long)fc);
        lv_label_set_text(s_lbl_video_stat, v_buf);
    }

    if (s_bar_video && lv_obj_is_valid(s_bar_video)) {
        int pct = (fc % 100);
        lv_bar_set_value(s_bar_video, pct, LV_ANIM_OFF);
    }
}

static void video_play_toggle_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    s_video_playing = !s_video_playing;
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_label_set_text(lbl, s_video_playing ? LV_SYMBOL_PAUSE " PAUSE" : LV_SYMBOL_PLAY " PLAY");
    }
}

static void open_video_viewer_modal(const char *filename, const char *fullpath)
{
    if (s_audio_timer) {
        lv_timer_del(s_audio_timer);
        s_audio_timer = NULL;
    }
    if (s_video_timer) {
        lv_timer_del(s_video_timer);
        s_video_timer = NULL;
    }
    if (s_modal_popup && lv_obj_is_valid(s_modal_popup)) {
        lv_obj_del(s_modal_popup);
        s_modal_popup = NULL;
    }

    if (!s_media_rgb_buf) {
        s_media_rgb_buf = (uint16_t *)heap_caps_malloc(480 * 320 * 2, MALLOC_CAP_SPIRAM);
    }
    if (!s_media_rgb_buf) {
        show_warning_modal_ex(filename, "Out of PSRAM memory for video buffer.");
        return;
    }

    if (!media_decoder_open_mjpeg(fullpath)) {
        show_warning_modal_ex(filename, "Cannot open MJPEG video stream.\nCheck if file is valid .mjp / .mjpeg.");
        return;
    }

    int fw = 0, fh = 0;
    if (!media_decoder_read_next_frame(s_media_rgb_buf, &fw, &fh)) {
        media_decoder_close_mjpeg();
        show_warning_modal_ex(filename, "Failed to decode MJPEG video frame.");
        return;
    }

    s_media_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_media_img_dsc.header.w = fw;
    s_media_img_dsc.header.h = fh;
    s_media_img_dsc.data_size = fw * fh * 2;
    s_media_img_dsc.data = (const uint8_t *)s_media_rgb_buf;

    lv_obj_t *parent = s_view_exp ? s_view_exp : lv_scr_act();
    s_modal_popup = lv_obj_create(parent);
    lv_obj_set_size(s_modal_popup, 400, 260);
    lv_obj_center(s_modal_popup);
    lv_obj_set_style_bg_color(s_modal_popup, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_modal_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_modal_popup, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(s_modal_popup, 2, 0);
    lv_obj_set_style_radius(s_modal_popup, 6, 0);
    lv_obj_clear_flag(s_modal_popup, LV_OBJ_FLAG_SCROLLABLE);

    /* Video Title */
    lv_obj_t *lbl_t = lv_label_create(s_modal_popup);
    char tbuf[128];
    snprintf(tbuf, sizeof(tbuf), "VIDEO // %s (%dx%d)", filename, fw, fh);
    lv_label_set_text(lbl_t, tbuf);
    lv_obj_set_style_text_font(lbl_t, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_t, COLOR_ORANGE, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 10, 8);

    /* Frame box */
    lv_obj_t *frame_box = lv_obj_create(s_modal_popup);
    s_media_box = frame_box;
    lv_obj_set_size(frame_box, 360, 150);
    lv_obj_align(frame_box, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_color(frame_box, lv_color_make(0x05, 0x08, 0x10), 0);
    lv_obj_set_style_border_color(frame_box, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(frame_box, 1, 0);
    lv_obj_set_style_pad_all(frame_box, 0, 0);
    lv_obj_clear_flag(frame_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(frame_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(frame_box, toggle_fullscreen_cb, LV_EVENT_CLICKED, NULL);

    /* Live Video Image */
    s_media_img_obj = lv_img_create(frame_box);
    lv_img_set_src(s_media_img_obj, &s_media_img_dsc);
    lv_obj_center(s_media_img_obj);
    lv_obj_add_flag(s_media_img_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_media_img_obj, toggle_fullscreen_cb, LV_EVENT_CLICKED, NULL);

    /* Timeline */
    s_bar_video = lv_bar_create(s_modal_popup);
    lv_obj_set_size(s_bar_video, 360, 6);
    lv_obj_align(s_bar_video, LV_ALIGN_TOP_MID, 0, 184);
    lv_obj_set_style_bg_color(s_bar_video, lv_color_make(0x18, 0x24, 0x38), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_video, COLOR_ORANGE, LV_PART_INDICATOR);
    lv_bar_set_value(s_bar_video, 0, LV_ANIM_OFF);

    /* Frame stats */
    s_lbl_video_stat = lv_label_create(s_modal_popup);
    lv_label_set_recolor(s_lbl_video_stat, true);
    lv_label_set_text(s_lbl_video_stat, "#38bfd8 FRAME #1# (25 FPS)");
    lv_obj_set_style_text_font(s_lbl_video_stat, &font_orbitron_12, 0);
    lv_obj_align(s_lbl_video_stat, LV_ALIGN_BOTTOM_LEFT, 20, -10);

    /* Play/Pause Button */
    s_video_playing = true;
    lv_obj_t *btn_play = lv_btn_create(s_modal_popup);
    lv_obj_set_size(btn_play, 85, 28);
    lv_obj_align(btn_play, LV_ALIGN_BOTTOM_RIGHT, -185, -6);
    lv_obj_set_style_bg_color(btn_play, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(btn_play, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(btn_play, 1, 0);
    lv_obj_add_event_cb(btn_play, video_play_toggle_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_play = lv_label_create(btn_play);
    lv_label_set_text(lbl_play, LV_SYMBOL_PAUSE " PAUSE");
    lv_obj_set_style_text_font(lbl_play, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_play, COLOR_ORANGE, 0);
    lv_obj_center(lbl_play);

    /* Fullscreen Toggle Button */
    lv_obj_t *btn_full = lv_btn_create(s_modal_popup);
    lv_obj_set_size(btn_full, 80, 28);
    lv_obj_align(btn_full, LV_ALIGN_BOTTOM_RIGHT, -98, -6);
    lv_obj_set_style_bg_color(btn_full, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(btn_full, COLOR_CYAN, 0);
    lv_obj_set_style_border_width(btn_full, 1, 0);
    lv_obj_add_event_cb(btn_full, toggle_fullscreen_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_full = lv_label_create(btn_full);
    lv_label_set_text(lbl_full, LV_SYMBOL_IMAGE " FULL");
    lv_obj_set_style_text_font(lbl_full, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_full, COLOR_CYAN, 0);
    lv_obj_center(lbl_full);

    /* Close Button */
    lv_obj_t *btn_close = lv_btn_create(s_modal_popup);
    lv_obj_set_size(btn_close, 80, 28);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, -12, -6);
    lv_obj_set_style_bg_color(btn_close, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(btn_close, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(btn_close, 1, 0);
    lv_obj_add_event_cb(btn_close, close_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_c = lv_label_create(btn_close);
    lv_label_set_text(lbl_c, "EXIT");
    lv_obj_set_style_text_font(lbl_c, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_c, COLOR_TEXT_MUTED, 0);
    lv_obj_center(lbl_c);

    /* Register UI controls to hide/show during fullscreen toggle */
    s_media_ctrls_count = 0;
    s_media_ctrls[s_media_ctrls_count++] = lbl_t;
    s_media_ctrls[s_media_ctrls_count++] = s_bar_video;
    s_media_ctrls[s_media_ctrls_count++] = s_lbl_video_stat;
    s_media_ctrls[s_media_ctrls_count++] = btn_play;
    s_media_ctrls[s_media_ctrls_count++] = btn_full;
    s_media_ctrls[s_media_ctrls_count++] = btn_close;

    /* 40ms interval ~ 25 FPS playback */
    s_video_timer = lv_timer_create(video_timer_cb, 40, NULL);
}

/* 4. IMAGE VIEWER POPUP */
static void open_image_viewer_modal(const char *filename, const char *fullpath)
{
    if (s_audio_timer) {
        lv_timer_del(s_audio_timer);
        s_audio_timer = NULL;
    }
    if (s_video_timer) {
        lv_timer_del(s_video_timer);
        s_video_timer = NULL;
    }
    if (s_modal_popup && lv_obj_is_valid(s_modal_popup)) {
        lv_obj_del(s_modal_popup);
        s_modal_popup = NULL;
    }

    if (!s_media_rgb_buf) {
        s_media_rgb_buf = (uint16_t *)heap_caps_malloc(480 * 320 * 2, MALLOC_CAP_SPIRAM);
    }
    if (!s_media_rgb_buf) {
        show_warning_modal_ex(filename, "Out of PSRAM memory to decode image.");
        return;
    }

    int img_w = 0, img_h = 0;
    bool ok = media_decoder_load_image(fullpath, s_media_rgb_buf, 480, 320, &img_w, &img_h);
    if (!ok) {
        show_warning_modal_ex(filename, "Image decoder error.\nCannot parse JPEG / BMP file format.");
        return;
    }

    s_media_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    s_media_img_dsc.header.w = img_w;
    s_media_img_dsc.header.h = img_h;
    s_media_img_dsc.data_size = img_w * img_h * 2;
    s_media_img_dsc.data = (const uint8_t *)s_media_rgb_buf;

    lv_obj_t *parent = s_view_exp ? s_view_exp : lv_scr_act();
    s_modal_popup = lv_obj_create(parent);
    lv_obj_set_size(s_modal_popup, 400, 260);
    lv_obj_center(s_modal_popup);
    lv_obj_set_style_bg_color(s_modal_popup, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_modal_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_modal_popup, lv_color_make(0xff, 0xc1, 0x07), 0);
    lv_obj_set_style_border_width(s_modal_popup, 2, 0);
    lv_obj_set_style_radius(s_modal_popup, 6, 0);
    lv_obj_clear_flag(s_modal_popup, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *lbl_t = lv_label_create(s_modal_popup);
    char tbuf[128];
    snprintf(tbuf, sizeof(tbuf), "PHOTO // %s (%dx%d)", filename, img_w, img_h);
    lv_label_set_text(lbl_t, tbuf);
    lv_obj_set_style_text_font(lbl_t, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_t, lv_color_make(0xff, 0xc1, 0x07), 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 10, 8);

    /* Frame box */
    lv_obj_t *box = lv_obj_create(s_modal_popup);
    s_media_box = box;
    lv_obj_set_size(box, 360, 160);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_color(box, lv_color_make(0x05, 0x08, 0x10), 0);
    lv_obj_set_style_border_color(box, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, toggle_fullscreen_cb, LV_EVENT_CLICKED, NULL);

    /* Actual decoded image widget */
    s_media_img_obj = lv_img_create(box);
    lv_img_set_src(s_media_img_obj, &s_media_img_dsc);
    lv_obj_center(s_media_img_obj);
    lv_obj_add_flag(s_media_img_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_media_img_obj, toggle_fullscreen_cb, LV_EVENT_CLICKED, NULL);

    /* Fullscreen Button */
    lv_obj_t *btn_full = lv_btn_create(s_modal_popup);
    lv_obj_set_size(btn_full, 110, 30);
    lv_obj_align(btn_full, LV_ALIGN_BOTTOM_LEFT, 60, -8);
    lv_obj_set_style_bg_color(btn_full, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(btn_full, COLOR_CYAN, 0);
    lv_obj_set_style_border_width(btn_full, 1, 0);
    lv_obj_add_event_cb(btn_full, toggle_fullscreen_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_f = lv_label_create(btn_full);
    lv_label_set_text(lbl_f, LV_SYMBOL_IMAGE " FULL");
    lv_obj_set_style_text_font(lbl_f, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_f, COLOR_CYAN, 0);
    lv_obj_center(lbl_f);

    /* Close Button */
    lv_obj_t *btn_close = lv_btn_create(s_modal_popup);
    lv_obj_set_size(btn_close, 110, 30);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_RIGHT, -60, -8);
    lv_obj_set_style_bg_color(btn_close, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(btn_close, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(btn_close, 1, 0);
    lv_obj_add_event_cb(btn_close, close_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_c = lv_label_create(btn_close);
    lv_label_set_text(lbl_c, "CLOSE");
    lv_obj_set_style_text_font(lbl_c, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_c, COLOR_ORANGE, 0);
    lv_obj_center(lbl_c);

    /* Register UI controls to hide/show during fullscreen toggle */
    s_media_ctrls_count = 0;
    s_media_ctrls[s_media_ctrls_count++] = lbl_t;
    s_media_ctrls[s_media_ctrls_count++] = btn_full;
    s_media_ctrls[s_media_ctrls_count++] = btn_close;
}

/* 5. WARNING POPUP (For unsupported/corrupted formats or strange characters) */
static void show_warning_modal_ex(const char *filename, const char *reason)
{
    if (s_audio_timer) {
        lv_timer_del(s_audio_timer);
        s_audio_timer = NULL;
    }
    if (s_video_timer) {
        lv_timer_del(s_video_timer);
        s_video_timer = NULL;
    }
    if (s_modal_popup && lv_obj_is_valid(s_modal_popup)) {
        lv_obj_del(s_modal_popup);
        s_modal_popup = NULL;
    }
    lv_obj_t *parent = s_view_exp ? s_view_exp : lv_scr_act();
    s_modal_popup = lv_obj_create(parent);
    lv_obj_set_size(s_modal_popup, 400, 260);
    lv_obj_center(s_modal_popup);
    lv_obj_set_style_bg_color(s_modal_popup, lv_color_make(0x18, 0x0a, 0x0a), 0);
    lv_obj_set_style_bg_opa(s_modal_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_modal_popup, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(s_modal_popup, 2, 0);
    lv_obj_set_style_radius(s_modal_popup, 6, 0);
    lv_obj_clear_flag(s_modal_popup, LV_OBJ_FLAG_SCROLLABLE);

    /* Warning Icon */
    lv_obj_t *lbl_warn_ico = lv_label_create(s_modal_popup);
    lv_label_set_text(lbl_warn_ico, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(lbl_warn_ico, &font_orbitron_24, 0);
    lv_obj_set_style_text_color(lbl_warn_ico, COLOR_ORANGE, 0);
    lv_obj_align(lbl_warn_ico, LV_ALIGN_TOP_MID, 0, 8);

    /* Header */
    lv_obj_t *lbl_title = lv_label_create(s_modal_popup);
    lv_label_set_text(lbl_title, "[ WARNING: CANNOT RENDER FILE ]");
    lv_obj_set_style_text_font(lbl_title, &font_orbitron_16, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_ORANGE, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 42);

    /* Body Text */
    lv_obj_t *lbl_body = lv_label_create(s_modal_popup);
    lv_label_set_recolor(lbl_body, true);
    char buf[320];
    snprintf(buf, sizeof(buf),
        "#f0f4f8 TARGET FILE:# #ff5a2a \"%s\"#\n\n"
        "#ff9966 %s#\n\n"
        "#33d17a SUPPORTED FILE TYPES:#\n"
        "#f0f4f8 - TEXT:#  ASCII .TXT, .LOG, .JSON, .C, .H\n"
        "#f0f4f8 - MEDIA:# .MP3, .WAV, .MJP, .MJPEG",
        filename, reason ? reason : "Unsupported format or binary payload.");
    lv_label_set_text(lbl_body, buf);
    lv_obj_set_style_text_font(lbl_body, &font_orbitron_12, 0);
    lv_obj_align(lbl_body, LV_ALIGN_TOP_MID, 0, 72);

    /* Dismiss Button */
    lv_obj_t *btn_dismiss = lv_btn_create(s_modal_popup);
    lv_obj_set_size(btn_dismiss, 120, 32);
    lv_obj_align(btn_dismiss, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(btn_dismiss, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_dismiss, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(btn_dismiss, 1, 0);
    lv_obj_add_event_cb(btn_dismiss, close_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_d = lv_label_create(btn_dismiss);
    lv_label_set_text(lbl_d, "DISMISS");
    lv_obj_set_style_text_font(lbl_d, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_d, COLOR_ORANGE, 0);
    lv_obj_center(lbl_d);
}

static void show_warning_modal(const char *filename)
{
    show_warning_modal_ex(filename, "System does not support this file format or binary payload.");
}

/* Dispatcher based on detected file type */
static void open_file_viewer(const char *filename, file_type_t type)
{
    if (s_audio_timer) {
        lv_timer_del(s_audio_timer);
        s_audio_timer = NULL;
    }
    if (s_video_timer) {
        lv_timer_del(s_video_timer);
        s_video_timer = NULL;
    }
    if (s_modal_popup && lv_obj_is_valid(s_modal_popup)) {
        lv_obj_del(s_modal_popup);
        s_modal_popup = NULL;
    }

    char fullpath[600];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", s_current_dir, filename);

    switch (type) {
        case FILE_TYPE_TEXT:
            open_text_viewer_modal(filename, fullpath);
            break;
        case FILE_TYPE_AUDIO:
            open_audio_player_modal(filename, fullpath);
            break;
        case FILE_TYPE_VIDEO:
            open_video_viewer_modal(filename, fullpath);
            break;
        case FILE_TYPE_IMAGE:
            open_image_viewer_modal(filename, fullpath);
            break;
        default:
            show_warning_modal(filename);
    }
}

/* --------------------------------------------------------------------
 * Scan and Render File Explorer View
 * -------------------------------------------------------------------- */
static void scan_and_render_files(void)
{
    if (!s_file_list_cont || !lv_obj_is_valid(s_file_list_cont)) return;

    lv_obj_clean(s_file_list_cont);

    scan_explorer_dir(s_current_dir);

    char info_buf[600];
    if (s_curr_tab == EXP_TAB_SDCARD && !gui_storage_is_sdcard_mounted()) {
        snprintf(info_buf, sizeof(info_buf), "#ff5a2a [ TF CARD UNMOUNTED // INSERT & RESCAN ]#");
    } else {
        snprintf(info_buf, sizeof(info_buf), "#33d17a [ PATH ]# %s (%d ITEMS)", s_current_dir, s_num_files);
    }
    lv_label_set_text(s_lbl_drive_info, info_buf);

    if (s_num_files == 0) {
        lv_obj_t *empty_box = lv_obj_create(s_file_list_cont);
        lv_obj_set_size(empty_box, 440, 110);
        lv_obj_set_style_bg_color(empty_box, COLOR_CARD_BG, 0);
        lv_obj_set_style_border_color(empty_box, COLOR_BORDER_LINE, 0);
        lv_obj_set_style_border_width(empty_box, 1, 0);
        lv_obj_set_style_radius(empty_box, 3, 0);
        lv_obj_center(empty_box);
        lv_obj_clear_flag(empty_box, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_e = lv_label_create(empty_box);
        if (s_curr_tab == EXP_TAB_SDCARD) {
            if (!gui_storage_is_sdcard_mounted()) {
                lv_label_set_recolor(lbl_e, true);
                lv_label_set_text(lbl_e,
                    "#ff5a2a [ NO MICRO-SD CARD DETECTED ]#\n\n"
                    "- Please insert Micro-SD Card into slot.\n"
                    "- Tap [ RESCAN ] to mount & scan files.");
            } else {
                lv_label_set_text(lbl_e,
                    "[ DIRECTORY IS EMPTY ]\n\n"
                    "- Micro-SD card mounted successfully.\n"
                    "- No media files found in this path.");
            }
        } else {
            lv_label_set_text(lbl_e,
                "[ NO FILES IN CURRENT DIRECTORY ]\n\n"
                "Directory is empty or ready to receive payloads.");
        }
        lv_obj_set_style_text_font(lbl_e, &font_orbitron_12, 0);
        lv_obj_set_style_text_color(lbl_e, COLOR_TEXT_MUTED, 0);
        lv_obj_center(lbl_e);
        return;
    }

    for (int i = 0; i < s_num_files; i++) {
        exp_file_item_t *item = &s_files[i];

        lv_obj_t *row = lv_btn_create(s_file_list_cont);
        lv_obj_set_size(row, 440, 36);

        /* Distinct styling for Folders vs Files */
        if (item->type == FILE_TYPE_DIR) {
            if (strcmp(item->name, "..") == 0) {
                lv_obj_set_style_bg_color(row, lv_color_make(0x0c, 0x1e, 0x28), 0);
                lv_obj_set_style_border_color(row, COLOR_CYAN, 0);
            } else {
                lv_obj_set_style_bg_color(row, lv_color_make(0x18, 0x18, 0x22), 0);
                lv_obj_set_style_border_color(row, lv_color_make(0x70, 0x50, 0x20), 0);
            }
        } else {
            lv_obj_set_style_bg_color(row, COLOR_CARD_BG, 0);
            lv_obj_set_style_border_color(row, COLOR_BORDER_LINE, 0);
        }
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 2, 0);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_add_event_cb(row, file_row_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        /* Icon */
        lv_obj_t *lbl_ico = lv_label_create(row);
        if (strcmp(item->name, "..") == 0) {
            lv_label_set_text(lbl_ico, LV_SYMBOL_UP);
            lv_obj_set_style_text_color(lbl_ico, COLOR_CYAN, 0);
        } else {
            lv_label_set_text(lbl_ico, get_icon_for_type(item->type));
            lv_obj_set_style_text_color(lbl_ico, get_color_for_type(item->type), 0);
        }
        lv_obj_align(lbl_ico, LV_ALIGN_LEFT_MID, 0, 0);

        /* Filename / Folder name */
        lv_obj_t *lbl_name = lv_label_create(row);
        if (strcmp(item->name, "..") == 0) {
            lv_label_set_text(lbl_name, ".. (UP TO PARENT DIRECTORY)");
            lv_obj_set_style_text_color(lbl_name, COLOR_CYAN, 0);
        } else if (item->type == FILE_TYPE_DIR) {
            char f_buf[72];
            snprintf(f_buf, sizeof(f_buf), "%s/", item->name);
            lv_label_set_text(lbl_name, f_buf);
            lv_obj_set_style_text_color(lbl_name, lv_color_make(0xff, 0xc0, 0x60), 0);
        } else {
            lv_label_set_text(lbl_name, item->name);
            lv_obj_set_style_text_color(lbl_name, COLOR_TEXT_WHITE, 0);
        }
        lv_obj_set_style_text_font(lbl_name, &font_orbitron_12, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 30, 0);

        /* Size / Action badge */
        lv_obj_t *lbl_size = lv_label_create(row);
        if (strcmp(item->name, "..") == 0) {
            lv_label_set_text(lbl_size, "[ UP ]");
            lv_obj_set_style_text_color(lbl_size, COLOR_CYAN, 0);
        } else if (item->type == FILE_TYPE_DIR) {
            lv_label_set_text(lbl_size, "<DIR> " LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(lbl_size, COLOR_ORANGE, 0);
        } else {
            char s_buf[32];
            if (item->size >= 1024 * 1024) {
                snprintf(s_buf, sizeof(s_buf), "%.1f MB", (float)item->size / (1024.0f * 1024.0f));
            } else if (item->size >= 1024) {
                snprintf(s_buf, sizeof(s_buf), "%.1f KB", (float)item->size / 1024.0f);
            } else {
                snprintf(s_buf, sizeof(s_buf), "%u B", (unsigned int)item->size);
            }
            lv_label_set_text(lbl_size, s_buf);
            lv_obj_set_style_text_color(lbl_size, COLOR_TEXT_MUTED, 0);
        }
        lv_obj_set_style_text_font(lbl_size, &font_orbitron_12, 0);
        lv_obj_align(lbl_size, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void switch_exp_tab(exp_tab_t tab)
{
    s_curr_tab = tab;

    if (s_curr_tab == EXP_TAB_INTERNAL) {
        strncpy(s_current_dir, "/storage", sizeof(s_current_dir) - 1);
        s_current_dir[sizeof(s_current_dir) - 1] = '\0';

        lv_obj_set_style_bg_color(s_btn_tab_int, COLOR_CYAN, 0);
        lv_obj_set_style_border_color(s_btn_tab_int, COLOR_CYAN, 0);
        lv_obj_set_style_text_color(s_lbl_tab_int, COLOR_PANEL_BG, 0);

        lv_obj_set_style_bg_color(s_btn_tab_sd, COLOR_PANEL_BG, 0);
        lv_obj_set_style_border_color(s_btn_tab_sd, COLOR_BORDER_LINE, 0);
        lv_obj_set_style_text_color(s_lbl_tab_sd, COLOR_TEXT_MUTED, 0);
    } else {
        strncpy(s_current_dir, "/sdcard", sizeof(s_current_dir) - 1);
        s_current_dir[sizeof(s_current_dir) - 1] = '\0';

        /* Ensure SD card is mounted before displaying tab */
        if (!gui_storage_is_sdcard_mounted()) {
            gui_storage_mount_sdcard();
        }

        lv_obj_set_style_bg_color(s_btn_tab_sd, COLOR_CYAN, 0);
        lv_obj_set_style_border_color(s_btn_tab_sd, COLOR_CYAN, 0);
        lv_obj_set_style_text_color(s_lbl_tab_sd, COLOR_PANEL_BG, 0);

        lv_obj_set_style_bg_color(s_btn_tab_int, COLOR_PANEL_BG, 0);
        lv_obj_set_style_border_color(s_btn_tab_int, COLOR_BORDER_LINE, 0);
        lv_obj_set_style_text_color(s_lbl_tab_int, COLOR_TEXT_MUTED, 0);
    }

    lv_async_call(async_scan_and_render_cb, NULL);
}

static void tab_btn_click_cb(lv_event_t *e)
{
    exp_tab_t tab = (exp_tab_t)(uintptr_t)lv_event_get_user_data(e);
    switch_exp_tab(tab);
}

static void rescan_btn_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_curr_tab == EXP_TAB_SDCARD && !gui_storage_is_sdcard_mounted()) {
        gui_storage_mount_sdcard();
    }
    lv_async_call(async_scan_and_render_cb, NULL);
}

static void on_file_explorer_close(void)
{
    audio_player_stop();
    media_decoder_close_mjpeg();
    s_audio_playing = false;
    s_video_playing = false;
    s_media_img_obj = NULL;

    if (s_audio_timer) {
        lv_timer_del(s_audio_timer);
        s_audio_timer = NULL;
    }
    if (s_video_timer) {
        lv_timer_del(s_video_timer);
        s_video_timer = NULL;
    }
    s_view_exp = NULL;
    s_modal_popup = NULL;
    s_file_list_cont = NULL;
    s_btn_tab_int = NULL;
    s_btn_tab_sd = NULL;
    s_lbl_drive_info = NULL;
}

void gui_file_explorer_open(lv_obj_t *parent)
{
    (void)parent;
    if (s_view_exp) return;
    ESP_LOGI(TAG, "Opening File Explorer");

    ui_frame_config_t cfg = {
        .title = "EXPLORER",
        .category = "MEDIA REPOSITORY",
        .mode = UI_FRAME_DEFAULT,
        .on_close = on_file_explorer_close,
        .user_data = NULL
    };
    ui_frame_handle_t *frame = ui_frame_push(&cfg);
    if (!frame) return;
    s_view_exp = frame->root;

    /* Rescan Button in Footer Action area */
    lv_obj_t *btn_rescan = lv_btn_create(frame->footer_action);
    lv_obj_set_size(btn_rescan, 100, 26);
    lv_obj_set_pos(btn_rescan, 0, 1);
    lv_obj_set_style_bg_color(btn_rescan, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_rescan, COLOR_MINT, 0);
    lv_obj_set_style_border_width(btn_rescan, 1, 0);
    lv_obj_set_style_radius(btn_rescan, 2, 0);
    lv_obj_clear_flag(btn_rescan, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_rescan, rescan_btn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_rescan = lv_label_create(btn_rescan);
    lv_label_set_text(lbl_rescan, LV_SYMBOL_REFRESH " RESCAN");
    lv_obj_set_style_text_font(lbl_rescan, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_rescan, COLOR_MINT, 0);
    lv_obj_center(lbl_rescan);

    /* Segmented Drive Tabs inside frame->content */
    int tab_w = 226;
    int tab_h = 26;

    s_btn_tab_int = lv_btn_create(frame->content);
    lv_obj_set_size(s_btn_tab_int, tab_w, tab_h);
    lv_obj_set_pos(s_btn_tab_int, 0, 0);
    lv_obj_set_style_radius(s_btn_tab_int, 2, 0);
    lv_obj_set_style_border_width(s_btn_tab_int, 1, 0);
    lv_obj_clear_flag(s_btn_tab_int, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_btn_tab_int, tab_btn_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)EXP_TAB_INTERNAL);

    s_lbl_tab_int = lv_label_create(s_btn_tab_int);
    lv_label_set_text(s_lbl_tab_int, LV_SYMBOL_DRIVE " FLASH (/storage)");
    lv_obj_set_style_text_font(s_lbl_tab_int, &font_orbitron_12, 0);
    lv_obj_center(s_lbl_tab_int);

    s_btn_tab_sd = lv_btn_create(frame->content);
    lv_obj_set_size(s_btn_tab_sd, tab_w, tab_h);
    lv_obj_set_pos(s_btn_tab_sd, 234, 0);
    lv_obj_set_style_radius(s_btn_tab_sd, 2, 0);
    lv_obj_set_style_border_width(s_btn_tab_sd, 1, 0);
    lv_obj_clear_flag(s_btn_tab_sd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_btn_tab_sd, tab_btn_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)EXP_TAB_SDCARD);

    s_lbl_tab_sd = lv_label_create(s_btn_tab_sd);
    lv_label_set_text(s_lbl_tab_sd, LV_SYMBOL_SD_CARD " TF CARD (/sdcard)");
    lv_obj_set_style_text_font(s_lbl_tab_sd, &font_orbitron_12, 0);
    lv_obj_center(s_lbl_tab_sd);

    /* Telemetry Ribbon */
    lv_obj_t *ribbon = lv_obj_create(frame->content);
    lv_obj_set_size(ribbon, 460, 22);
    lv_obj_set_pos(ribbon, 0, 30);
    lv_obj_set_style_bg_color(ribbon, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(ribbon, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(ribbon, 1, 0);
    lv_obj_set_style_radius(ribbon, 2, 0);
    lv_obj_set_style_pad_hor(ribbon, 8, 0);
    lv_obj_set_style_pad_ver(ribbon, 0, 0);
    lv_obj_clear_flag(ribbon, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_drive_info = lv_label_create(ribbon);
    lv_label_set_recolor(s_lbl_drive_info, true);
    lv_label_set_text(s_lbl_drive_info, "#33d17a [ SCANNING DRIVE ]#");
    lv_obj_set_style_text_font(s_lbl_drive_info, &font_orbitron_12, 0);
    lv_obj_align(s_lbl_drive_info, LV_ALIGN_LEFT_MID, 0, 0);

    /* Scrollable File List */
    s_file_list_cont = lv_obj_create(frame->content);
    lv_obj_set_size(s_file_list_cont, 460, 168);
    lv_obj_set_pos(s_file_list_cont, 0, 56);
    lv_obj_set_style_bg_color(s_file_list_cont, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_file_list_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_file_list_cont, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(s_file_list_cont, 1, 0);
    lv_obj_set_style_radius(s_file_list_cont, 3, 0);
    lv_obj_set_style_pad_all(s_file_list_cont, 6, 0);
    lv_obj_set_style_pad_row(s_file_list_cont, 4, 0);
    lv_obj_set_flex_flow(s_file_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_file_list_cont, LV_DIR_VER);

    switch_exp_tab(EXP_TAB_INTERNAL);
}
