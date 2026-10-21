#ifndef MEDIA_DECODER_H
#define MEDIA_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode an image file (JPEG or BMP) into an RGB565 buffer.
 * @param filepath Full path to image file
 * @param out_rgb565 Destination buffer for RGB565 pixels (e.g. allocated in PSRAM)
 * @param max_w Maximum allowed width (e.g. 480)
 * @param max_h Maximum allowed height (e.g. 320)
 * @param out_w Output actual decoded width
 * @param out_h Output actual decoded height
 * @return true on success, false on failure
 */
bool media_decoder_load_image(const char *filepath, uint16_t *out_rgb565, int max_w, int max_h, int *out_w, int *out_h);

/**
 * @brief Open an MJPEG video stream file for frame-by-frame playback.
 * @param filepath Full path to .mjp or .mjpeg file
 * @return true if stream opened successfully
 */
bool media_decoder_open_mjpeg(const char *filepath);

/**
 * @brief Read and decode the next MJPEG video frame into the RGB565 buffer.
 * @param out_rgb565 Destination frame buffer (480 x 320 x 2 bytes)
 * @param out_w Decoded frame width
 * @param out_h Decoded frame height
 * @return true if frame read and decoded, false if EOF or error
 */
bool media_decoder_read_next_frame(uint16_t *out_rgb565, int *out_w, int *out_h);

/**
 * @brief Close MJPEG video stream and release file handles.
 */
void media_decoder_close_mjpeg(void);

/**
 * @brief Rewind MJPEG stream to start for looping playback.
 */
void media_decoder_rewind_mjpeg(void);

/**
 * @brief Get total frame count decoded so far.
 */
uint32_t media_decoder_get_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif // MEDIA_DECODER_H
