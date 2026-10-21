#ifndef GUI_FILE_EXPLORER_H
#define GUI_FILE_EXPLORER_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the Dedicated File Explorer Application
 * @param parent Parent LVGL object (usually active screen)
 */
void gui_file_explorer_open(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif // GUI_FILE_EXPLORER_H
