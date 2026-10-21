#include "audio_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
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

#define IN_BUF_SIZE   8192

static i2s_chan_handle_t s_tx_handle = NULL;
static uint32_t s_current_rate = 44100;
static volatile audio_state_t s_state = AUDIO_STATE_STOPPED;
static volatile bool s_is_decoding = false;
static TaskHandle_t s_audio_task_handle = NULL;

static char s_filepath[512] = {0};
static uint8_t s_volume = 80; // 0 - 100%
static uint32_t s_pos_sec = 0;
static uint32_t s_dur_sec = 0;

/* Dedicated persistent streaming buffers in PSRAM */
static uint8_t  *s_in_buf     = NULL;
static int16_t  *s_out_buf    = NULL;
static int16_t  *s_stereo_buf = NULL;

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
    s_is_decoding = true;
    uint8_t header[44];
    if (fread(header, 1, 44, f) < 44) {
        s_is_decoding = false;
        return;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "Not a valid RIFF/WAVE file");
        s_is_decoding = false;
        return;
    }

    uint16_t channels = header[22] | (header[23] << 8);
    uint32_t sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint32_t byte_rate = header[28] | (header[29] << 8) | (header[30] << 16) | (header[31] << 24);
    uint16_t bits_per_sample = header[34] | (header[35] << 8);
    uint32_t data_size = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);

    if (bits_per_sample != 16) {
        ESP_LOGW(TAG, "Only 16-bit PCM WAV is supported (got %d)", bits_per_sample);
        s_is_decoding = false;
        return;
    }

    set_sample_rate(sample_rate);
    if (byte_rate > 0 && data_size > 0) {
        s_dur_sec = data_size / byte_rate;
    }

    uint32_t bytes_played = 0;

    while (s_state != AUDIO_STATE_STOPPED) {
        while (s_state == AUDIO_STATE_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (s_state == AUDIO_STATE_STOPPED) break;

        size_t samples_to_read = (channels == 1) ? 512 : 1024;
        size_t r = fread(s_out_buf, sizeof(int16_t), samples_to_read, f);
        if (r == 0) break; // EOF reached

        size_t bytes_written = 0;
        if (channels == 1) {
            for (size_t i = 0; i < r; i++) {
                s_stereo_buf[i * 2]     = s_out_buf[i];
                s_stereo_buf[i * 2 + 1] = s_out_buf[i];
            }
            apply_volume(s_stereo_buf, r * 2);
            i2s_channel_write(s_tx_handle, s_stereo_buf, r * 2 * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(100));
            bytes_played += r * sizeof(int16_t);
        } else {
            apply_volume(s_out_buf, r);
            i2s_channel_write(s_tx_handle, s_out_buf, r * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(100));
            bytes_played += r * sizeof(int16_t);
        }

        if (byte_rate > 0) {
            s_pos_sec = bytes_played / byte_rate;
        }
    }
    s_is_decoding = false;
}

/* --------------------------------------------------------------------
 * MP3 File Playback Routine using Persistent Helix Decoder Buffers
 * -------------------------------------------------------------------- */
static void play_mp3(FILE *f)
{
    s_is_decoding = true;
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    MP3Decoder_ClearBuffer();

    if (!s_in_buf || !s_out_buf || !s_stereo_buf) {
        ESP_LOGE(TAG, "Streaming buffers not ready");
        s_is_decoding = false;
        return;
    }

    uint8_t *read_ptr = s_in_buf;
    int32_t bytes_left = 0;
    long total_bytes_read = 0;

    while (s_state != AUDIO_STATE_STOPPED) {
        while (s_state == AUDIO_STATE_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (s_state == AUDIO_STATE_STOPPED) break;

        /* Refill input buffer if less than 2048 bytes available */
        if (bytes_left < 2048) {
            if (bytes_left < 0) bytes_left = 0;
            if (bytes_left > (int32_t)IN_BUF_SIZE) bytes_left = (int32_t)IN_BUF_SIZE;
            if (bytes_left > 0 && read_ptr > s_in_buf) {
                memmove(s_in_buf, read_ptr, bytes_left);
            }
            read_ptr = s_in_buf;
            size_t bytes_to_read = IN_BUF_SIZE - bytes_left;
            size_t r = fread(s_in_buf + bytes_left, 1, bytes_to_read, f);
            bytes_left += r;
            total_bytes_read += r;
            if (bytes_left <= 0) break; // EOF reached
        }

        /* Find MP3 Sync Word */
        int sync = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync < 0) {
            /* If no sync word in remaining data, drop and re-read */
            bytes_left = 0;
            read_ptr = s_in_buf;
            continue;
        }

        read_ptr += sync;
        bytes_left -= sync;

        int32_t prev_bytes_left = bytes_left;
        int err = MP3Decode(read_ptr, &bytes_left, s_out_buf, 0);

        if (err == ERR_MP3_NONE) {
            int sr = MP3GetSampRate();
            int ch = MP3GetChannels();
            int samps = MP3GetOutputSamps(); // Total samples
            int br = MP3GetBitrate();

            if (sr >= 8000 && sr <= 96000 && sr != (int)s_current_rate) {
                set_sample_rate(sr);
            }

            if (br > 0 && s_dur_sec == 0 && file_size > 0) {
                s_dur_sec = (uint32_t)((file_size * 8) / br);
            }

            size_t bytes_written = 0;
            if (samps > 0 && samps <= 2304) {
                if (ch == 1) {
                    /* Convert mono to stereo (samps <= 2304, s_stereo_buf has 4608 samples capacity) */
                    for (int i = 0; i < samps; i++) {
                        s_stereo_buf[i * 2]     = s_out_buf[i];
                        s_stereo_buf[i * 2 + 1] = s_out_buf[i];
                    }
                    apply_volume(s_stereo_buf, samps * 2);
                    i2s_channel_write(s_tx_handle, s_stereo_buf, samps * 2 * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(100));
                } else {
                    apply_volume(s_out_buf, samps);
                    i2s_channel_write(s_tx_handle, s_out_buf, samps * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(100));
                }
            }

            if (br > 0 && total_bytes_read > 0) {
                s_pos_sec = (uint32_t)((total_bytes_read * 8) / br);
                if (s_dur_sec > 0 && s_pos_sec > s_dur_sec) s_pos_sec = s_dur_sec;
            }
        } else {
            /* Error in MP3 bitstream: clear bit reservoir state */
            MP3Decoder_ClearBuffer();
        }

        int32_t consumed = prev_bytes_left - bytes_left;
        if (consumed > 0 && consumed <= prev_bytes_left) {
            read_ptr += consumed;
        } else {
            /* Advance by 1 byte to prevent loop stall on corrupted frame */
            read_ptr++;
            bytes_left--;
        }

        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to other tasks
    }

    s_is_decoding = false;
}

/* --------------------------------------------------------------------
 * Persistent Audio Worker Task (Core 0)
 * -------------------------------------------------------------------- */
static void audio_worker_task(void *pvParam)
{
    (void)pvParam;
    ESP_LOGI(TAG, "Audio player background worker active on Core 0");

    while (1) {
        /* Sleep until a play request arrives */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (s_filepath[0] == '\0') {
            s_state = AUDIO_STATE_STOPPED;
            continue;
        }

        FILE *f = fopen(s_filepath, "rb");
        if (!f) {
            ESP_LOGE(TAG, "Cannot open audio file: %s", s_filepath);
            s_state = AUDIO_STATE_STOPPED;
            continue;
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
    }
}

/* --------------------------------------------------------------------
 * Public Subsystem API
 * -------------------------------------------------------------------- */
esp_err_t audio_player_init(void)
{
    if (s_tx_handle != NULL) return ESP_OK;

    /* 1. Allocate dedicated streaming buffers once in PSRAM */
    if (!s_in_buf) {
        s_in_buf = (uint8_t *)heap_caps_malloc(IN_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_out_buf) {
        s_out_buf = (int16_t *)heap_caps_malloc(2304 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_stereo_buf) {
        /* Allocate 4608 samples capacity (9216 bytes) so mono conversion never overflows */
        s_stereo_buf = (int16_t *)heap_caps_malloc(4608 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    MP3Decoder_AllocateBuffers();

    /* 2. Configure I2S standard Philips mode */
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

    /* 3. Start single persistent audio worker on Core 0 */
    if (!s_audio_task_handle) {
        BaseType_t res = xTaskCreatePinnedToCore(
            audio_worker_task,
            "audio_worker",
            12288,
            NULL,
            5,
            &s_audio_task_handle,
            0 // Core 0
        );
        if (res != pdPASS) {
            ESP_LOGE(TAG, "Failed to create persistent audio worker task!");
        }
    }

    return ret;
}

esp_err_t audio_player_play_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!s_tx_handle) return ESP_ERR_INVALID_STATE;
    const uint32_t sr = 44100;
    uint32_t total_samples = (sr * duration_ms) / 1000;
    const size_t chunk_samps = 256;
    int16_t buf[chunk_samps * 2]; // Stereo

    for (uint32_t s = 0; s < total_samples; s += chunk_samps) {
        uint32_t count = (s + chunk_samps <= total_samples) ? chunk_samps : (total_samples - s);
        for (uint32_t i = 0; i < count; i++) {
            float t = (float)(s + i) / (float)sr;
            int16_t val = (int16_t)(sinf(2.0f * 3.14159265f * (float)freq_hz * t) * 18000.0f);
            val = (int16_t)(((int32_t)val * s_volume) / 100);
            buf[i * 2 + 0] = val;
            buf[i * 2 + 1] = val;
        }
        size_t written = 0;
        i2s_channel_write(s_tx_handle, buf, count * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

esp_err_t audio_player_play(const char *filepath)
{
    if (!filepath) return ESP_ERR_INVALID_ARG;
    if (!s_audio_task_handle) {
        audio_player_init();
        if (!s_audio_task_handle) return ESP_ERR_INVALID_STATE;
    }

    /* Stop previous playback cleanly */
    audio_player_stop();

    strncpy(s_filepath, filepath, sizeof(s_filepath) - 1);
    s_filepath[sizeof(s_filepath) - 1] = '\0';
    s_pos_sec = 0;
    s_dur_sec = 0;
    s_state = AUDIO_STATE_PLAYING;

    /* Signal persistent worker task */
    xTaskNotifyGive(s_audio_task_handle);
    return ESP_OK;
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
    if (s_state == AUDIO_STATE_STOPPED && !s_is_decoding) return;
    s_state = AUDIO_STATE_STOPPED;
    int timeout = 30; // Wait up to 300ms for current decode loop to exit
    while (s_is_decoding && timeout-- > 0) {
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
