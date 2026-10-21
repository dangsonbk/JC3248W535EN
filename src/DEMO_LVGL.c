
// #include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include <esp_log.h>   // Add this line to include the header file that declares ESP_LOGI
#include <esp_flash.h> // Add this line to include the header file that declares esp_flash_t
#include <esp_chip_info.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "weather_service.h"

static const char *TAG = "DEMO_LVGL";

#define BUILD (String(__DATE__) + " - " + String(__TIME__)).c_str()

#define logSection(section) \
  ESP_LOGI(TAG, "\n\n************* %s **************\n", section);

/**
 * @brief LVGL porting example
 * Set the rotation degree:
 *      - 0: 0 degree
 *      - 90: 90 degree
 *      - 180: 180 degree
 *      - 270: 270 degree
 *
 */
#define LVGL_PORT_ROTATION_DEGREE (90)

#include "gui_launcher.h"
#include "wifi_controller.h"
#include "sys_log.h"
#include "gui_display_settings.h"
#include "gui_storage_page.h"
#include "gui_boot_screen.h"
#include "screenshot_service.h"
#include "audio_player.h"
#include "clock_service.h"
#include "ui_frame.h"

void setup();

#if !CONFIG_AUTOSTART_ARDUINO
void app_main()
{
  // initialize arduino library before we start the tasks
  // initArduino();

  setup();
}
#endif
static void on_boot_complete(void)
{
  ESP_LOGI(TAG, "Boot sequence completed. Loading Mission Control HUD.");
  gui_launcher_init();
}

void setup()
{
  logSection("LVGL porting example start");
  esp_chip_info_t chip_info;
  uint32_t flash_size;
  esp_chip_info(&chip_info);
  ESP_LOGI(TAG, "This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

  unsigned major_rev = chip_info.revision / 100;
  unsigned minor_rev = chip_info.revision % 100;
  ESP_LOGI(TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);
  if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
  {
    ESP_LOGI(TAG, "Get flash size failed");
    return;
  }

  ESP_LOGI(TAG, "%" PRIu32 "MB %s flash", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

  ESP_LOGI(TAG, "Minimum free heap size: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());
  size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(TAG, "Free PSRAM: %d bytes", freePsram);
  logSection("Initialize panel device");
  // ESP_LOGI(TAG, "Initialize panel device");
  lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  port_cfg.task_affinity = 1;       // Pin LVGL task to Core 1 (leaving Core 0 for system/wifi/audio)
  port_cfg.task_priority = 6;       // Elevated priority above background workers
  port_cfg.task_stack = 8192;       // Ensure sufficient stack for VFS, file I/O and LVGL modals
  port_cfg.task_max_sleep_ms = 10;  // Cap idle sleep to 10ms (eliminates sluggish 500ms touch delay)
  port_cfg.timer_period_ms = 2;     // 2ms high precision tick timer

  bsp_display_cfg_t cfg = {
      .lvgl_port_cfg = port_cfg,
      .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
#if LVGL_PORT_ROTATION_DEGREE == 90
      .rotate = LV_DISP_ROT_90,
#elif LVGL_PORT_ROTATION_DEGREE == 270
      .rotate = LV_DISP_ROT_270,
#elif LVGL_PORT_ROTATION_DEGREE == 180
      .rotate = LV_DISP_ROT_180,
#elif LVGL_PORT_ROTATION_DEGREE == 0
      .rotate = LV_DISP_ROT_NONE,
#endif
  };

  bsp_display_start_with_config(&cfg);
  bsp_display_backlight_on();

  logSection("Create UI");
  /* Lock the mutex due to the LVGL APIs are not thread-safe */
  bsp_display_lock(0);

  /* Initialize Centralized UI Frame & Navigation Stack */
  ui_frame_init();

  /* Initialize Display & Sleep Subsystem (loads brightness & sleep watchdog) */
  gui_display_settings_init();

  /* Initialize Storage subsystem */
  gui_storage_init();

  /* Initialize System Log subsystem */
  sys_log_init();

  /* Initialize Audio Player I2S hardware early to reserve GDMA interrupt before Wi-Fi */
  audio_player_init();

  /* Initialize Wi-Fi Controller worker on Core 0 */
  wifi_controller_init();

  /* Initialize Weather Service worker on Core 0 */
  weather_service_init();

  /* Initialize Screenshot Service (Serial command + Touch hold) */
  screenshot_service_init();

  /* Initialize Clock & Alarm subsystem (NTP sync, world time, background alarm monitor) */
  clock_service_init();

  /* Start Cyberpunk Root Access Boot Sequence, then load Launcher */
  gui_boot_screen_start(on_boot_complete);

  /* Release the mutex */
  bsp_display_unlock();

  logSection("LVGL porting example end");
}

void loop()
{
  ESP_LOGI(TAG, "IDLE loop");
  // delay(1000);
}