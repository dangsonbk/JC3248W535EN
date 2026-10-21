#include "screenshot_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <lvgl.h>
#include "esp_bsp.h"
#include "gui_storage_page.h"

static const char *TAG = "SCREENSHOT";

/* BMP File Structures (Packed) */
#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;          /* "BM" -> 0x4D42 */
    uint32_t bfSize;          /* File size in bytes */
    uint16_t bfReserved1;     /* 0 */
    uint16_t bfReserved2;     /* 0 */
    uint32_t bfOffBits;       /* Offset to pixel array (54) */
} bmp_file_header_t;

typedef struct {
    uint32_t biSize;          /* Header size (40) */
    int32_t  biWidth;         /* Image width */
    int32_t  biHeight;        /* Image height */
    uint16_t biPlanes;        /* Color planes (1) */
    uint16_t biBitCount;      /* Bit depth (24) */
    uint32_t biCompression;   /* Compression (0 = uncompressed BI_RGB) */
    uint32_t biSizeImage;     /* Image data size */
    int32_t  biXPelsPerMeter; /* 2835 */
    int32_t  biYPelsPerMeter; /* 2835 */
    uint32_t biClrUsed;       /* 0 */
    uint32_t biClrImportant;  /* 0 */
} bmp_info_header_t;
#pragma pack(pop)

static lv_timer_t *s_touch_timer = NULL;
static bool s_is_taking = false;

/* --------------------------------------------------------------------
 * Camera Flash & Toast Notification Animations
 * -------------------------------------------------------------------- */
static void show_camera_flash(void)
{
    lv_obj_t *flash = lv_obj_create(lv_layer_top());
    if (!flash) return;

    lv_obj_set_size(flash, 480, 320);
    lv_obj_set_pos(flash, 0, 0);
    lv_obj_set_style_bg_color(flash, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(flash, LV_OPA_80, 0);
    lv_obj_set_style_border_width(flash, 0, 0);
    lv_obj_set_style_radius(flash, 0, 0);
    lv_obj_clear_flag(flash, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_del_delayed(flash, 180);
}

static void show_toast_notification(const char *saved_path)
{
    lv_obj_t *toast = lv_obj_create(lv_layer_top());
    if (!toast) return;

    lv_obj_set_size(toast, 340, 36);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(toast, lv_color_make(0x06, 0x14, 0x0c), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
    lv_obj_set_style_border_color(toast, lv_color_make(0x00, 0xe6, 0x76), 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_radius(toast, 6, 0);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(toast);
    char buf[128];
    snprintf(buf, sizeof(buf), LV_SYMBOL_IMAGE " SAVED // %s", saved_path);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_make(0x00, 0xe6, 0x76), 0);
    lv_obj_center(lbl);

    lv_obj_del_delayed(toast, 2200);
}

/* --------------------------------------------------------------------
 * Find Next Available Screenshot Filename (8.3 DOS standard: snapXXXX.bmp)
 * -------------------------------------------------------------------- */
static void get_next_screenshot_path(char *out, size_t out_sz, const char *dir_prefix)
{
    for (int i = 1; i <= 9999; i++) {
        snprintf(out, out_sz, "%s/snap%04d.bmp", dir_prefix, i);
        if (access(out, F_OK) != 0) {
            return; // File does not exist, use this name
        }
    }
    snprintf(out, out_sz, "%s/snap9999.bmp", dir_prefix);
}

/* --------------------------------------------------------------------
 * Core Snapshot Implementation
 * -------------------------------------------------------------------- */
esp_err_t screenshot_take(char *out_path, size_t out_path_size)
{
    if (s_is_taking) return ESP_ERR_INVALID_STATE;
    s_is_taking = true;

    /* 1. Allocate snapshot buffer in PSRAM */
    const int w = 480;
    const int h = 320;
    const uint32_t buf_size = w * h * sizeof(lv_color_t); // 307.2 KB

    uint8_t *psram_buf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!psram_buf) {
        ESP_LOGE(TAG, "Cannot allocate PSRAM buffer for screenshot");
        s_is_taking = false;
        return ESP_ERR_NO_MEM;
    }

    /* 2. Lock LVGL display mutex & capture the screen */
    bsp_display_lock(0);

    lv_img_dsc_t snap_dsc;
    lv_res_t res = lv_snapshot_take_to_buf(lv_scr_act(), LV_IMG_CF_TRUE_COLOR, &snap_dsc, psram_buf, buf_size);

    if (res == LV_RES_OK) {
        show_camera_flash();
    }

    bsp_display_unlock();

    if (res != LV_RES_OK) {
        ESP_LOGE(TAG, "lv_snapshot_take_to_buf failed");
        free(psram_buf);
        s_is_taking = false;
        return ESP_FAIL;
    }

    /* 3. Determine storage destination with auto-mount and fallback */
    FILE *f = NULL;
    char filepath[128] = {0};

    /* Check SD Card mount status, attempt mount if not ready */
    if (!gui_storage_is_sdcard_mounted()) {
        gui_storage_mount_sdcard();
    }

    if (gui_storage_is_sdcard_mounted()) {
        get_next_screenshot_path(filepath, sizeof(filepath), "/sdcard");
        f = fopen(filepath, "wb");
        if (!f) {
            ESP_LOGW(TAG, "Cannot write to SD card %s (errno %d: %s). Trying /storage...",
                     filepath, errno, strerror(errno));
        }
    }

    /* Fallback to internal flash (/storage) if SD Card is unmounted or write failed */
    if (!f) {
        get_next_screenshot_path(filepath, sizeof(filepath), "/storage");
        f = fopen(filepath, "wb");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open file in /storage: %s (errno %d: %s)",
                     filepath, errno, strerror(errno));
            free(psram_buf);
            s_is_taking = false;
            return ESP_FAIL;
        }
    }

    if (out_path && out_path_size > 0) {
        strncpy(out_path, filepath, out_path_size - 1);
        out_path[out_path_size - 1] = '\0';
    }

    /* Row stride in bytes (480 pixels * 3 = 1440 bytes, multiple of 4, no padding) */
    const uint32_t row_stride = w * 3;
    const uint32_t image_data_size = row_stride * h;

    bmp_file_header_t fhdr;
    memset(&fhdr, 0, sizeof(fhdr));
    fhdr.bfType = 0x4D42; // "BM"
    fhdr.bfSize = sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t) + image_data_size;
    fhdr.bfOffBits = sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t);

    bmp_info_header_t ihdr;
    memset(&ihdr, 0, sizeof(ihdr));
    ihdr.biSize = sizeof(bmp_info_header_t);
    ihdr.biWidth = w;
    ihdr.biHeight = h; // positive height means bottom-to-top scanlines
    ihdr.biPlanes = 1;
    ihdr.biBitCount = 24; // 24-bit RGB
    ihdr.biCompression = 0; // BI_RGB (uncompressed)
    ihdr.biSizeImage = image_data_size;
    ihdr.biXPelsPerMeter = 2835;
    ihdr.biYPelsPerMeter = 2835;

    fwrite(&fhdr, 1, sizeof(fhdr), f);
    fwrite(&ihdr, 1, sizeof(ihdr), f);

    /* Allocate one row buffer */
    uint8_t *row_buf = (uint8_t *)malloc(row_stride);
    if (!row_buf) {
        fclose(f);
        free(psram_buf);
        s_is_taking = false;
        return ESP_ERR_NO_MEM;
    }

    /* Write rows from bottom (h - 1) to top (0) as required by standard BMP */
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint16_t pix = ((const uint16_t *)snap_dsc.data)[y * w + x];
#if LV_COLOR_16_SWAP
            pix = __builtin_bswap16(pix);
#endif
            uint8_t r = (uint8_t)(((pix >> 11) & 0x1F) << 3);
            uint8_t g = (uint8_t)(((pix >> 5) & 0x3F) << 2);
            uint8_t b = (uint8_t)((pix & 0x1F) << 3);

            /* BMP stores channels in Blue, Green, Red order */
            row_buf[x * 3 + 0] = b;
            row_buf[x * 3 + 1] = g;
            row_buf[x * 3 + 2] = r;
        }
        fwrite(row_buf, 1, row_stride, f);
    }

    free(row_buf);
    fclose(f);
    free(psram_buf);

    /* 5. Show toast notification on screen */
    bsp_display_lock(0);
    show_toast_notification(filepath);
    bsp_display_unlock();

    /* 6. Print Serial feedback */
    printf("\r\n===================================================================\r\n");
    printf("[SCREENSHOT] Successfully captured active screen (480x320 24-bit BMP)\r\n");
    printf("[SCREENSHOT] Saved to: %s (%lu bytes)\r\n", filepath, (unsigned long)fhdr.bfSize);
    printf("===================================================================\r\n\n");

    s_is_taking = false;
    return ESP_OK;
}

/* --------------------------------------------------------------------
 * Touch Gesture Listener (Hold Top-Right Corner for 1.5s)
 * -------------------------------------------------------------------- */
static void touch_check_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_is_taking) return;

    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    static int s_hold_ticks = 0;

    /* Top-right corner zone: x >= 420 && y <= 50 */
    if (indev->proc.state == LV_INDEV_STATE_PRESSED) {
        if (pt.x >= 420 && pt.y <= 50) {
            s_hold_ticks++;
            if (s_hold_ticks == 15) { // 15 ticks * 100ms = 1.5 seconds!
                char path[128];
                screenshot_take(path, sizeof(path));
            }
        } else {
            s_hold_ticks = 0;
        }
    } else {
        s_hold_ticks = 0;
    }
}

/* --------------------------------------------------------------------
 * Serial UART Command Listener Task (Core 0)
 * -------------------------------------------------------------------- */
static void serial_listener_task(void *pvParam)
{
    (void)pvParam;

    /* Ensure UART0 driver is installed */
    if (!uart_is_driver_installed(UART_NUM_0)) {
        uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    }

    ESP_LOGI(TAG, "Screenshot Serial listener active. Type 'screenshot' or 'cap' to capture.");

    char cmd_buf[64];
    int cmd_pos = 0;

    while (1) {
        uint8_t ch = 0;
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            if (ch == '\r' || ch == '\n') {
                if (cmd_pos > 0) {
                    cmd_buf[cmd_pos] = '\0';
                    cmd_pos = 0;

                    if (strcasecmp(cmd_buf, "screenshot") == 0 ||
                        strcasecmp(cmd_buf, "cap") == 0 ||
                        strcasecmp(cmd_buf, "snap") == 0) {
                        char path[128] = {0};
                        screenshot_take(path, sizeof(path));
                    }
                }
            } else if (cmd_pos < (int)sizeof(cmd_buf) - 1) {
                if (ch >= 0x20 && ch <= 0x7E) {
                    cmd_buf[cmd_pos++] = (char)ch;
                }
            }
        }
    }
}

/* --------------------------------------------------------------------
 * Subsystem Initialization
 * -------------------------------------------------------------------- */
esp_err_t screenshot_service_init(void)
{
    /* 1. Start Touch Watcher Timer in LVGL (running every 100ms) */
    if (!s_touch_timer) {
        s_touch_timer = lv_timer_create(touch_check_timer_cb, 100, NULL);
    }

    /* 2. Start Serial Console Command Task on Core 0 */
    xTaskCreatePinnedToCore(
        serial_listener_task,
        "snap_serial",
        4096,
        NULL,
        2,
        NULL,
        0 // Core 0
    );

    ESP_LOGI(TAG, "Screenshot service initialized (Touch corner hold + Serial command)");
    return ESP_OK;
}
