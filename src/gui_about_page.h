#ifndef GUI_ABOUT_PAGE_H
#define GUI_ABOUT_PAGE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the About & Mission Identity Settings subpage
 * @param parent Parent view (view_settings)
 */
void gui_about_page_open(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif // GUI_ABOUT_PAGE_H
