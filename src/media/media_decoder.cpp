#include "media_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "jpegdec/JPEGDEC.h"

static const char *TAG = "MEDIA_DECODER";

struct JpegDrawContext {
    uint16_t *dest;
    int max_w;
    int max_h;
    int stride;
    int actual_w;
    int actual_h;
};

static int jpeg_draw_callback(JPEGDRAW *pDraw)
{
    JpegDrawContext *ctx = (JpegDrawContext *)pDraw->pUser;
    if (!ctx || !ctx->dest) return 0;

    uint16_t *src = (uint16_t *)pDraw->pPixels;
    for (int y = 0; y < pDraw->iHeight; y++) {
        int dst_y = pDraw->y + y;
        if (dst_y >= ctx->max_h) break;

        int dst_x = pDraw->x;
        int len = pDraw->iWidth;
        if (dst_x + len > ctx->max_w) len = ctx->max_w - dst_x;

        if (len > 0) {
            uint16_t *dst_ptr = ctx->dest + (dst_y * ctx->stride) + dst_x;
            uint16_t *src_ptr = src + (y * pDraw->iWidth);
            memcpy(dst_ptr, src_ptr, len * sizeof(uint16_t));
        }
    }
    return 1;
}

/* --------------------------------------------------------------------
 * BMP Image Decoder
 * -------------------------------------------------------------------- */
static bool decode_bmp(FILE *f, uint16_t *out_rgb565, int max_w, int max_h, int *out_w, int *out_h)
{
    uint8_t header[54];
    if (fread(header, 1, 54, f) < 54) return false;

    if (header[0] != 'B' || header[1] != 'M') return false;

    uint32_t data_offset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
    int32_t width = (int32_t)(header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24));
    int32_t height = (int32_t)(header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24));
    uint16_t bpp = header[28] | (header[29] << 8);

    if (width <= 0 || height == 0 || (bpp != 16 && bpp != 24 && bpp != 32)) {
        ESP_LOGW(TAG, "Unsupported BMP: %ldx%ld %dbpp", (long)width, (long)height, bpp);
        return false;
    }

    bool top_down = (height < 0);
    int abs_h = top_down ? -height : height;
    int abs_w = (width > max_w) ? max_w : width;
    if (abs_h > max_h) abs_h = max_h;

    int row_stride = ((width * bpp + 31) / 32) * 4;
    uint8_t *row_buf = (uint8_t *)malloc(row_stride);
    if (!row_buf) return false;

    fseek(f, data_offset, SEEK_SET);

    for (int y = 0; y < abs_h; y++) {
        int dst_y = top_down ? y : (abs_h - 1 - y);
        if (fread(row_buf, 1, row_stride, f) < (size_t)row_stride) break;

        uint16_t *dst_row = out_rgb565 + dst_y * max_w;
        if (bpp == 24) {
            for (int x = 0; x < abs_w; x++) {
                uint8_t b = row_buf[x * 3 + 0];
                uint8_t g = row_buf[x * 3 + 1];
                uint8_t r = row_buf[x * 3 + 2];
                uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                dst_row[x] = __builtin_bswap16(c);
            }
        } else if (bpp == 32) {
            for (int x = 0; x < abs_w; x++) {
                uint8_t b = row_buf[x * 4 + 0];
                uint8_t g = row_buf[x * 4 + 1];
                uint8_t r = row_buf[x * 4 + 2];
                uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                dst_row[x] = __builtin_bswap16(c);
            }
        } else if (bpp == 16) {
            for (int x = 0; x < abs_w; x++) {
                dst_row[x] = __builtin_bswap16(((uint16_t *)row_buf)[x]);
            }
        }
    }

    free(row_buf);
    *out_w = abs_w;
    *out_h = abs_h;
    return true;
}

static void *jpeg_posix_open(const char *szFilename, int32_t *pFileSize) {
    FILE *f = fopen(szFilename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *pFileSize = (int32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    return (void *)f;
}

static void jpeg_posix_close(void *pHandle) {
    if (pHandle) {
        fclose((FILE *)pHandle);
    }
}

static int32_t jpeg_posix_read(JPEGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    FILE *f = (FILE *)pFile->fHandle;
    if (!f) return 0;
    return (int32_t)fread(pBuf, 1, iLen, f);
}

static int32_t jpeg_posix_seek(JPEGFILE *pFile, int32_t iPosition) {
    FILE *f = (FILE *)pFile->fHandle;
    if (!f) return 0;
    fseek(f, iPosition, SEEK_SET);
    return (int32_t)ftell(f);
}

static JPEGDEC *s_jpeg_dec = NULL;

static JPEGDEC *get_jpeg_decoder(void)
{
    if (!s_jpeg_dec) {
        s_jpeg_dec = (JPEGDEC *)heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_jpeg_dec) {
            s_jpeg_dec = (JPEGDEC *)malloc(sizeof(JPEGDEC));
        }
    }
    return s_jpeg_dec;
}

/* --------------------------------------------------------------------
 * High-Level Image Decoder (JPEG & BMP)
 * -------------------------------------------------------------------- */
bool media_decoder_load_image(const char *filepath, uint16_t *out_rgb565, int max_w, int max_h, int *out_w, int *out_h)
{
    if (!filepath || !out_rgb565) return false;

    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    const char *dot = strrchr(filepath, '.');
    if (dot && strcasecmp(dot, ".bmp") == 0) {
        bool res = decode_bmp(f, out_rgb565, max_w, max_h, out_w, out_h);
        fclose(f);
        return res;
    }
    fclose(f);

    /* JPEG Decoder */
    JPEGDEC *jpeg = get_jpeg_decoder();
    if (!jpeg) return false;

    JpegDrawContext ctx;
    ctx.dest = out_rgb565;
    ctx.max_w = max_w;
    ctx.max_h = max_h;
    ctx.stride = max_w;
    ctx.actual_w = 0;
    ctx.actual_h = 0;

    memset(out_rgb565, 0, max_w * max_h * sizeof(uint16_t));

    if (!jpeg->open(filepath, jpeg_posix_open, jpeg_posix_close, jpeg_posix_read, jpeg_posix_seek, jpeg_draw_callback)) {
        ESP_LOGE(TAG, "Failed to open JPEG file: %s", filepath);
        return false;
    }

    jpeg->setUserPointer(&ctx);
    jpeg->setPixelType(RGB565_BIG_ENDIAN);

    int orig_w = jpeg->getWidth();
    int orig_h = jpeg->getHeight();
    int scale = 0;

    if (orig_w > max_w * 2 || orig_h > max_h * 2) {
        scale = JPEG_SCALE_QUARTER;
        orig_w /= 4;
        orig_h /= 4;
    } else if (orig_w > max_w || orig_h > max_h) {
        scale = JPEG_SCALE_HALF;
        orig_w /= 2;
        orig_h /= 2;
    }

    int target_w = (orig_w > max_w) ? max_w : orig_w;
    int target_h = (orig_h > max_h) ? max_h : orig_h;
    ctx.max_w = target_w;
    ctx.max_h = target_h;
    ctx.stride = target_w;

    int rc = jpeg->decode(0, 0, scale);
    jpeg->close();

    if (rc == 1) {
        *out_w = target_w;
        *out_h = target_h;
        return true;
    }
    return false;
}

/* --------------------------------------------------------------------
 * MJPEG Video Decoder
 * -------------------------------------------------------------------- */
static FILE *s_mjpeg_file = NULL;
static uint8_t *s_mjpeg_frame_buf = NULL;
static const size_t MJPEG_FRAME_MAX_SIZE = 128 * 1024;
static uint32_t s_mjpeg_frames_decoded = 0;
static uint8_t s_chunk[1024];
static size_t s_chunk_len = 0;
static size_t s_chunk_pos = 0;

bool media_decoder_open_mjpeg(const char *filepath)
{
    media_decoder_close_mjpeg();

    s_mjpeg_file = fopen(filepath, "rb");
    if (!s_mjpeg_file) {
        ESP_LOGE(TAG, "Cannot open MJPEG video: %s", filepath);
        return false;
    }

    if (!s_mjpeg_frame_buf) {
        s_mjpeg_frame_buf = (uint8_t *)heap_caps_malloc(MJPEG_FRAME_MAX_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    s_mjpeg_frames_decoded = 0;
    s_chunk_len = 0;
    s_chunk_pos = 0;
    return (s_mjpeg_frame_buf != NULL);
}

static inline int mjpeg_read_byte(void)
{
    if (s_chunk_pos >= s_chunk_len) {
        if (!s_mjpeg_file) return -1;
        s_chunk_len = fread(s_chunk, 1, sizeof(s_chunk), s_mjpeg_file);
        s_chunk_pos = 0;
        if (s_chunk_len == 0) return -1;
    }
    return s_chunk[s_chunk_pos++];
}

bool media_decoder_read_next_frame(uint16_t *out_rgb565, int *out_w, int *out_h)
{
    if (!s_mjpeg_file || !s_mjpeg_frame_buf || !out_rgb565) return false;

    /* 1. Find JPEG SOI (0xFF, 0xD8) */
    int b1 = -1, b2 = -1;
    while ((b2 = mjpeg_read_byte()) != -1) {
        if (b1 == 0xFF && b2 == 0xD8) break;
        b1 = b2;
    }
    if (b2 == -1) return false; // EOF

    s_mjpeg_frame_buf[0] = 0xFF;
    s_mjpeg_frame_buf[1] = 0xD8;
    size_t frame_len = 2;

    /* 2. Read until JPEG EOI (0xFF, 0xD9) */
    b1 = 0xD8;
    while ((b2 = mjpeg_read_byte()) != -1) {
        if (frame_len < MJPEG_FRAME_MAX_SIZE) {
            s_mjpeg_frame_buf[frame_len++] = (uint8_t)b2;
        }
        if (b1 == 0xFF && b2 == 0xD9) break;
        b1 = b2;
    }

    if (b2 == -1 && frame_len <= 2) return false;

    /* 3. Decode single JPEG frame using JPEGDEC */
    JPEGDEC *jpeg = get_jpeg_decoder();
    if (!jpeg) return false;

    if (!jpeg->openRAM(s_mjpeg_frame_buf, frame_len, jpeg_draw_callback)) {
        return false;
    }

    jpeg->setPixelType(RGB565_BIG_ENDIAN);

    int fw = jpeg->getWidth();
    int fh = jpeg->getHeight();
    *out_w = (fw > 480) ? 480 : fw;
    *out_h = (fh > 320) ? 320 : fh;

    JpegDrawContext ctx;
    ctx.dest = out_rgb565;
    ctx.max_w = *out_w;
    ctx.max_h = *out_h;
    ctx.stride = *out_w;
    ctx.actual_w = 0;
    ctx.actual_h = 0;

    jpeg->setUserPointer(&ctx);

    int rc = jpeg->decode(0, 0, 0);
    jpeg->close();

    if (rc == 1) {
        s_mjpeg_frames_decoded++;
        return true;
    }
    return false;
}

void media_decoder_close_mjpeg(void)
{
    if (s_mjpeg_file) {
        fclose(s_mjpeg_file);
        s_mjpeg_file = NULL;
    }
    if (s_mjpeg_frame_buf) {
        free(s_mjpeg_frame_buf);
        s_mjpeg_frame_buf = NULL;
    }
    s_chunk_len = 0;
    s_chunk_pos = 0;
}

void media_decoder_rewind_mjpeg(void)
{
    if (s_mjpeg_file) {
        fseek(s_mjpeg_file, 0, SEEK_SET);
        s_chunk_len = 0;
        s_chunk_pos = 0;
    }
}

uint32_t media_decoder_get_frame_count(void)
{
    return s_mjpeg_frames_decoded;
}
