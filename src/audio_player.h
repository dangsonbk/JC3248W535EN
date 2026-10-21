#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_STATE_STOPPED = 0,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_PAUSED
} audio_state_t;

/**
 * @brief Initialize the I2S audio driver for JC3248W535EN
 * Hardware Pins: GPIO 42 (BCK), GPIO 2 (LRCK), GPIO 41 (DIN)
 */
esp_err_t audio_player_init(void);

/**
 * @brief Start playing audio file (.wav or .mp3)
 * @param filepath Full path to file (e.g. "/sdcard/music/test.mp3")
 */
esp_err_t audio_player_play(const char *filepath);

/**
 * @brief Pause audio playback
 */
void audio_player_pause(void);

/**
 * @brief Resume audio playback
 */
void audio_player_resume(void);

/**
 * @brief Stop audio playback and release decoder resources
 */
void audio_player_stop(void);

/**
 * @brief Check if audio is currently playing
 */
bool audio_player_is_playing(void);

/**
 * @brief Get current playback state
 */
audio_state_t audio_player_get_state(void);

/**
 * @brief Get current playback position in seconds
 */
uint32_t audio_player_get_position_sec(void);

/**
 * @brief Get total file duration in seconds
 */
uint32_t audio_player_get_duration_sec(void);

/**
 * @brief Set volume level (0 - 100%)
 */
void audio_player_set_volume(uint8_t vol);

/**
 * @brief Get current volume level (0 - 100%)
 */
uint8_t audio_player_get_volume(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_PLAYER_H
