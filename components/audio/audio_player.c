#include "audio_player.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio_board.h"
#include "decoder/esp_audio_dec.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef struct {
    int metaint;
    int audio_bytes_left;
    int metadata_bytes_left;
    bool expect_metadata_len;
} icy_filter_t;

static const char *TAG = "audio_player";

#define AUDIO_MAX_CONSECUTIVE_DECODE_ERRORS 24
#define AUDIO_MAX_CONSECUTIVE_EMPTY_READS   300
#define AUDIO_I2S_WRITE_TIMEOUT_MS          300
#define AUDIO_I2S_DMA_DESC_NUM              8
#define AUDIO_I2S_DMA_FRAME_NUM             512
#define AUDIO_PCM_RING_SIZE                 (1024 * 1024)
#define AUDIO_PCM_PREBUFFER_SIZE            (256 * 1024)
#define AUDIO_PCM_RESUME_SIZE               (192 * 1024)
#define AUDIO_PCM_READ_CHUNK_SIZE           4096
#define AUDIO_PCM_POLL_MS                   10
#define AUDIO_PCM_UNDERRUN_GRACE_MS         800

static const audio_station_t s_stations[] = {
    { "Groove Salad", "http://ice1.somafm.com/groovesalad-128-mp3" },
    { "Drone Zone", "http://ice1.somafm.com/dronezone-128-mp3" },
    { "Secret Agent", "http://ice1.somafm.com/secretagent-128-mp3" },
    { "DEF CON", "http://ice1.somafm.com/defcon-128-mp3" },
};

TaskHandle_t s_audio_task = NULL;
static TaskHandle_t s_audio_writer_task = NULL;
static portMUX_TYPE s_audio_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile audio_state_t s_audio_state = AUDIO_STATE_IDLE;
static volatile bool s_radio_active = false;
// Token is bumped on every mode/station change so the streaming loop can self-cancel safely.
static volatile uint32_t s_request_token = 0;
static volatile size_t s_station_index = 0;
static bool s_decoder_registered = false;

static i2s_chan_handle_t s_tx_handle = NULL;
static uint32_t s_i2s_rate = 0;
static uint8_t s_i2s_channels = 0;
static SemaphoreHandle_t s_pcm_mutex = NULL;
static uint8_t *s_pcm_ring = NULL;
static size_t s_pcm_ring_capacity = 0;
static size_t s_pcm_ring_head = 0;
static size_t s_pcm_ring_tail = 0;
static size_t s_pcm_ring_size = 0;
static bool s_pcm_format_ready = false;
static uint32_t s_pcm_sample_rate = 0;
static uint8_t s_pcm_channels = 0;
static uint8_t s_pcm_bits_per_sample = 0;
static bool s_pcm_ring_in_psram = false;

static bool audio_request_changed(uint32_t token, size_t station_index);
static esp_err_t audio_pcm_ensure_allocated(void);
static void audio_pcm_release(void);
static void audio_pcm_reset_or_release(void);

static size_t audio_pcm_prebuffer_target(void)
{
    if (s_pcm_ring_capacity == 0) {
        return AUDIO_PCM_PREBUFFER_SIZE;
    }

    size_t target = (s_pcm_ring_capacity * 3) / 4;
    if (target < (12 * 1024)) {
        target = s_pcm_ring_capacity / 2;
    }
    if (target > AUDIO_PCM_PREBUFFER_SIZE) {
        target = AUDIO_PCM_PREBUFFER_SIZE;
    }
    return target;
}

static size_t audio_pcm_resume_target(void)
{
    if (s_pcm_ring_capacity == 0) {
        return AUDIO_PCM_RESUME_SIZE;
    }

    size_t target = s_pcm_ring_capacity / 2;
    if (target < (8 * 1024)) {
        target = s_pcm_ring_capacity / 3;
    }
    if (target > AUDIO_PCM_RESUME_SIZE) {
        target = AUDIO_PCM_RESUME_SIZE;
    }
    return target;
}

static const char *audio_state_to_string(audio_state_t state)
{
    switch (state) {
        case AUDIO_STATE_BUFFERING:
            return "BUFFERING";
        case AUDIO_STATE_PLAYING:
            return "PLAYING";
        case AUDIO_STATE_ERROR:
            return "ERROR";
        case AUDIO_STATE_NO_WIFI:
            return "NO_WIFI";
        case AUDIO_STATE_IDLE:
        default:
            return "IDLE";
    }
}

static void audio_set_state(audio_state_t state)
{
    audio_state_t old_state;

    portENTER_CRITICAL(&s_audio_lock);
    old_state = s_audio_state;
    s_audio_state = state;
    portEXIT_CRITICAL(&s_audio_lock);

    if (old_state != state) {
        ESP_LOGI(TAG, "state: %s -> %s", audio_state_to_string(old_state), audio_state_to_string(state));
    }
}

static audio_state_t audio_get_state_locked(void)
{
    audio_state_t state;

    portENTER_CRITICAL(&s_audio_lock);
    state = s_audio_state;
    portEXIT_CRITICAL(&s_audio_lock);
    return state;
}

static size_t audio_get_station_index_locked(void)
{
    size_t index;

    portENTER_CRITICAL(&s_audio_lock);
    index = s_station_index;
    portEXIT_CRITICAL(&s_audio_lock);
    return index;
}

static bool audio_is_active_locked(void)
{
    bool active;

    portENTER_CRITICAL(&s_audio_lock);
    active = s_radio_active;
    portEXIT_CRITICAL(&s_audio_lock);
    return active;
}

static uint32_t audio_get_request_token_locked(void)
{
    uint32_t token;

    portENTER_CRITICAL(&s_audio_lock);
    token = s_request_token;
    portEXIT_CRITICAL(&s_audio_lock);
    return token;
}

static bool audio_wifi_is_ready(void)
{
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

static void audio_notify_task(void)
{
    if (s_audio_task != NULL) {
        xTaskNotifyGive(s_audio_task);
    }
}

static void audio_output_stop(void)
{
    if (s_tx_handle != NULL) {
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }
    s_i2s_rate = 0;
    s_i2s_channels = 0;
}

static void audio_pcm_reset(void)
{
    if (s_pcm_mutex == NULL || s_pcm_ring == NULL) {
        return;
    }
    xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
    s_pcm_ring_head = 0;
    s_pcm_ring_tail = 0;
    s_pcm_ring_size = 0;
    s_pcm_format_ready = false;
    s_pcm_sample_rate = 0;
    s_pcm_channels = 0;
    s_pcm_bits_per_sample = 0;
    xSemaphoreGive(s_pcm_mutex);
}

static esp_err_t audio_pcm_ensure_allocated(void)
{
    static const size_t psram_ring_sizes[] = {
        AUDIO_PCM_RING_SIZE,
        768 * 1024,
        512 * 1024,
        384 * 1024,
        256 * 1024,
        192 * 1024,
        160 * 1024,
    };
    static const size_t internal_ring_sizes[] = {
        128 * 1024,
        96 * 1024,
        64 * 1024,
        40 * 1024,
        32 * 1024,
        24 * 1024,
        16 * 1024,
    };

    if (s_pcm_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
    if (s_pcm_ring != NULL) {
        xSemaphoreGive(s_pcm_mutex);
        return ESP_OK;
    }
    xSemaphoreGive(s_pcm_mutex);

    for (size_t i = 0; i < (sizeof(psram_ring_sizes) / sizeof(psram_ring_sizes[0])); i++) {
        uint8_t *ring = NULL;

#if CONFIG_SPIRAM
        ring = heap_caps_malloc(psram_ring_sizes[i], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ring != NULL) {
            xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
            if (s_pcm_ring == NULL) {
                s_pcm_ring = ring;
                s_pcm_ring_capacity = psram_ring_sizes[i];
                s_pcm_ring_in_psram = true;
                s_pcm_ring_head = 0;
                s_pcm_ring_tail = 0;
                s_pcm_ring_size = 0;
                xSemaphoreGive(s_pcm_mutex);
                ESP_LOGI(TAG, "pcm ring capacity=%u bytes from %s, free=%u, largest=%u",
                         (unsigned)s_pcm_ring_capacity,
                         "PSRAM",
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
                return ESP_OK;
            }
            xSemaphoreGive(s_pcm_mutex);
            free(ring);
            return ESP_OK;
        }
#endif
    }

    for (size_t i = 0; i < (sizeof(internal_ring_sizes) / sizeof(internal_ring_sizes[0])); i++) {
        uint8_t *ring = heap_caps_malloc(internal_ring_sizes[i], MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (ring == NULL) {
            continue;
        }
        xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
        if (s_pcm_ring == NULL) {
            s_pcm_ring = ring;
            s_pcm_ring_capacity = internal_ring_sizes[i];
            s_pcm_ring_in_psram = false;
            s_pcm_ring_head = 0;
            s_pcm_ring_tail = 0;
            s_pcm_ring_size = 0;
            xSemaphoreGive(s_pcm_mutex);
            ESP_LOGI(TAG, "pcm ring capacity=%u bytes from %s, free=%u, largest=%u",
                     (unsigned)s_pcm_ring_capacity,
                     "internal",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            return ESP_OK;
        }
        xSemaphoreGive(s_pcm_mutex);

        free(ring);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "alloc pcm ring failed, free=%u, largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return ESP_ERR_NO_MEM;
}

static void audio_pcm_reset_or_release(void)
{
    if (s_pcm_ring_in_psram) {
        audio_pcm_reset();
    } else {
        audio_pcm_release();
    }
}

static void audio_pcm_release(void)
{
    uint8_t *ring = NULL;

    if (s_pcm_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
    ring = s_pcm_ring;
    s_pcm_ring = NULL;
    s_pcm_ring_capacity = 0;
    s_pcm_ring_in_psram = false;
    s_pcm_ring_head = 0;
    s_pcm_ring_tail = 0;
    s_pcm_ring_size = 0;
    s_pcm_format_ready = false;
    s_pcm_sample_rate = 0;
    s_pcm_channels = 0;
    s_pcm_bits_per_sample = 0;
    xSemaphoreGive(s_pcm_mutex);

    if (ring != NULL) {
        free(ring);
    }
}

static void audio_pcm_set_format(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    if (s_pcm_mutex == NULL || s_pcm_ring == NULL) {
        return;
    }
    xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
    s_pcm_sample_rate = sample_rate;
    s_pcm_channels = channels;
    s_pcm_bits_per_sample = bits_per_sample;
    s_pcm_format_ready = true;
    xSemaphoreGive(s_pcm_mutex);
}

static bool audio_pcm_get_format(uint32_t *sample_rate, uint8_t *channels, uint8_t *bits_per_sample)
{
    bool ready = false;

    if (s_pcm_mutex == NULL || s_pcm_ring == NULL) {
        return false;
    }
    xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
    ready = s_pcm_format_ready;
    if (ready) {
        *sample_rate = s_pcm_sample_rate;
        *channels = s_pcm_channels;
        *bits_per_sample = s_pcm_bits_per_sample;
    }
    xSemaphoreGive(s_pcm_mutex);
    return ready;
}

static size_t audio_pcm_bytes_available(void)
{
    size_t available = 0;

    if (s_pcm_mutex == NULL || s_pcm_ring == NULL) {
        return 0;
    }
    xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
    available = s_pcm_ring_size;
    xSemaphoreGive(s_pcm_mutex);
    return available;
}

static size_t audio_pcm_pop(uint8_t *dst, size_t len)
{
    size_t copied = 0;

    if (s_pcm_mutex == NULL || s_pcm_ring == NULL || dst == NULL || len == 0) {
        return 0;
    }

    xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
    while (copied < len && s_pcm_ring_size > 0) {
        size_t contiguous = s_pcm_ring_capacity - s_pcm_ring_tail;
        size_t chunk = len - copied;
        if (chunk > s_pcm_ring_size) {
            chunk = s_pcm_ring_size;
        }
        if (chunk > contiguous) {
            chunk = contiguous;
        }
        memcpy(dst + copied, s_pcm_ring + s_pcm_ring_tail, chunk);
        s_pcm_ring_tail = (s_pcm_ring_tail + chunk) % s_pcm_ring_capacity;
        s_pcm_ring_size -= chunk;
        copied += chunk;
    }
    xSemaphoreGive(s_pcm_mutex);

    return copied;
}

static esp_err_t audio_pcm_push(const uint8_t *src, size_t len, uint32_t token, size_t station_index)
{
    size_t copied = 0;

    if (s_pcm_mutex == NULL || src == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(audio_pcm_ensure_allocated(), TAG, "alloc pcm ring failed");

    while (copied < len) {
        bool wrote = false;

        if (audio_request_changed(token, station_index)) {
            return ESP_ERR_INVALID_STATE;
        }

        xSemaphoreTake(s_pcm_mutex, portMAX_DELAY);
        if (s_pcm_ring_size < s_pcm_ring_capacity) {
            size_t free_bytes = s_pcm_ring_capacity - s_pcm_ring_size;
            size_t contiguous = s_pcm_ring_capacity - s_pcm_ring_head;
            size_t chunk = len - copied;
            if (chunk > free_bytes) {
                chunk = free_bytes;
            }
            if (chunk > contiguous) {
                chunk = contiguous;
            }
            memcpy(s_pcm_ring + s_pcm_ring_head, src + copied, chunk);
            s_pcm_ring_head = (s_pcm_ring_head + chunk) % s_pcm_ring_capacity;
            s_pcm_ring_size += chunk;
            copied += chunk;
            wrote = true;
        }
        xSemaphoreGive(s_pcm_mutex);

        if (!wrote) {
            vTaskDelay(pdMS_TO_TICKS(AUDIO_PCM_POLL_MS));
        }
    }

    return ESP_OK;
}

static esp_err_t audio_output_write_all(const uint8_t *data, size_t len)
{
    size_t total_written = 0;

    while (total_written < len) {
        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_handle, data + total_written, len - total_written,
                                          &bytes_written, pdMS_TO_TICKS(AUDIO_I2S_WRITE_TIMEOUT_MS));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write failed ret=%d written=%lu remain=%lu",
                     ret, (unsigned long)bytes_written, (unsigned long)(len - total_written));
            return ret;
        }
        if (bytes_written == 0) {
            ESP_LOGE(TAG, "i2s_channel_write timeout remain=%lu", (unsigned long)(len - total_written));
            return ESP_ERR_TIMEOUT;
        }
        total_written += bytes_written;
    }

    return ESP_OK;
}

static esp_err_t audio_output_ensure(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    if (sample_rate == 0 || (channels != 1 && channels != 2) || bits_per_sample != 16) {
        return ESP_ERR_INVALID_ARG;
    }
    // Reuse the existing TX channel when decoder output format stays unchanged.
    if (s_tx_handle != NULL && s_i2s_rate == sample_rate && s_i2s_channels == channels) {
        return ESP_OK;
    }

    audio_output_stop();

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_I2S_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_I2S_DMA_FRAME_NUM;
    chan_cfg.auto_clear_after_cb = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL), TAG, "alloc i2s channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_MAX98357_BCLK_PIN,
            .ws = AUDIO_MAX98357_LRC_PIN,
            .dout = AUDIO_MAX98357_DIN_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = 0,
                .bclk_inv = 0,
                .ws_inv = 0,
            },
        },
    };

    if (i2s_channel_init_std_mode(s_tx_handle, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "init i2s std failed");
        goto fail;
    }
    if (i2s_channel_enable(s_tx_handle) != ESP_OK) {
        ESP_LOGE(TAG, "enable i2s failed");
        goto fail;
    }

    s_i2s_rate = sample_rate;
    s_i2s_channels = channels;
    return ESP_OK;

fail:
    audio_output_stop();
    return ESP_FAIL;
}

static esp_err_t audio_output_write(const uint8_t *pcm_data, size_t pcm_len, uint8_t channels)
{
    if (pcm_data == NULL || pcm_len == 0) {
        return ESP_OK;
    }

    if ((pcm_len % sizeof(int16_t)) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (channels == 2) {
        return audio_output_write_all(pcm_data, pcm_len);
    }

    if (pcm_len > AUDIO_PCM_BUFFER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    // MAX98357 is driven in stereo mode, so duplicate mono PCM into L/R slots.
    static uint8_t stereo_buf[AUDIO_MONO_BUFFER_SIZE];
    int16_t *src = (int16_t *)pcm_data;
    int16_t *dst = (int16_t *)stereo_buf;
    size_t samples = pcm_len / sizeof(int16_t);
    for (size_t i = 0; i < samples; i++) {
        dst[2 * i] = src[i];
        dst[2 * i + 1] = src[i];
    }

    return audio_output_write_all(stereo_buf, pcm_len * 2);
}

static void audio_writer_task(void *arg)
{
    (void)arg;

    static uint8_t pcm_out[AUDIO_PCM_READ_CHUNK_SIZE];
    bool primed = false;
    bool warmed_up = false;
    uint32_t active_token = 0;
    int64_t underrun_since_us = 0;

    while (1) {
        if (!audio_is_active_locked()) {
            if (primed || s_tx_handle != NULL) {
                audio_output_stop();
            }
            primed = false;
            warmed_up = false;
            underrun_since_us = 0;
            active_token = audio_get_request_token_locked();
            vTaskDelay(pdMS_TO_TICKS(AUDIO_PCM_POLL_MS));
            continue;
        }

        uint32_t token = audio_get_request_token_locked();
        if (token != active_token) {
            active_token = token;
            primed = false;
            warmed_up = false;
            underrun_since_us = 0;
            audio_output_stop();
        }

        uint32_t sample_rate = 0;
        uint8_t channels = 0;
        uint8_t bits_per_sample = 0;
        if (!audio_pcm_get_format(&sample_rate, &channels, &bits_per_sample)) {
            vTaskDelay(pdMS_TO_TICKS(AUDIO_PCM_POLL_MS));
            continue;
        }

        size_t available = audio_pcm_bytes_available();
        if (!primed) {
            size_t target = warmed_up ? audio_pcm_resume_target() : audio_pcm_prebuffer_target();
            if (available < target) {
                if (audio_get_state_locked() != AUDIO_STATE_BUFFERING) {
                    audio_set_state(AUDIO_STATE_BUFFERING);
                }
                vTaskDelay(pdMS_TO_TICKS(AUDIO_PCM_POLL_MS));
                continue;
            }

            if (audio_output_ensure(sample_rate, channels, bits_per_sample) != ESP_OK) {
                audio_set_state(AUDIO_STATE_ERROR);
                vTaskDelay(pdMS_TO_TICKS(AUDIO_PCM_POLL_MS));
                continue;
            }

            primed = true;
            warmed_up = true;
            underrun_since_us = 0;
            audio_set_state(AUDIO_STATE_PLAYING);
        }

        if (available == 0) {
            int64_t now_us = esp_timer_get_time();
            if (underrun_since_us == 0) {
                underrun_since_us = now_us;
            }
            if ((now_us - underrun_since_us) >= (AUDIO_PCM_UNDERRUN_GRACE_MS * 1000LL)) {
                ESP_LOGW(TAG, "pcm underrun after %lld ms", (long long)((now_us - underrun_since_us) / 1000));
                primed = false;
                underrun_since_us = 0;
                audio_set_state(AUDIO_STATE_BUFFERING);
            }
            vTaskDelay(pdMS_TO_TICKS(AUDIO_PCM_POLL_MS));
            continue;
        }
        underrun_since_us = 0;

        size_t to_read = available > sizeof(pcm_out) ? sizeof(pcm_out) : available;
        size_t got = audio_pcm_pop(pcm_out, to_read);
        if (got == 0) {
            vTaskDelay(pdMS_TO_TICKS(AUDIO_PCM_POLL_MS));
            continue;
        }

        if (audio_output_write(pcm_out, got, channels) != ESP_OK) {
            primed = false;
            underrun_since_us = 0;
            audio_output_stop();
            audio_set_state(AUDIO_STATE_BUFFERING);
            vTaskDelay(pdMS_TO_TICKS(AUDIO_PCM_POLL_MS));
        }
    }
}

static void icy_filter_reset(icy_filter_t *filter, int metaint)
{
    memset(filter, 0, sizeof(*filter));
    filter->metaint = metaint;
    filter->audio_bytes_left = metaint;
}

static size_t icy_filter_append(icy_filter_t *filter, const uint8_t *input, size_t input_len,
                                uint8_t *output, size_t output_cap)
{
    // Some radio streams insert ICY metadata blocks in-band. Strip them before MP3 decode.
    if (filter->metaint <= 0) {
        size_t copy_len = input_len > output_cap ? output_cap : input_len;
        memcpy(output, input, copy_len);
        return copy_len;
    }

    size_t in_pos = 0;
    size_t out_pos = 0;

    while (in_pos < input_len && out_pos < output_cap) {
        if (filter->metadata_bytes_left > 0) {
            size_t skip_len = input_len - in_pos;
            if (skip_len > (size_t)filter->metadata_bytes_left) {
                skip_len = (size_t)filter->metadata_bytes_left;
            }
            in_pos += skip_len;
            filter->metadata_bytes_left -= (int)skip_len;
            if (filter->metadata_bytes_left == 0) {
                filter->audio_bytes_left = filter->metaint;
            }
            continue;
        }

        if (filter->expect_metadata_len) {
            filter->metadata_bytes_left = input[in_pos++] * 16;
            filter->expect_metadata_len = false;
            if (filter->metadata_bytes_left == 0) {
                filter->audio_bytes_left = filter->metaint;
            }
            continue;
        }

        size_t copy_len = input_len - in_pos;
        if (copy_len > (size_t)filter->audio_bytes_left) {
            copy_len = (size_t)filter->audio_bytes_left;
        }
        if (copy_len > output_cap - out_pos) {
            copy_len = output_cap - out_pos;
        }
        memcpy(output + out_pos, input + in_pos, copy_len);
        in_pos += copy_len;
        out_pos += copy_len;
        filter->audio_bytes_left -= (int)copy_len;
        if (filter->audio_bytes_left == 0) {
            filter->expect_metadata_len = true;
        }
    }

    return out_pos;
}

static esp_http_client_handle_t audio_stream_open(const char *url, icy_filter_t *filter)
{
    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = AUDIO_HTTP_CHUNK_SIZE,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "create http client failed");
        return NULL;
    }

    esp_http_client_set_header(client, "Icy-MetaData", "1");
    esp_http_client_set_header(client, "User-Agent", "MOYU_Badge/1.0");

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "open stream failed");
        esp_http_client_cleanup(client);
        return NULL;
    }

    if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGW(TAG, "fetch headers failed");
    }

    char *metaint_value = NULL;
    int metaint = 0;
    if (esp_http_client_get_header(client, "icy-metaint", &metaint_value) == ESP_OK && metaint_value != NULL) {
        metaint = atoi(metaint_value);
    }
    icy_filter_reset(filter, metaint);
    ESP_LOGI(TAG, "stream metaint=%d", metaint);
    return client;
}

static void audio_stream_close(esp_http_client_handle_t client)
{
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
}

static esp_err_t audio_decoder_prepare(esp_audio_simple_dec_handle_t *decoder)
{
    if (!s_decoder_registered) {
        // Register once globally; each playback session still owns its own decoder instance.
        ESP_RETURN_ON_ERROR(esp_audio_dec_register_default(), TAG, "register decoders failed");
        ESP_RETURN_ON_ERROR(esp_audio_simple_dec_register_default(), TAG, "register simple decoders failed");
        s_decoder_registered = true;
    }

    if (*decoder == NULL) {
        esp_audio_simple_dec_cfg_t dec_cfg = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
            .dec_cfg = NULL,
            .cfg_size = 0,
            .use_frame_dec = false,
        };
        ESP_RETURN_ON_ERROR(esp_audio_simple_dec_open(&dec_cfg, decoder), TAG, "open mp3 decoder failed");
    } else {
        ESP_RETURN_ON_ERROR(esp_audio_simple_dec_reset(*decoder), TAG, "reset mp3 decoder failed");
    }
    return ESP_OK;
}

static bool audio_request_changed(uint32_t token, size_t station_index)
{
    return (!audio_is_active_locked()) ||
           (audio_get_request_token_locked() != token) ||
           (audio_get_station_index_locked() != station_index);
}

static esp_err_t audio_play_station(size_t station_index, uint32_t token)
{
    static uint8_t net_buf[AUDIO_HTTP_CHUNK_SIZE];
    static uint8_t filtered_buf[AUDIO_HTTP_CHUNK_SIZE];
    static uint8_t raw_buf[AUDIO_RAW_BUFFER_SIZE];
    static uint8_t pcm_buf[AUDIO_PCM_BUFFER_SIZE];

    esp_audio_simple_dec_handle_t decoder = NULL;
    esp_http_client_handle_t client = NULL;
    icy_filter_t filter = {0};
    esp_err_t ret = ESP_FAIL;
    size_t raw_len = 0;
    int consecutive_decode_errors = 0;
    int consecutive_empty_reads = 0;
    bool logged_first_pcm = false;

    ESP_RETURN_ON_ERROR(audio_decoder_prepare(&decoder), TAG, "decoder setup failed");
    audio_pcm_reset_or_release();

    client = audio_stream_open(s_stations[station_index].url, &filter);
    if (client == NULL) {
        esp_audio_simple_dec_close(decoder);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "start station[%u]: %s", (unsigned)station_index, s_stations[station_index].name);

    audio_set_state(AUDIO_STATE_BUFFERING);

    while (!audio_request_changed(token, station_index)) {
        if (!audio_wifi_is_ready()) {
            ret = ESP_ERR_INVALID_STATE;
            audio_set_state(AUDIO_STATE_NO_WIFI);
            goto cleanup;
        }

        int read_len = esp_http_client_read(client, (char *)net_buf, sizeof(net_buf));
        if (read_len < 0) {
            ESP_LOGW(TAG, "stream read failed: %d", read_len);
            ret = ESP_FAIL;
            goto cleanup;
        }
        if (read_len == 0) {
            consecutive_empty_reads++;
            if (consecutive_empty_reads >= AUDIO_MAX_CONSECUTIVE_EMPTY_READS) {
                ESP_LOGW(TAG, "stream stalled for %d reads, pcm=%lu",
                         consecutive_empty_reads, (unsigned long)audio_pcm_bytes_available());
                ret = ESP_FAIL;
                goto cleanup;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        consecutive_empty_reads = 0;

        size_t audio_len = icy_filter_append(&filter, net_buf, (size_t)read_len, filtered_buf, sizeof(filtered_buf));
        if (audio_len == 0) {
            continue;
        }

        // Keep a small tail of undecoded MP3 bytes so the simple decoder can recover frame boundaries.
        if (raw_len + audio_len > sizeof(raw_buf)) {
            size_t keep_len = raw_len > 1024 ? 1024 : raw_len;
            memmove(raw_buf, raw_buf + raw_len - keep_len, keep_len);
            raw_len = keep_len;
        }
        memcpy(raw_buf + raw_len, filtered_buf, audio_len);
        raw_len += audio_len;

        esp_audio_simple_dec_raw_t raw = {
            .buffer = raw_buf,
            .len = raw_len,
            .eos = false,
            .consumed = 0,
            .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
        };

        while (raw.len > 0 && !audio_request_changed(token, station_index)) {
            esp_audio_simple_dec_out_t out_frame = {
                .buffer = pcm_buf,
                .len = sizeof(pcm_buf),
                .needed_size = 0,
                .decoded_size = 0,
            };
            raw.consumed = 0;

            ret = esp_audio_simple_dec_process(decoder, &raw, &out_frame);
            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                ESP_LOGW(TAG, "pcm buffer too small: %lu", (unsigned long)out_frame.needed_size);
                ret = ESP_ERR_NO_MEM;
                goto cleanup;
            }
            if (ret != ESP_AUDIO_ERR_OK) {
                consecutive_decode_errors++;
                ESP_LOGW(TAG, "decode failed: %d, raw_len=%lu, consumed=%lu, streak=%d",
                         ret, (unsigned long)raw.len, (unsigned long)raw.consumed, consecutive_decode_errors);
                if (consecutive_decode_errors >= AUDIO_MAX_CONSECUTIVE_DECODE_ERRORS || raw.len <= 1) {
                    ret = ESP_FAIL;
                    goto cleanup;
                }
                // Live radio may splice or corrupt MP3 frames. Drop one byte and search for the next sync word.
                raw.buffer += 1;
                raw.len -= 1;
                raw.consumed = 0;
                continue;
            }

            if (out_frame.decoded_size > 0) {
                esp_audio_simple_dec_info_t info = {0};
                if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK) {
                    ESP_LOGW(TAG, "decoder info unavailable");
                    ret = ESP_FAIL;
                    goto cleanup;
                }
                if (info.bits_per_sample != 16 || (info.channel != 1 && info.channel != 2) || info.sample_rate == 0) {
                    ESP_LOGE(TAG, "unsupported pcm format: rate=%lu bits=%u ch=%u",
                             (unsigned long)info.sample_rate, info.bits_per_sample, info.channel);
                    ret = ESP_ERR_NOT_SUPPORTED;
                    goto cleanup;
                }

                audio_pcm_set_format(info.sample_rate, info.channel, info.bits_per_sample);
                ESP_GOTO_ON_ERROR(audio_pcm_push(out_frame.buffer, out_frame.decoded_size, token, station_index),
                                  cleanup, TAG, "pcm buffer push failed");
                if (!logged_first_pcm) {
                    ESP_LOGI(TAG, "first pcm: rate=%lu bits=%u ch=%u size=%lu",
                             (unsigned long)info.sample_rate, info.bits_per_sample, info.channel,
                             (unsigned long)out_frame.decoded_size);
                    logged_first_pcm = true;
                }
            }

            if (raw.consumed > 0 || out_frame.decoded_size > 0) {
                consecutive_decode_errors = 0;
            }

            if (raw.consumed == 0 && out_frame.decoded_size == 0) {
                break;
            }

            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
        }

        if (raw.len > 0 && raw.buffer != raw_buf) {
            memmove(raw_buf, raw.buffer, raw.len);
        }
        raw_len = raw.len;
    }

    ret = ESP_OK;

cleanup:
    audio_stream_close(client);
    audio_pcm_reset_or_release();
    if (decoder != NULL) {
        esp_audio_simple_dec_close(decoder);
    }
    return ret;
}

void audio_task(void *arg)
{
    (void)arg;

    while (1) {
        if (!audio_is_active_locked()) {
            if (audio_get_state_locked() != AUDIO_STATE_IDLE) {
                audio_set_state(AUDIO_STATE_IDLE);
            }
            // Sleep until UI/key logic explicitly asks for playback work again.
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        if (!audio_wifi_is_ready()) {
            audio_set_state(AUDIO_STATE_NO_WIFI);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }

        size_t station_index = audio_get_station_index_locked();
        uint32_t token = audio_get_request_token_locked();
        esp_err_t ret = audio_play_station(station_index, token);

        if (!audio_is_active_locked()) {
            audio_set_state(AUDIO_STATE_IDLE);
            continue;
        }

        if (!audio_wifi_is_ready()) {
            audio_set_state(AUDIO_STATE_NO_WIFI);
            continue;
        }

        if (audio_get_request_token_locked() != token) {
            continue;
        }

        if (ret != ESP_OK) {
            audio_set_state(AUDIO_STATE_ERROR);
            // Back off briefly so transient HTTP failures do not spin the CPU.
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AUDIO_RETRY_DELAY_MS));
        }
    }
}

void audio_player_init(void)
{
    if (s_pcm_mutex == NULL) {
        s_pcm_mutex = xSemaphoreCreateMutex();
    }
    if (s_audio_writer_task == NULL) {
        xTaskCreate(audio_writer_task, "audio_writer", AUDIO_TASK_STACK_SIZE, NULL,
                    AUDIO_TASK_PRIORITY + 1, &s_audio_writer_task);
    }
    if (s_audio_task == NULL) {
        xTaskCreate(audio_task, "audio_task", AUDIO_TASK_STACK_SIZE, NULL, AUDIO_TASK_PRIORITY, &s_audio_task);
    }
}


void audio_player_enter_radio_mode(void)
{
    portENTER_CRITICAL(&s_audio_lock);
    s_radio_active = true;
    s_request_token++;
    portEXIT_CRITICAL(&s_audio_lock);
    audio_pcm_reset_or_release();
    audio_set_state(audio_wifi_is_ready() ? AUDIO_STATE_BUFFERING : AUDIO_STATE_NO_WIFI);
    audio_notify_task();
}

void audio_player_exit_radio_mode(void)
{
    portENTER_CRITICAL(&s_audio_lock);
    s_radio_active = false;
    s_request_token++;
    portEXIT_CRITICAL(&s_audio_lock);
    audio_pcm_release();
    audio_set_state(AUDIO_STATE_IDLE);
    audio_notify_task();
}

void audio_player_next_station(void)
{
    portENTER_CRITICAL(&s_audio_lock);
    s_station_index = (s_station_index + 1) % (sizeof(s_stations) / sizeof(s_stations[0]));
    s_request_token++;
    portEXIT_CRITICAL(&s_audio_lock);
    audio_pcm_reset_or_release();
    if (audio_is_active_locked()) {
        audio_set_state(audio_wifi_is_ready() ? AUDIO_STATE_BUFFERING : AUDIO_STATE_NO_WIFI);
    }
    audio_notify_task();
}

audio_state_t audio_player_get_state(void)
{
    return audio_get_state_locked();
}

const audio_station_t *audio_player_get_station(void)
{
    return &s_stations[audio_get_station_index_locked()];
}

bool audio_player_is_active(void)
{
    return audio_is_active_locked();
}
