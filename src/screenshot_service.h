#ifndef SCREENSHOT_SERVICE_H
#define SCREENSHOT_SERVICE_H

#include <esp_err.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize screenshot subsystem (serial console listener & touch gesture watcher)
 */
esp_err_t screenshot_service_init(void);

/**
 * @brief Manually capture current screen and save to SD card or Internal Storage as 24-bit BMP
 * @param out_path Optional buffer to receive the saved file path (can be NULL)
 * @param out_path_size Size of out_path buffer
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t screenshot_take(char *out_path, size_t out_path_size);

#ifdef __cplusplus
}
#endif

#endif // SCREENSHOT_SERVICE_H
