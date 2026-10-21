#ifndef GUI_STORAGE_PAGE_H
#define GUI_STORAGE_PAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Initialize storage systems (Internal SPIFFS / LittleFS and SD card).
 */
void gui_storage_init(void);

/**
 * @brief Mount SD Card or verify mount status.
 */
esp_err_t gui_storage_mount_sdcard(void);
bool gui_storage_is_sdcard_mounted(void);

/**
 * @brief Open the Storage Explorer subpage.
 * @param parent Parent LVGL view (view_settings)
 */
void gui_storage_page_open(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif // GUI_STORAGE_PAGE_H
