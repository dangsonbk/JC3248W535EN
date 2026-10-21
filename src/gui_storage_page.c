#include "gui_storage_page.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "gui_file_explorer.h"

static const char *TAG = "GUI_STORAGE";

/* Cyberpunk Palette */
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

typedef enum {
    STORAGE_TAB_INTERNAL = 0,
    STORAGE_TAB_SDCARD   = 1
} storage_tab_t;

static storage_tab_t s_active_tab = STORAGE_TAB_INTERNAL;

static size_t s_total_bytes = 0;
static size_t s_used_bytes  = 0;

static bool s_spiffs_mounted = false;
static bool s_sd_mounted     = false;
static sdmmc_card_t *s_sd_card = NULL;

/* UI Widgets */
static lv_obj_t *s_view_storage     = NULL;
static lv_obj_t *s_btn_tab_internal = NULL;
static lv_obj_t *s_btn_tab_sdcard   = NULL;
static lv_obj_t *s_lbl_tab_internal = NULL;
static lv_obj_t *s_lbl_tab_sdcard   = NULL;

static lv_obj_t *s_arc_donut        = NULL;
static lv_obj_t *s_lbl_donut_pct    = NULL;
static lv_obj_t *s_lbl_donut_sub    = NULL;

static lv_obj_t *s_lbl_status_val   = NULL;
static lv_obj_t *s_lbl_details      = NULL;
static lv_obj_t *s_bar_storage      = NULL;

static void update_storage_display(void);
static void switch_tab(storage_tab_t tab);

/* --------------------------------------------------------------------
 * Filesystem Operations
 * -------------------------------------------------------------------- */
static esp_err_t init_internal_spiffs(void)
{
    if (s_spiffs_mounted) return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/storage",
        .partition_label = "littlefs",
        .max_files = 10,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        s_spiffs_mounted = true;
        ESP_LOGI(TAG, "SPIFFS mounted at /storage");

        size_t total = 0, used = 0;
        esp_spiffs_info("littlefs", &total, &used);
        if (used < 1024) {
            FILE *f = fopen("/storage/welcome.txt", "w");
            if (f) {
                fprintf(f, "System: JC3248W535EN Mission Control\nStorage: Internal Flash Partition 'littlefs'\nStatus: [OK] Ready for operation.\n");
                fclose(f);
            }
        }
    } else {
        ESP_LOGW(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
    }
    return ret;
}

static bool s_sd_stats_cached = false;
static size_t s_cached_sd_total = 0;
static size_t s_cached_sd_free = 0;
static size_t s_cached_sd_used = 0;

esp_err_t gui_storage_mount_sdcard(void)
{
    if (s_sd_mounted && s_sd_card != NULL) return ESP_OK;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = (gpio_num_t)12;
    slot_config.cmd = (gpio_num_t)11;
    slot_config.d0  = (gpio_num_t)13;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &s_sd_card);
    if (ret == ESP_OK) {
        s_sd_mounted = true;
        s_sd_stats_cached = false;
        ESP_LOGI(TAG, "SD Card mounted successfully at /sdcard");
    } else {
        s_sd_mounted = false;
        s_sd_card = NULL;
        s_sd_stats_cached = false;
        ESP_LOGW(TAG, "SD Card mount failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

bool gui_storage_is_sdcard_mounted(void)
{
    return s_sd_mounted && (s_sd_card != NULL);
}

void gui_storage_init(void)
{
    init_internal_spiffs();
    /* Proactively attempt to mount SD Card at startup */
    gui_storage_mount_sdcard();
}

/* --------------------------------------------------------------------
 * Render Storage Donut Gauge & Detailed Metrics
 * -------------------------------------------------------------------- */
static void update_storage_display(void)
{
    if (!s_arc_donut || !lv_obj_is_valid(s_arc_donut)) return;

    if (s_active_tab == STORAGE_TAB_INTERNAL) {
        init_internal_spiffs();
        if (s_spiffs_mounted) {
            size_t total = 0, used = 0;
            esp_err_t err = esp_spiffs_info("littlefs", &total, &used);
            if (err == ESP_OK && total > 0) {
                s_total_bytes = total;
                s_used_bytes = used;
            } else {
                s_total_bytes = 12451840; // 0xBE0000 bytes
                s_used_bytes = used;
            }

            size_t free_bytes = (s_total_bytes > s_used_bytes) ? (s_total_bytes - s_used_bytes) : 0;
            float pct_f = (s_total_bytes > 0) ? ((float)s_used_bytes * 100.0f / (float)s_total_bytes) : 0.0f;
            int pct_int = (int)pct_f;

            int arc_val = pct_int;
            if (arc_val == 0 && s_used_bytes > 0) arc_val = 1;
            lv_arc_set_value(s_arc_donut, arc_val);

            char pct_buf[16];
            if (pct_f > 0.0f && pct_f < 1.0f) {
                snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", pct_f);
            } else {
                snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct_int);
            }
            lv_label_set_text(s_lbl_donut_pct, pct_buf);
            lv_label_set_text(s_lbl_donut_sub, "USED");

            lv_label_set_text(s_lbl_status_val, "#33d17a [ MOUNTED // ONLINE ]# /storage");

            char details_buf[512];
            snprintf(details_buf, sizeof(details_buf),
                "#708298 FORMAT:# #38bfd8 SPIFFS (Partition 'littlefs')#\n"
                "#708298 STATUS:# #33d17a READ / WRITE READY#\n"
                "#708298 FREE SPACE:# #33d17a %lu KB# (%.2f MB)\n"
                "#708298 USED SPACE:# #ff5a2a %lu KB# (%.2f MB)\n"
                "#708298 TOTAL CAPACITY:# #f0f4f8 %lu KB# (%.2f MB)\n"
                "#708298 HARDWARE BUS:# #f0f4f8 16MB Quad-SPI Flash#",
                (unsigned long)(free_bytes / 1024), (float)free_bytes / (1024.0f * 1024.0f),
                (unsigned long)(s_used_bytes / 1024), (float)s_used_bytes / (1024.0f * 1024.0f),
                (unsigned long)(s_total_bytes / 1024), (float)s_total_bytes / (1024.0f * 1024.0f));
            lv_label_set_text(s_lbl_details, details_buf);

            lv_bar_set_value(s_bar_storage, arc_val, LV_ANIM_OFF);
        } else {
            lv_arc_set_value(s_arc_donut, 0);
            lv_label_set_text(s_lbl_donut_pct, "ERR");
            lv_label_set_text(s_lbl_donut_sub, "SPIFFS");
            lv_label_set_text(s_lbl_status_val, "#ff5a2a [ UNMOUNTED // ERROR ]#");
            lv_label_set_text(s_lbl_details,
                "#708298 FORMAT:# #ff5a2a N/A#\n"
                "#708298 STATUS:# #ff5a2a FAILED TO MOUNT PARTITION#\n"
                "#708298 FREE SPACE:# #708298 0 KB#\n"
                "#708298 TOTAL CAPACITY:# #708298 0 KB#");
            lv_bar_set_value(s_bar_storage, 0, LV_ANIM_OFF);
        }
    } else {
        /* SD Card Tab */
        gui_storage_mount_sdcard();
        if (s_sd_mounted && s_sd_card) {
            size_t free_bytes = 0;

            if (s_sd_stats_cached && s_cached_sd_total > 0) {
                s_total_bytes = s_cached_sd_total;
                free_bytes = s_cached_sd_free;
                s_used_bytes = s_cached_sd_used;
            } else {
                uint64_t cap_bytes = ((uint64_t)s_sd_card->csd.capacity) * s_sd_card->csd.sector_size;
                s_total_bytes = (size_t)cap_bytes;
                s_used_bytes = 0;

                FATFS *fs = NULL;
                DWORD fre_clust = 0;
                if (f_getfree("0:", &fre_clust, &fs) == FR_OK && fs != NULL) {
                    uint64_t tot_sect = (fs->n_fatent - 2) * fs->csize;
                    uint64_t fre_sect = fre_clust * fs->csize;
                    s_total_bytes = (size_t)(tot_sect * 512ULL);
                    free_bytes = (size_t)(fre_sect * 512ULL);
                    s_used_bytes = (s_total_bytes > free_bytes) ? (s_total_bytes - free_bytes) : 0;
                } else {
                    free_bytes = s_total_bytes;
                }
                s_cached_sd_total = s_total_bytes;
                s_cached_sd_free = free_bytes;
                s_cached_sd_used = s_used_bytes;
                s_sd_stats_cached = true;
            }

            float pct_f = (s_total_bytes > 0) ? ((float)s_used_bytes * 100.0f / (float)s_total_bytes) : 0.0f;
            int pct_int = (int)pct_f;
            int arc_val = pct_int;
            if (arc_val == 0 && s_used_bytes > 0) arc_val = 1;
            lv_arc_set_value(s_arc_donut, arc_val);

            char pct_buf[16];
            if (pct_f > 0.0f && pct_f < 1.0f) {
                snprintf(pct_buf, sizeof(pct_buf), "%.1f%%", pct_f);
            } else {
                snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct_int);
            }
            lv_label_set_text(s_lbl_donut_pct, pct_buf);
            lv_label_set_text(s_lbl_donut_sub, "USED");

            char status_buf[64];
            snprintf(status_buf, sizeof(status_buf), "#33d17a [ MOUNTED // READY ]# %s", s_sd_card->cid.name);
            lv_label_set_text(s_lbl_status_val, status_buf);

            char details_buf[512];
            snprintf(details_buf, sizeof(details_buf),
                "#708298 FORMAT:# #38bfd8 FAT32 / FAT (SDMMC)#\n"
                "#708298 STATUS:# #33d17a READ / WRITE READY#\n"
                "#708298 FREE SPACE:# #33d17a %lu KB# (%.2f MB)\n"
                "#708298 USED SPACE:# #ff5a2a %lu KB# (%.2f MB)\n"
                "#708298 TOTAL CAPACITY:# #f0f4f8 %lu KB# (%.2f MB)\n"
                "#708298 HARDWARE BUS:# #f0f4f8 1-Bit SDMMC (IO12/11/13)#",
                (unsigned long)(free_bytes / 1024), (float)free_bytes / (1024.0f * 1024.0f),
                (unsigned long)(s_used_bytes / 1024), (float)s_used_bytes / (1024.0f * 1024.0f),
                (unsigned long)(s_total_bytes / 1024), (float)s_total_bytes / (1024.0f * 1024.0f));
            lv_label_set_text(s_lbl_details, details_buf);

            lv_bar_set_value(s_bar_storage, arc_val, LV_ANIM_OFF);
        } else {
            lv_arc_set_value(s_arc_donut, 0);
            lv_label_set_text(s_lbl_donut_pct, "N/A");
            lv_label_set_text(s_lbl_donut_sub, "NO CARD");
            lv_label_set_text(s_lbl_status_val, "#708298 [ NO SD CARD DETECTED ]#");
            lv_label_set_text(s_lbl_details,
                "#708298 FORMAT:# #708298 UNMOUNTED#\n"
                "#708298 STATUS:# #ff5a2a NO TF CARD INSERTED#\n"
                "#708298 SLOT PINOUT:# #f0f4f8 CLK=12, CMD=11, D0=13#\n"
                "#708298 ACTION:# #38bfd8 INSERT TF CARD & TAP RESCAN#\n"
                "#708298 TOTAL CAPACITY:# #708298 0 KB#\n"
                "#708298 FREE SPACE:# #708298 0 KB#");
            lv_bar_set_value(s_bar_storage, 0, LV_ANIM_OFF);
        }
    }
}

static void switch_tab(storage_tab_t tab)
{
    s_active_tab = tab;

    if (s_active_tab == STORAGE_TAB_INTERNAL) {
        lv_obj_set_style_bg_color(s_btn_tab_internal, COLOR_ORANGE, 0);
        lv_obj_set_style_border_color(s_btn_tab_internal, COLOR_ORANGE, 0);
        lv_obj_set_style_text_color(s_lbl_tab_internal, COLOR_TEXT_WHITE, 0);

        lv_obj_set_style_bg_color(s_btn_tab_sdcard, COLOR_PANEL_BG, 0);
        lv_obj_set_style_border_color(s_btn_tab_sdcard, COLOR_BORDER_LINE, 0);
        lv_obj_set_style_text_color(s_lbl_tab_sdcard, COLOR_TEXT_MUTED, 0);
    } else {
        lv_obj_set_style_bg_color(s_btn_tab_sdcard, COLOR_ORANGE, 0);
        lv_obj_set_style_border_color(s_btn_tab_sdcard, COLOR_ORANGE, 0);
        lv_obj_set_style_text_color(s_lbl_tab_sdcard, COLOR_TEXT_WHITE, 0);

        lv_obj_set_style_bg_color(s_btn_tab_internal, COLOR_PANEL_BG, 0);
        lv_obj_set_style_border_color(s_btn_tab_internal, COLOR_BORDER_LINE, 0);
        lv_obj_set_style_text_color(s_lbl_tab_internal, COLOR_TEXT_MUTED, 0);
    }

    update_storage_display();
}

static void tab_btn_click_cb(lv_event_t *e)
{
    storage_tab_t tab = (storage_tab_t)(uintptr_t)lv_event_get_user_data(e);
    switch_tab(tab);
}

static void rescan_btn_click_cb(lv_event_t *e)
{
    (void)e;
    s_sd_stats_cached = false;
    if (!s_sd_mounted) {
        gui_storage_mount_sdcard();
    }
    update_storage_display();
}

static void open_explorer_btn_click_cb(lv_event_t *e)
{
    (void)e;
    gui_file_explorer_open(lv_scr_act());
}

static void btn_back_cb(lv_event_t *e)
{
    (void)e;
    if (s_view_storage) {
        lv_obj_del(s_view_storage);
        s_view_storage = NULL;
        s_btn_tab_internal = NULL;
        s_btn_tab_sdcard = NULL;
        s_lbl_tab_internal = NULL;
        s_lbl_tab_sdcard = NULL;
        s_arc_donut = NULL;
        s_lbl_donut_pct = NULL;
        s_lbl_donut_sub = NULL;
        s_lbl_status_val = NULL;
        s_lbl_details = NULL;
        s_bar_storage = NULL;
    }
}

void gui_storage_page_open(lv_obj_t *parent)
{
    if (s_view_storage) return;

    s_view_storage = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_set_size(s_view_storage, 480, 320);
    lv_obj_set_pos(s_view_storage, 0, 0);
    lv_obj_set_style_bg_color(s_view_storage, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_view_storage, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_view_storage, 0, 0);
    lv_obj_set_style_pad_all(s_view_storage, 0, 0);
    lv_obj_clear_flag(s_view_storage, LV_OBJ_FLAG_SCROLLABLE);

    /* Top Left Back Button */
    lv_obj_t *btn_back = lv_btn_create(s_view_storage);
    lv_obj_set_size(btn_back, 100, 28);
    lv_obj_set_pos(btn_back, 10, 8);
    lv_obj_set_style_bg_color(btn_back, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_back, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 2, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< SETTINGS");
    lv_obj_set_style_text_font(lbl_back, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_back, COLOR_ORANGE, 0);
    lv_obj_center(lbl_back);

    /* Header Title */
    lv_obj_t *lbl_title = lv_label_create(s_view_storage);
    lv_label_set_text(lbl_title, "STORAGE REPOSITORY\n// CAPACITY & FORMAT");
    lv_obj_set_style_text_font(lbl_title, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_title, 118, 4);
    lv_obj_set_width(lbl_title, 255);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_WRAP);

    /* Rescan Button on Top Right */
    lv_obj_t *btn_rescan = lv_btn_create(s_view_storage);
    lv_obj_set_size(btn_rescan, 85, 28);
    lv_obj_set_pos(btn_rescan, 385, 8);
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

    /* Segmented Drive Tabs */
    int tab_w = 225;
    int tab_h = 28;

    s_btn_tab_internal = lv_btn_create(s_view_storage);
    lv_obj_set_size(s_btn_tab_internal, tab_w, tab_h);
    lv_obj_set_pos(s_btn_tab_internal, 10, 42);
    lv_obj_set_style_radius(s_btn_tab_internal, 2, 0);
    lv_obj_set_style_border_width(s_btn_tab_internal, 1, 0);
    lv_obj_clear_flag(s_btn_tab_internal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_btn_tab_internal, tab_btn_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)STORAGE_TAB_INTERNAL);

    s_lbl_tab_internal = lv_label_create(s_btn_tab_internal);
    lv_label_set_text(s_lbl_tab_internal, LV_SYMBOL_DRIVE " INTERNAL (SPIFFS)");
    lv_obj_set_style_text_font(s_lbl_tab_internal, &font_orbitron_12, 0);
    lv_obj_center(s_lbl_tab_internal);

    s_btn_tab_sdcard = lv_btn_create(s_view_storage);
    lv_obj_set_size(s_btn_tab_sdcard, tab_w, tab_h);
    lv_obj_set_pos(s_btn_tab_sdcard, 245, 42);
    lv_obj_set_style_radius(s_btn_tab_sdcard, 2, 0);
    lv_obj_set_style_border_width(s_btn_tab_sdcard, 1, 0);
    lv_obj_clear_flag(s_btn_tab_sdcard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_btn_tab_sdcard, tab_btn_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)STORAGE_TAB_SDCARD);

    s_lbl_tab_sdcard = lv_label_create(s_btn_tab_sdcard);
    lv_label_set_text(s_lbl_tab_sdcard, LV_SYMBOL_SD_CARD " TF CARD (SDMMC)");
    lv_obj_set_style_text_font(s_lbl_tab_sdcard, &font_orbitron_12, 0);
    lv_obj_center(s_lbl_tab_sdcard);

    /* Main Container for Donut & Telemetry Details */
    lv_obj_t *cont = lv_obj_create(s_view_storage);
    lv_obj_set_size(cont, 460, 238);
    lv_obj_set_pos(cont, 10, 74);
    lv_obj_set_style_bg_color(cont, COLOR_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cont, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 3, 0);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Left Card: Circular Arc Donut Meter */
    lv_obj_t *donut_card = lv_obj_create(cont);
    lv_obj_set_size(donut_card, 145, 172);
    lv_obj_set_pos(donut_card, 4, 4);
    lv_obj_set_style_bg_color(donut_card, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(donut_card, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(donut_card, 1, 0);
    lv_obj_set_style_radius(donut_card, 2, 0);
    lv_obj_set_style_pad_all(donut_card, 0, 0);
    lv_obj_clear_flag(donut_card, LV_OBJ_FLAG_SCROLLABLE);

    s_arc_donut = lv_arc_create(donut_card);
    lv_obj_set_size(s_arc_donut, 120, 120);
    lv_obj_set_pos(s_arc_donut, 12, 10);
    lv_arc_set_rotation(s_arc_donut, 270);
    lv_arc_set_bg_angles(s_arc_donut, 0, 360);
    lv_arc_set_range(s_arc_donut, 0, 100);
    lv_obj_remove_style(s_arc_donut, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_donut, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_arc_color(s_arc_donut, lv_color_make(0x18, 0x24, 0x38), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_donut, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_donut, COLOR_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_donut, 12, LV_PART_INDICATOR);

    s_lbl_donut_pct = lv_label_create(donut_card);
    lv_label_set_text(s_lbl_donut_pct, "0%");
    lv_obj_set_style_text_font(s_lbl_donut_pct, &font_orbitron_24, 0);
    lv_obj_set_style_text_color(s_lbl_donut_pct, COLOR_TEXT_WHITE, 0);
    lv_obj_align(s_lbl_donut_pct, LV_ALIGN_CENTER, 0, -6);

    s_lbl_donut_sub = lv_label_create(donut_card);
    lv_label_set_text(s_lbl_donut_sub, "USED");
    lv_obj_set_style_text_font(s_lbl_donut_sub, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(s_lbl_donut_sub, COLOR_ORANGE, 0);
    lv_obj_align(s_lbl_donut_sub, LV_ALIGN_CENTER, 0, 18);

    lv_obj_t *lbl_donut_note = lv_label_create(donut_card);
    lv_label_set_text(lbl_donut_note, "ALLOCATION GAUGE");
    lv_obj_set_style_text_font(lbl_donut_note, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_donut_note, COLOR_TEXT_MUTED, 0);
    lv_obj_align(lbl_donut_note, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* Right Card: Telemetry & Detailed Space Metrics */
    lv_obj_t *detail_card = lv_obj_create(cont);
    lv_obj_set_size(detail_card, 285, 172);
    lv_obj_set_pos(detail_card, 155, 4);
    lv_obj_set_style_bg_color(detail_card, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(detail_card, COLOR_BORDER_LINE, 0);
    lv_obj_set_style_border_width(detail_card, 1, 0);
    lv_obj_set_style_radius(detail_card, 2, 0);
    lv_obj_set_style_pad_all(detail_card, 8, 0);
    lv_obj_clear_flag(detail_card, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_status_val = lv_label_create(detail_card);
    lv_label_set_recolor(s_lbl_status_val, true);
    lv_label_set_text(s_lbl_status_val, "#33d17a [ SCANNING DRIVE ]#");
    lv_obj_set_style_text_font(s_lbl_status_val, &font_orbitron_12, 0);
    lv_obj_set_pos(s_lbl_status_val, 4, 4);

    s_lbl_details = lv_label_create(detail_card);
    lv_label_set_recolor(s_lbl_details, true);
    lv_label_set_text(s_lbl_details, "READING HARDWARE REPOSITORY...");
    lv_obj_set_style_text_font(s_lbl_details, &font_orbitron_12, 0);
    lv_obj_set_pos(s_lbl_details, 4, 26);

    /* Bottom: Full Width Progress Bar & Explorer Navigation */
    s_bar_storage = lv_bar_create(cont);
    lv_obj_set_size(s_bar_storage, 300, 10);
    lv_obj_set_pos(s_bar_storage, 4, 184);
    lv_obj_set_style_bg_color(s_bar_storage, lv_color_make(0x18, 0x24, 0x38), LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_storage, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_storage, COLOR_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_storage, 2, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar_storage, 0, 100);

    /* Open Media Explorer Button */
    lv_obj_t *btn_open_exp = lv_btn_create(cont);
    lv_obj_set_size(btn_open_exp, 126, 32);
    lv_obj_set_pos(btn_open_exp, 314, 180);
    lv_obj_set_style_bg_color(btn_open_exp, COLOR_PANEL_BG, 0);
    lv_obj_set_style_border_color(btn_open_exp, COLOR_CYAN, 0);
    lv_obj_set_style_border_width(btn_open_exp, 1, 0);
    lv_obj_set_style_radius(btn_open_exp, 2, 0);
    lv_obj_clear_flag(btn_open_exp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_open_exp, open_explorer_btn_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_open_exp = lv_label_create(btn_open_exp);
    lv_label_set_text(lbl_open_exp, LV_SYMBOL_DIRECTORY " EXPLORER >");
    lv_obj_set_style_text_font(lbl_open_exp, &font_orbitron_12, 0);
    lv_obj_set_style_text_color(lbl_open_exp, COLOR_CYAN, 0);
    lv_obj_center(lbl_open_exp);

    switch_tab(STORAGE_TAB_INTERNAL);
}
