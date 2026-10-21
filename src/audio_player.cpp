#include "audio_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "mp3dec/mp3_decoder.h"

static const char *TAG = "AUDIO_PLAYER";

#define I2S_BCK_PIN   (gpio_num_t)42
#define I2S_LRCK_PIN  (gpio_num_t)2
#define I2S_DIN_PIN   (gpio_num_t)41

static i2s_chan_handle_t s_tx_handle = NULL;
static uint32_t s_current_rate = 44100;
static audio_state_t s_state = AUDIO_STATE_STOPPED;
static TaskHandle_t s_audio_task_handle = NULL;

static char s_filepath[512] = {0};
static uint8_t s_volume = 80; // 0 - 100%
static uint32_t s_pos_sec = 0;
static uint32_t s_dur_sec = 0;

static void set_sample_rate(uint32_t rate)
{
    if (!s_tx_handle || rate == s_current_rate || rate < 8000 || rate > 96000) return;
    s_current_rate = rate;
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    i2s_channel_disable(s_tx_handle);
    esp_err_t err = i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg);
    i2s_channel_enable(s_tx_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2S Clock reconfigured to %lu Hz", (unsigned long)rate);
    }
}

esp_err_t audio_player_init(void)
{
    if (s_tx_handle != NULL) return ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S TX channel (%s)", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg;
    memset(&std_cfg, 0, sizeof(std_cfg));
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = I2S_BCK_PIN;
    std_cfg.gpio_cfg.ws   = I2S_LRCK_PIN;
    std_cfg.gpio_cfg.dout = I2S_DIN_PIN;
    std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;

    ret = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S standard mode (%s)", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_tx_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2S Audio Output initialized on BCK:%d, LRCK:%d, DOUT:%d",
                 I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DIN_PIN);
    }
    return ret;
}

static void apply_volume(int16_t *samples, size_t num_samples)
{
    if (s_volume >= 100) return;
    for (size_t i = 0; i < num_samples; i++) {
        samples[i] = (int16_t)(((int32_t)samples[i] * s_volume) / 100);
    }
}

/* --------------------------------------------------------------------
 * WAV File Playback Routine
 * -------------------------------------------------------------------- */
static void play_wav(FILE *f)
{
    uint8_t header[44];
    if (fread(header, 1, 44, f) < 44) return;

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "Not a valid RIFF/WAVE file");
        return;
    }

    uint16_t channels = header[22] | (header[23] << 8);
    uint32_t sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint32_t byte_rate = header[28] | (header[29] << 8) | (header[30] << 16) | (header[31] << 24);
    uint16_t bits_per_sample = header[34] | (header[35] << 8);
    uint32_t data_size = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);

    if (bits_per_sample != 16) {
        ESP_LOGW(TAG, "Only 16-bit PCM WAV is supported (got %d)", bits_per_sample);
        return;
    }

    set_sample_rate(sample_rate);
    if (byte_rate > 0 && data_size > 0) {
        s_dur_sec = data_size / byte_rate;
    }

    uint32_t bytes_played = 0;
    int16_t raw_buf[512];
    int16_t stereo_buf[1024];

    while (s_state != AUDIO_STATE_STOPPED) {
        while (s_state == AUDIO_STATE_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (s_state == AUDIO_STATE_STOPPED) break;

        size_t samples_to_read = (channels == 1) ? 512 : 1024;
        size_t r = fread(raw_buf, sizeof(int16_t), samples_to_read, f);
        if (r == 0) break; // EOF

        size_t bytes_written = 0;
        if (channels == 1) {
            for (size_t i = 0; i < r; i++) {
                stereo_buf[i * 2]     = raw_buf[i];
                stereo_buf[i * 2 + 1] = raw_buf[i];
            }
            apply_volume(stereo_buf, r * 2);
            i2s_channel_write(s_tx_handle, stereo_buf, r * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
            bytes_played += r * sizeof(int16_t);
        } else {
            apply_volume(raw_buf, r);
            i2s_channel_write(s_tx_handle, raw_buf, r * sizeof(int16_t), &bytes_written, portMAX_DELAY);
            bytes_played += r * sizeof(int16_t);
        }

        if (byte_rate > 0) {
            s_pos_sec = bytes_played / byte_rate;
        }
    }
}

/* --------------------------------------------------------------------
 * MP3 File Playback Routine using Helix Decoder
 * -------------------------------------------------------------------- */
static void play_mp3(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (!MP3Decoder_AllocateBuffers()) {
        ESP_LOGE(TAG, "Failed to allocate MP3 decoder memory");
        return;
    }

    const size_t IN_BUF_SIZE = 4096;
    uint8_t *in_buf = (uint8_t *)heap_caps_malloc(IN_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *out_buf = (int16_t *)heap_caps_malloc(1152 * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int16_t *stereo_buf = (int16_t *)heap_caps_malloc(1152 * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (!in_buf || !out_buf || !stereo_buf) {
        ESP_LOGE(TAG, "Failed to allocate audio streaming buffers");
        if (in_buf) free(in_buf);
        if (out_buf) free(out_buf);
        if (stereo_buf) free(stereo_buf);
        MP3Decoder_FreeBuffers();
        return;
    }

    uint8_t *read_ptr = in_buf;
    int32_t bytes_left = 0;
    long total_bytes_read = 0;

    while (s_state != AUDIO_STATE_STOPPED) {
        while (s_state == AUDIO_STATE_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (s_state == AUDIO_STATE_STOPPED) break;

        /* Refill input buffer if less than 1940 bytes available */
        if (bytes_left < 1940) {
            if (bytes_left > 0 && read_ptr > in_buf) {
                memmove(in_buf, read_ptr, bytes_left);
            }
            read_ptr = in_buf;
            size_t bytes_to_read = IN_BUF_SIZE - bytes_left;
            size_t r = fread(in_buf + bytes_left, 1, bytes_to_read, f);
            bytes_left += r;
            total_bytes_read += r;
            if (bytes_left <= 0) break; // EOF reached
        }

        /* Find MP3 Sync Word */
        int sync = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync < 0) {
            /* Drop buffer and re-read */
            bytes_left = 0;
            read_ptr = in_buf;
            continue;
        }

        read_ptr += sync;
        bytes_left -= sync;

        int32_t prev_bytes_left = bytes_left;
        int err = MP3Decode(read_ptr, &bytes_left, out_buf, 0);

        if (err == ERR_MP3_NONE) {
            int sr = MP3GetSampRate();
            int ch = MP3GetChannels();
            int samps = MP3GetOutputSamps(); // Total samples (e.g. 1152 for mono, 2304 for stereo)
            int br = MP3GetBitrate();

            if (sr > 0 && sr != (int)s_current_rate) {
                set_sample_rate(sr);
            }

            if (br > 0 && s_dur_sec == 0 && file_size > 0) {
                s_dur_sec = (uint32_t)((file_size * 8) / br);
            }

            size_t bytes_written = 0;
            if (ch == 1) {
                /* Convert mono to stereo */
                for (int i = 0; i < samps; i++) {
                    stereo_buf[i * 2]     = out_buf[i];
                    stereo_buf[i * 2 + 1] = out_buf[i];
                }
                apply_volume(stereo_buf, samps * 2);
                i2s_channel_write(s_tx_handle, stereo_buf, samps * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
            } else {
                apply_volume(out_buf, samps);
                i2s_channel_write(s_tx_handle, out_buf, samps * sizeof(int16_t), &bytes_written, portMAX_DELAY);
            }

            if (br > 0 && total_bytes_read > 0) {
                s_pos_sec = (uint32_t)((total_bytes_read * 8) / br);
                if (s_dur_sec > 0 && s_pos_sec > s_dur_sec) s_pos_sec = s_dur_sec;
            }
        }

        int32_t consumed = prev_bytes_left - bytes_left;
        if (consumed > 0) {
            read_ptr += consumed;
        } else {
            /* If no bytes consumed, advance by 1 byte to prevent infinite loop */
            read_ptr++;
            bytes_left--;
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to feed watchdog
    }

    free(in_buf);
    free(out_buf);
    free(stereo_buf);
    MP3Decoder_FreeBuffers();
}

/* --------------------------------------------------------------------
 * Audio Playback FreeRTOS Task
 * -------------------------------------------------------------------- */
static void audio_task(void *pvParam)
{
    (void)pvParam;
    audio_player_init();

    FILE *f = fopen(s_filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open audio file: %s", s_filepath);
        s_state = AUDIO_STATE_STOPPED;
        s_audio_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting audio playback: %s", s_filepath);

    const char *dot = strrchr(s_filepath, '.');
    if (dot && (strcasecmp(dot, ".wav") == 0)) {
        play_wav(f);
    } else {
        play_mp3(f);
    }

    fclose(f);
    ESP_LOGI(TAG, "Playback completed for %s", s_filepath);

    s_state = AUDIO_STATE_STOPPED;
    s_audio_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_player_play(const char *filepath)
{
    if (!filepath) return ESP_ERR_INVALID_ARG;

    audio_player_stop();
    vTaskDelay(pdMS_TO_TICKS(50));

    strncpy(s_filepath, filepath, sizeof(s_filepath) - 1);
    s_filepath[sizeof(s_filepath) - 1] = '\0';
    s_pos_sec = 0;
    s_dur_sec = 0;
    s_state = AUDIO_STATE_PLAYING;

    BaseType_t res = xTaskCreatePinnedToCore(
        audio_task,
        "audio_task",
        16384,
        NULL,
        5,
        &s_audio_task_handle,
        0 // Core 0
    );

    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

void audio_player_pause(void)
{
    if (s_state == AUDIO_STATE_PLAYING) {
        s_state = AUDIO_STATE_PAUSED;
    }
}

void audio_player_resume(void)
{
    if (s_state == AUDIO_STATE_PAUSED) {
        s_state = AUDIO_STATE_PLAYING;
    }
}

void audio_player_stop(void)
{
    s_state = AUDIO_STATE_STOPPED;
    int timeout = 20; // wait up to 200ms
    while (s_audio_task_handle != NULL && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool audio_player_is_playing(void)
{
    return (s_state == AUDIO_STATE_PLAYING);
}

audio_state_t audio_player_get_state(void)
{
    return s_state;
}

uint32_t audio_player_get_position_sec(void)
{
    return s_pos_sec;
}

uint32_t audio_player_get_duration_sec(void)
{
    return s_dur_sec;
}

void audio_player_set_volume(uint8_t vol)
{
    s_volume = (vol > 100) ? 100 : vol;
}

uint8_t audio_player_get_volume(void)
{
    return s_volume;
}
