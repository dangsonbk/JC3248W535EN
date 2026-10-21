#ifndef GUI_POWER_SETTINGS_H
#define GUI_POWER_SETTINGS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open Power Management settings page
 * @param parent The parent container (view_settings)
 */
void gui_power_settings_open(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif // GUI_POWER_SETTINGS_H
