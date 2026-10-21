#include "gui_screensaver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_bsp.h"

static const char *TAG = "SCREENSAVER";

static lv_obj_t *s_saver_obj = NULL;
static lv_obj_t *s_canvas = NULL;
static lv_timer_t *s_saver_timer = NULL;
static lv_color_t *s_canvas_buf = NULL;
static screensaver_effect_t s_current_fx = SCREENSAVER_FX_MATRIX;
static bool s_is_active = false;

/* --------------------------------------------------------------------
 * Effect 1: Cinematic Matrix Rain with Continuous Character Mutation
 * 32 Custom 5x7 Katakana / Hex / Rune Bitmaps cascading down in 30 columns
 * -------------------------------------------------------------------- */
#define MATRIX_COLS 30
#define MATRIX_ROWS 20

static const uint8_t s_matrix_glyphs[32][7] = {
    { 0x1F, 0x11, 0x1F, 0x11, 0x1F, 0x00, 0x00 }, /* 0: Katakana 日 (Hi) */
    { 0x10, 0x12, 0x12, 0x14, 0x18, 0x10, 0x00 }, /* 1: Katakana ﾊ (Ha) */
    { 0x1C, 0x00, 0x0E, 0x00, 0x07, 0x00, 0x00 }, /* 2: Katakana ﾐ (Mi) */
    { 0x11, 0x11, 0x1F, 0x01, 0x01, 0x00, 0x00 }, /* 3: Katakana ﾋ (Hi) */
    { 0x04, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x00 }, /* 4: Katakana ｳ (U) */
    { 0x10, 0x08, 0x10, 0x02, 0x05, 0x08, 0x10 }, /* 5: Katakana ｼ (Shi) */
    { 0x04, 0x1F, 0x04, 0x04, 0x0A, 0x11, 0x00 }, /* 6: Katakana ﾅ (Na) */
    { 0x04, 0x1F, 0x04, 0x1F, 0x04, 0x05, 0x06 }, /* 7: Katakana ﾓ (Mo) */
    { 0x1F, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }, /* 8: Katakana ﾆ (Ni) */
    { 0x1F, 0x0A, 0x0A, 0x04, 0x08, 0x10, 0x00 }, /* 9: Katakana ｻ (Sa) */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x00, 0x00 }, /* 10: Katakana ﾜ (Wa) */
    { 0x11, 0x09, 0x04, 0x02, 0x01, 0x00, 0x00 }, /* 11: Katakana ﾂ (Tsu) */
    { 0x04, 0x1F, 0x04, 0x0E, 0x15, 0x04, 0x00 }, /* 12: Katakana ｵ (O) */
    { 0x12, 0x12, 0x12, 0x02, 0x04, 0x08, 0x00 }, /* 13: Katakana ﾘ (Ri) */
    { 0x1F, 0x02, 0x04, 0x0E, 0x12, 0x12, 0x00 }, /* 14: Katakana ｱ (A) */
    { 0x04, 0x1F, 0x04, 0x15, 0x15, 0x15, 0x00 }, /* 15: Katakana ﾎ (Ho) */
    { 0x1F, 0x04, 0x04, 0x02, 0x01, 0x00, 0x00 }, /* 16: Katakana ﾃ (Te) */
    { 0x1F, 0x02, 0x1F, 0x02, 0x04, 0x08, 0x00 }, /* 17: Katakana ﾏ (Ma) */
    { 0x12, 0x12, 0x1F, 0x02, 0x04, 0x08, 0x00 }, /* 18: Katakana ｹ (Ke) */
    { 0x09, 0x06, 0x1F, 0x06, 0x09, 0x10, 0x00 }, /* 19: Katakana ﾒ (Me) */
    { 0x1F, 0x04, 0x04, 0x04, 0x1F, 0x00, 0x00 }, /* 20: Katakana ｴ (E) */
    { 0x17, 0x15, 0x1D, 0x15, 0x12, 0x00, 0x00 }, /* 21: Katakana ｶ (Ka) */
    { 0x1F, 0x04, 0x1F, 0x04, 0x0A, 0x11, 0x00 }, /* 22: Katakana ｷ (Ki) */
    { 0x02, 0x04, 0x08, 0x1F, 0x02, 0x04, 0x00 }, /* 23: Katakana ﾑ (Mu) */
    { 0x11, 0x11, 0x1F, 0x04, 0x04, 0x00, 0x00 }, /* 24: Katakana ﾕ (Yu) */
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x00, 0x00 }, /* 25: Katakana ﾗ (Ra) */
    { 0x1F, 0x12, 0x12, 0x1F, 0x02, 0x00, 0x00 }, /* 26: Katakana ｾ (Se) */
    { 0x12, 0x1F, 0x12, 0x1A, 0x16, 0x12, 0x00 }, /* 27: Katakana ﾈ (Ne) */
    { 0x1F, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00 }, /* 28: Katakana ｽ (Su) */
    { 0x1F, 0x02, 0x04, 0x08, 0x08, 0x00, 0x00 }, /* 29: Matrix Digit 7 */
    { 0x04, 0x0E, 0x1F, 0x04, 0x04, 0x00, 0x00 }, /* 30: Rune Cross */
    { 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00, 0x00 }  /* 31: Cyber Rune Z */
};

typedef struct {
    uint8_t glyph;
    uint8_t brightness;      /* 0: black, 1: dim, 2: dark, 3: mid, 4: bright, 5: head white */
    uint8_t prev_brightness;
    uint8_t prev_glyph;
} matrix_cell_t;

static matrix_cell_t s_matrix_grid[MATRIX_COLS][MATRIX_ROWS];
static int8_t s_matrix_drops[MATRIX_COLS];
static uint8_t s_matrix_speeds[MATRIX_COLS];
static uint8_t s_matrix_lengths[MATRIX_COLS];
static uint8_t s_matrix_tick = 0;

static void matrix_init(void)
{
    memset(s_matrix_grid, 0, sizeof(s_matrix_grid));
    for (int i = 0; i < MATRIX_COLS; i++) {
        s_matrix_drops[i] = -(rand() % 20);
        s_matrix_speeds[i] = 1 + (rand() % 2);
        s_matrix_lengths[i] = 8 + (rand() % 10);
    }
    s_matrix_tick = 0;
}

static void matrix_step(void)
{
    if (!s_canvas_buf) return;
    s_matrix_tick++;

    /* 1. Advance Column Stream Heads */
    for (int c = 0; c < MATRIX_COLS; c++) {
        if ((s_matrix_tick % s_matrix_speeds[c]) != 0) continue;

        s_matrix_drops[c]++;
        int head_y = s_matrix_drops[c];

        if (head_y >= 0 && head_y < MATRIX_ROWS) {
            s_matrix_grid[c][head_y].glyph = rand() % 32;
            s_matrix_grid[c][head_y].brightness = 5; /* Glowing White Head */
        }
        if (head_y - 1 >= 0 && head_y - 1 < MATRIX_ROWS) {
            if (s_matrix_grid[c][head_y - 1].brightness == 5) {
                s_matrix_grid[c][head_y - 1].brightness = 4; /* Transitions to Electric Green */
            }
        }
        if (head_y - s_matrix_lengths[c] >= MATRIX_ROWS) {
            /* Reset column stream */
            s_matrix_drops[c] = -(rand() % 16);
            s_matrix_speeds[c] = 1 + (rand() % 2);
            s_matrix_lengths[c] = 8 + (rand() % 10);
        }
    }

    /* 2. Gradual Organic Trail Decay */
    for (int c = 0; c < MATRIX_COLS; c++) {
        for (int r = 0; r < MATRIX_ROWS; r++) {
            uint8_t b = s_matrix_grid[c][r].brightness;
            if (b > 0 && b < 5) {
                if ((rand() % 3) == 0) {
                    s_matrix_grid[c][r].brightness = b - 1;
                }
            }
        }
    }

    /* 3. Continuous Character Mutation (The Iconic Movie Matrix Effect)
     * Randomly morph ~20% of active visible characters in place every frame */
    for (int c = 0; c < MATRIX_COLS; c++) {
        for (int r = 0; r < MATRIX_ROWS; r++) {
            if (s_matrix_grid[c][r].brightness > 0) {
                if ((rand() % 5) == 0) {
                    s_matrix_grid[c][r].glyph = rand() % 32;
                }
            }
        }
    }

    /* 4. Render Changed Cells Directly to Canvas */
    for (int c = 0; c < MATRIX_COLS; c++) {
        int px_x = c * 16;
        for (int r = 0; r < MATRIX_ROWS; r++) {
            matrix_cell_t *cell = &s_matrix_grid[c][r];

            if (cell->brightness == cell->prev_brightness && cell->glyph == cell->prev_glyph) {
                continue;
            }
            cell->prev_brightness = cell->brightness;
            cell->prev_glyph = cell->glyph;

            int px_y = r * 16;

            /* Clear cell bounding box (16x16) to pure black */
            for (int dy = 0; dy < 16; dy++) {
                int row_idx = (px_y + dy) * 480 + px_x;
                for (int dx = 0; dx < 16; dx++) {
                    s_canvas_buf[row_idx + dx].full = 0;
                }
            }

            if (cell->brightness == 0) continue;

            /* Select color palette according to luminescence stage */
            uint16_t color;
            switch (cell->brightness) {
                case 5: color = 0xFFFF; break; /* White / Mint Head */
                case 4: color = 0x37E6; break; /* Electric Matrix Green */
                case 3: color = 0x07E0; break; /* Classic Neon Green */
                case 2: color = 0x04E0; break; /* Phosphor Dark Green */
                default: color = 0x0280; break; /* Ghost Faint Green */
            }
#if LV_COLOR_16_SWAP
            color = __builtin_bswap16(color);
#endif

            /* Draw 5x7 glyph scaled 2x2 (10x14 px) centered in 16x16 cell */
            const uint8_t *glyph_data = s_matrix_glyphs[cell->glyph];
            for (int gy = 0; gy < 7; gy++) {
                uint8_t row_bits = glyph_data[gy];
                int base_y = px_y + 1 + gy * 2;
                for (int gx = 0; gx < 5; gx++) {
                    if (row_bits & (0x10 >> gx)) {
                        int base_x = px_x + 3 + gx * 2;
                        s_canvas_buf[base_y * 480 + base_x].full = color;
                        s_canvas_buf[base_y * 480 + (base_x + 1)].full = color;
                        s_canvas_buf[(base_y + 1) * 480 + base_x].full = color;
                        s_canvas_buf[(base_y + 1) * 480 + (base_x + 1)].full = color;
                    }
                }
            }
        }
    }

    lv_obj_invalidate(s_canvas);
}

/* --------------------------------------------------------------------
 * Effect 2: Conway's Game of Life (48x32 grid)
 * -------------------------------------------------------------------- */
#define LIFE_W 48
#define LIFE_H 32
static uint8_t s_life_grid[LIFE_W * LIFE_H];
static uint8_t s_life_next[LIFE_W * LIFE_H];
static uint16_t s_life_gens = 0;

static void life_seed(void)
{
    memset(s_life_grid, 0, sizeof(s_life_grid));
    s_life_gens = 0;
    for (int y = 0; y < LIFE_H; y++) {
        for (int x = 0; x < LIFE_W; x++) {
            if ((rand() % 100) < 22) {
                s_life_grid[y * LIFE_W + x] = 1;
            }
        }
    }
}

static void life_step(void)
{
    if (!s_canvas_buf) return;

    int live_count = 0;
    for (int y = 0; y < LIFE_H; y++) {
        for (int x = 0; x < LIFE_W; x++) {
            /* Count 8 neighbors with toroidal wrapping */
            int neighbors = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = (x + dx + LIFE_W) % LIFE_W;
                    int ny = (y + dy + LIFE_H) % LIFE_H;
                    if (s_life_grid[ny * LIFE_W + nx]) neighbors++;
                }
            }

            int idx = y * LIFE_W + x;
            uint8_t curr = s_life_grid[idx];
            uint8_t nxt = 0;
            if (curr && (neighbors == 2 || neighbors == 3)) {
                nxt = 1;
            } else if (!curr && neighbors == 3) {
                nxt = 1;
            }
            s_life_next[idx] = nxt;
            if (nxt) live_count++;
        }
    }

    memcpy(s_life_grid, s_life_next, sizeof(s_life_grid));
    s_life_gens++;

    /* Reseed if population dies or stagnates after 350 generations */
    if (live_count < 15 || s_life_gens > 350) {
        life_seed();
    }

    /* Render Life Grid to Canvas (each cell is 10x10 px) */
    memset(s_canvas_buf, 0, 480 * 320 * sizeof(lv_color_t));
    for (int y = 0; y < LIFE_H; y++) {
        for (int x = 0; x < LIFE_W; x++) {
            if (!s_life_grid[y * LIFE_W + x]) continue;

            uint16_t c = (x % 2 == 0) ? 0x07FD : 0x07E0; // Mint Cyan & Neon Green
#if LV_COLOR_16_SWAP
            c = __builtin_bswap16(c);
#endif
            int px_x = x * 10 + 1;
            int px_y = y * 10 + 1;
            for (int cy = 0; cy < 8; cy++) {
                for (int cx = 0; cx < 8; cx++) {
                    int pidx = (px_y + cy) * 480 + (px_x + cx);
                    if (pidx >= 0 && pidx < 480 * 320) {
                        s_canvas_buf[pidx].full = c;
                    }
                }
            }
        }
    }
    lv_obj_invalidate(s_canvas);
}

/* --------------------------------------------------------------------
 * Effect 3: Sci-Fi Starfield Warp (60 stars)
 * -------------------------------------------------------------------- */
#define STAR_COUNT 60
typedef struct {
    float x, y, z;
} star_t;
static star_t s_stars[STAR_COUNT];

static void stars_init(void)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        s_stars[i].x = (float)((rand() % 480) - 240);
        s_stars[i].y = (float)((rand() % 320) - 160);
        s_stars[i].z = (float)(1 + (rand() % 250));
    }
}

static void stars_step(void)
{
    if (!s_canvas_buf) return;

    /* Trail fade */
    for (int i = 0; i < 480 * 320; i++) {
        uint16_t c = s_canvas_buf[i].full;
#if LV_COLOR_16_SWAP
        c = __builtin_bswap16(c);
#endif
        uint16_t r = ((c >> 11) & 0x1F) * 3 / 4;
        uint16_t g = ((c >> 5) & 0x3F) * 3 / 4;
        uint16_t b = (c & 0x1F) * 3 / 4;
        uint16_t dim = (r << 11) | (g << 5) | b;
#if LV_COLOR_16_SWAP
        dim = __builtin_bswap16(dim);
#endif
        s_canvas_buf[i].full = dim;
    }

    for (int i = 0; i < STAR_COUNT; i++) {
        s_stars[i].z -= 3.5f;
        if (s_stars[i].z <= 1.0f) {
            s_stars[i].x = (float)((rand() % 480) - 240);
            s_stars[i].y = (float)((rand() % 320) - 160);
            s_stars[i].z = 250.0f;
        }

        int sx = (int)(240.0f + (s_stars[i].x * 140.0f) / s_stars[i].z);
        int sy = (int)(160.0f + (s_stars[i].y * 140.0f) / s_stars[i].z);

        if (sx >= 1 && sx < 479 && sy >= 1 && sy < 319) {
            uint16_t col = 0xFFFF; // White
            if (s_stars[i].z > 150) col = 0x5ADF; // Cyan Muted
#if LV_COLOR_16_SWAP
            col = __builtin_bswap16(col);
#endif
            s_canvas_buf[sy * 480 + sx].full = col;
            s_canvas_buf[sy * 480 + sx + 1].full = col;
            s_canvas_buf[(sy + 1) * 480 + sx].full = col;
        } else {
            s_stars[i].z = 0; // Trigger reset
        }
    }
    lv_obj_invalidate(s_canvas);
}

/* --------------------------------------------------------------------
 * Screen Saver Timer Callback
 * -------------------------------------------------------------------- */
static void saver_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_is_active || !s_canvas_buf) return;

    switch (s_current_fx) {
        case SCREENSAVER_FX_MATRIX:
            matrix_step();
            break;
        case SCREENSAVER_FX_LIFE:
            life_step();
            break;
        case SCREENSAVER_FX_STARS:
        default:
            stars_step();
            break;
    }
}

static void saver_tap_cb(lv_event_t *e)
{
    (void)e;
    gui_screensaver_stop();
}

/* --------------------------------------------------------------------
 * Public Controls
 * -------------------------------------------------------------------- */
void gui_screensaver_start(screensaver_effect_t fx)
{
    if (s_is_active) return;
    s_current_fx = fx;

    /* Allocate 480x320 16-bit canvas in 8MB PSRAM */
    const size_t buf_sz = 480 * 320 * sizeof(lv_color_t);
    s_canvas_buf = (lv_color_t *)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM for screensaver");
        return;
    }
    memset(s_canvas_buf, 0, buf_sz);

    /* Fullscreen container on top layer */
    s_saver_obj = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_saver_obj, 480, 320);
    lv_obj_set_pos(s_saver_obj, 0, 0);
    lv_obj_set_style_bg_color(s_saver_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_saver_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_saver_obj, 0, 0);
    lv_obj_set_style_pad_all(s_saver_obj, 0, 0);
    lv_obj_clear_flag(s_saver_obj, LV_OBJ_FLAG_SCROLLABLE);

    /* Canvas */
    s_canvas = lv_canvas_create(s_saver_obj);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, 480, 320, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(s_canvas);

    /* HUD Watermark hint */
    lv_obj_t *lbl_hint = lv_label_create(s_saver_obj);
    lv_label_set_text(lbl_hint, "[ TOUCH ANYWHERE TO WAKE ]");
    lv_obj_set_style_text_color(lbl_hint, lv_color_make(0x40, 0x50, 0x60), 0);
    lv_obj_align(lbl_hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* Tap anywhere to dismiss */
    lv_obj_add_event_cb(s_saver_obj, saver_tap_cb, LV_EVENT_CLICKED, NULL);

    /* Initialize selected effect */
    switch (s_current_fx) {
        case SCREENSAVER_FX_MATRIX:
            matrix_init();
            s_saver_timer = lv_timer_create(saver_timer_cb, 50, NULL);
            break;
        case SCREENSAVER_FX_LIFE:
            life_seed();
            s_saver_timer = lv_timer_create(saver_timer_cb, 100, NULL);
            break;
        case SCREENSAVER_FX_STARS:
        default:
            stars_init();
            s_saver_timer = lv_timer_create(saver_timer_cb, 40, NULL);
            break;
    }

    s_is_active = true;
    ESP_LOGI(TAG, "Screen saver started (effect: %d)", fx);
}

void gui_screensaver_stop(void)
{
    if (!s_is_active) return;
    s_is_active = false;

    if (s_saver_timer) {
        lv_timer_del(s_saver_timer);
        s_saver_timer = NULL;
    }
    if (s_saver_obj && lv_obj_is_valid(s_saver_obj)) {
        lv_obj_del(s_saver_obj);
        s_saver_obj = NULL;
        s_canvas = NULL;
    }
    if (s_canvas_buf) {
        free(s_canvas_buf);
        s_canvas_buf = NULL;
    }

    lv_disp_trig_activity(NULL);
    ESP_LOGI(TAG, "Screen saver dismissed");
}

bool gui_screensaver_is_active(void)
{
    return s_is_active;
}

const char *gui_screensaver_get_fx_name(screensaver_effect_t fx)
{
    switch (fx) {
        case SCREENSAVER_FX_MATRIX: return "MATRIX RAIN";
        case SCREENSAVER_FX_LIFE:   return "GAME OF LIFE";
        case SCREENSAVER_FX_STARS:  return "STARFIELD WARP";
        default:                    return "UNKNOWN";
    }
}
