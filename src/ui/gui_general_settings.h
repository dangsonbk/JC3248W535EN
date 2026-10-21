#ifndef GUI_GENERAL_SETTINGS_H
#define GUI_GENERAL_SETTINGS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open General Settings sub-page inside Settings
 * @param parent The parent container (view_settings)
 */
void gui_general_settings_open(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif // GUI_GENERAL_SETTINGS_H
