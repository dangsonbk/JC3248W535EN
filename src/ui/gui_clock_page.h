#ifndef GUI_CLOCK_PAGE_H
#define GUI_CLOCK_PAGE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the minimalist Cyberpunk Digital Clock & Alarm application
 * @param parent Optional parent container (defaults to active screen)
 */
void gui_clock_page_open(lv_obj_t *parent);

/**
 * @brief Close the clock page
 */
void gui_clock_page_close(void);

#ifdef __cplusplus
}
#endif

#endif // GUI_CLOCK_PAGE_H
