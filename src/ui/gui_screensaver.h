#ifndef GUI_SCREENSAVER_H
#define GUI_SCREENSAVER_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREENSAVER_FX_MATRIX = 0,
    SCREENSAVER_FX_LIFE   = 1,
    SCREENSAVER_FX_STARS  = 2,
    SCREENSAVER_FX_COUNT
} screensaver_effect_t;

/**
 * @brief Start the screensaver overlay with the specified effect
 * @param fx The effect to run (Matrix, Game of Life, Starfield)
 */
void gui_screensaver_start(screensaver_effect_t fx);

/**
 * @brief Stop the active screensaver and return to normal UI
 */
void gui_screensaver_stop(void);

/**
 * @brief Check if screensaver is currently active
 */
bool gui_screensaver_is_active(void);

/**
 * @brief Get human-readable name of effect
 */
const char *gui_screensaver_get_fx_name(screensaver_effect_t fx);

#ifdef __cplusplus
}
#endif

#endif // GUI_SCREENSAVER_H
