#include "audio_player.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "decoder/esp_audio_dec.h"
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
#include "max98357.h"
#include "ws2812.h"

static const audio_station_t s_stations[] = {
    { "星河音乐", "http://lhttp.qingting.fm/live/20210755/64k.mp3" },
    { "上海动感101", "http://lhttp.qingting.fm/live/274/64k.mp3" },
    { "深圳飞扬971", "http://lhttp.qingting.fm/live/1271/64k.mp3" },
    { "上海流行音乐", "http://lhttp.qingting.fm/live/273/64k.mp3" },
    { "怀旧好声音", "http://lhttp.qingting.fm/live/1223/64k.mp3" },
    { "广东音乐之声", "http://lhttp.qingting.fm/live/1260/64k.mp3" },
    { "好听 1055", "http://lhttp.qingting.fm/live/4885/64k.mp3"},
    { "欧美音乐88.7", "http://lhttp.qingting.fm/live/15318703/64k.mp3" },
};

static TaskHandle_t s_audio_task_handle = NULL;
static TaskHandle_t s_audio_writer_task = NULL;
static portMUX_TYPE s_audio_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile audio_state_t s_audio_state = AUDIO_STATE_IDLE;
static volatile bool s_radio_active = false;
static volatile uint32_t s_request_token = 0;
static volatile size_t s_station_index = 0;
static volatile uint8_t s_volume_percent = AUDIO_DEFAULT_VOLUME_PERCENT;
static bool s_decoder_registered = false;
static TaskHandle_t s_audio_led_task = NULL;

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
static int16_t s_audio_lp_prev_l = 0;
static int16_t s_audio_lp_prev_r = 0;
static int16_t s_audio_prev_sample_l = 0;
static int16_t s_audio_prev_sample_r = 0;
static size_t s_audio_fade_samples_left = 0;

static bool audio_request_changed(uint32_t token, size_t station_index);
static esp_err_t audio_pcm_ensure_allocated(void);
static void audio_pcm_release(void);
static void audio_pcm_reset_or_release(void);
static void audio_led_task(void *arg);
static void audio_led_show_rainbow_breathing(uint32_t ms_now);
static void audio_led_hsv_to_rgb(uint16_t hue, uint8_t sat, uint8_t val,
                                 uint8_t *r, uint8_t *g, uint8_t *b);
static void audio_declick_output(uint8_t *pcm_data, size_t pcm_len, uint8_t channels);
static void audio_smooth_output(uint8_t *pcm_data, size_t pcm_len, uint8_t channels);
static void audio_apply_fade_in(uint8_t *pcm_data, size_t pcm_len, uint8_t channels);

#define AUDIO_CLICK_DELTA_THRESHOLD 12000

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
        ESP_LOGI(AUDIO_PLAYER_TAG, "state: %s -> %s", audio_state_to_string(old_state), audio_state_to_string(state));
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

static uint8_t audio_get_volume_locked(void)
{
    uint8_t volume;

    portENTER_CRITICAL(&s_audio_lock);
    volume = s_volume_percent;
    portEXIT_CRITICAL(&s_audio_lock);
    return volume;
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
    if (s_audio_task_handle != NULL) {
        xTaskNotifyGive(s_audio_task_handle);
    }
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
    s_audio_lp_prev_l = 0;
    s_audio_lp_prev_r = 0;
    s_audio_prev_sample_l = 0;
    s_audio_prev_sample_r = 0;
    s_audio_fade_samples_left = AUDIO_FADE_IN_SAMPLES;
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
            return ESP_OK;
        }
        xSemaphoreGive(s_pcm_mutex);

        free(ring);
        return ESP_OK;
    }

    ESP_LOGE(AUDIO_PLAYER_TAG, "alloc pcm ring failed, free=%u, largest=%u",
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
    s_audio_lp_prev_l = 0;
    s_audio_lp_prev_r = 0;
    s_audio_prev_sample_l = 0;
    s_audio_prev_sample_r = 0;
    s_audio_fade_samples_left = AUDIO_FADE_IN_SAMPLES;
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

    ESP_RETURN_ON_ERROR(audio_pcm_ensure_allocated(), AUDIO_PLAYER_TAG, "alloc pcm ring failed");

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

static void audio_declick_output(uint8_t *pcm_data, size_t pcm_len, uint8_t channels)
{
    if (pcm_data == NULL || pcm_len < sizeof(int16_t) || (channels != 1 && channels != 2)) {
        return;
    }

    int16_t *samples = (int16_t *)pcm_data;
    size_t sample_count = pcm_len / sizeof(int16_t);

    if (channels == 1) {
        for (size_t i = 0; i < sample_count; i++) {
            int32_t current = samples[i];
            int32_t delta = current - s_audio_prev_sample_l;
            if (delta > AUDIO_CLICK_DELTA_THRESHOLD || delta < -AUDIO_CLICK_DELTA_THRESHOLD) {
                current = s_audio_prev_sample_l + (delta / 4);
            }
            samples[i] = (int16_t)current;
            s_audio_prev_sample_l = (int16_t)current;
        }
        return;
    }

    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        int32_t current_l = samples[i];
        int32_t current_r = samples[i + 1];
        int32_t delta_l = current_l - s_audio_prev_sample_l;
        int32_t delta_r = current_r - s_audio_prev_sample_r;

        if (delta_l > AUDIO_CLICK_DELTA_THRESHOLD || delta_l < -AUDIO_CLICK_DELTA_THRESHOLD) {
            current_l = s_audio_prev_sample_l + (delta_l / 4);
        }
        if (delta_r > AUDIO_CLICK_DELTA_THRESHOLD || delta_r < -AUDIO_CLICK_DELTA_THRESHOLD) {
            current_r = s_audio_prev_sample_r + (delta_r / 4);
        }

        samples[i] = (int16_t)current_l;
        samples[i + 1] = (int16_t)current_r;
        s_audio_prev_sample_l = (int16_t)current_l;
        s_audio_prev_sample_r = (int16_t)current_r;
    }
}

static void audio_apply_volume(uint8_t *pcm_data, size_t pcm_len, uint8_t volume_percent)
{
    if (pcm_data == NULL || pcm_len == 0 || volume_percent >= 100) {
        return;
    }

    int16_t *samples = (int16_t *)pcm_data;
    size_t sample_count = pcm_len / sizeof(int16_t);

    for (size_t i = 0; i < sample_count; i++) {
        int32_t scaled = ((int32_t)samples[i] * volume_percent) / 100;
        if (scaled > INT16_MAX) {
            scaled = INT16_MAX;
        } else if (scaled < INT16_MIN) {
            scaled = INT16_MIN;
        }
        samples[i] = (int16_t)scaled;
    }
}

static void audio_smooth_output(uint8_t *pcm_data, size_t pcm_len, uint8_t channels)
{
    if (pcm_data == NULL || pcm_len < sizeof(int16_t) || (channels != 1 && channels != 2)) {
        return;
    }

    int16_t *samples = (int16_t *)pcm_data;
    size_t sample_count = pcm_len / sizeof(int16_t);

    if (channels == 1) {
        for (size_t i = 0; i < sample_count; i++) {
            int32_t filtered = ((int32_t)s_audio_lp_prev_l * 3 + samples[i]) / 4;
            s_audio_lp_prev_l = (int16_t)filtered;
            samples[i] = (int16_t)filtered;
        }
        return;
    }

    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        int32_t filtered_l = ((int32_t)s_audio_lp_prev_l * 3 + samples[i]) / 4;
        int32_t filtered_r = ((int32_t)s_audio_lp_prev_r * 3 + samples[i + 1]) / 4;
        s_audio_lp_prev_l = (int16_t)filtered_l;
        s_audio_lp_prev_r = (int16_t)filtered_r;
        samples[i] = (int16_t)filtered_l;
        samples[i + 1] = (int16_t)filtered_r;
    }
}

static void audio_apply_fade_in(uint8_t *pcm_data, size_t pcm_len, uint8_t channels)
{
    if (pcm_data == NULL || pcm_len < sizeof(int16_t) || s_audio_fade_samples_left == 0) {
        return;
    }

    int16_t *samples = (int16_t *)pcm_data;
    size_t frames = pcm_len / (sizeof(int16_t) * channels);

    for (size_t frame = 0; frame < frames && s_audio_fade_samples_left > 0; frame++) {
        size_t done = AUDIO_FADE_IN_SAMPLES - s_audio_fade_samples_left;
        int32_t gain = (int32_t)((done * 255) / AUDIO_FADE_IN_SAMPLES);
        for (uint8_t ch = 0; ch < channels; ch++) {
            size_t idx = frame * channels + ch;
            samples[idx] = (int16_t)(((int32_t)samples[idx] * gain) / 255);
        }
        s_audio_fade_samples_left--;
    }
}

static esp_err_t audio_output_write(uint8_t *pcm_data, size_t pcm_len, uint8_t channels)
{
    if (pcm_data == NULL || pcm_len == 0) {
        return ESP_OK;
    }

    audio_declick_output(pcm_data, pcm_len, channels);
    audio_smooth_output(pcm_data, pcm_len, channels);
    audio_apply_volume(pcm_data, pcm_len, audio_get_volume_locked());
    audio_apply_fade_in(pcm_data, pcm_len, channels);
    return max98357_write(pcm_data, pcm_len, channels, pdMS_TO_TICKS(AUDIO_I2S_WRITE_TIMEOUT_MS));
}

static void audio_led_hsv_to_rgb(uint16_t hue, uint8_t sat, uint8_t val,
                                 uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region = (uint8_t)(hue / 43);
    uint8_t remainder = (uint8_t)((hue - (region * 43)) * 6);
    uint8_t p = (uint8_t)((val * (255 - sat)) / 255);
    uint8_t q = (uint8_t)((val * (255 - ((sat * remainder) / 255))) / 255);
    uint8_t t = (uint8_t)((val * (255 - ((sat * (255 - remainder)) / 255))) / 255);

    switch (region) {
        case 0:
            *r = val; *g = t;   *b = p;
            break;
        case 1:
            *r = q;   *g = val; *b = p;
            break;
        case 2:
            *r = p;   *g = val; *b = t;
            break;
        case 3:
            *r = p;   *g = q;   *b = val;
            break;
        case 4:
            *r = t;   *g = p;   *b = val;
            break;
        default:
            *r = val; *g = p;   *b = q;
            break;
    }
}

static void audio_led_show_rainbow_breathing(uint32_t ms_now)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint16_t hue = (uint16_t)((ms_now / 12) % 256);
    uint32_t phase = ms_now % 2000;
    uint8_t brightness;

    if (phase < 1000) {
        brightness = (uint8_t)(40 + (phase * 175) / 1000);
    } else {
        brightness = (uint8_t)(40 + ((2000 - phase) * 175) / 1000);
    }

    audio_led_hsv_to_rgb(hue, 255, brightness, &r, &g, &b);
    ws2812_flash(r, g, b);
}

static void audio_led_task(void *arg)
{
    (void)arg;

    audio_state_t last_state = AUDIO_STATE_IDLE;
    bool buffering_color_ready = false;

    while (1) {
        bool active = audio_player_is_active();
        audio_state_t state = audio_player_get_state();
        uint32_t ms_now = (uint32_t)(esp_timer_get_time() / 1000);

        if (!active) {
            if (last_state != AUDIO_STATE_IDLE) {
                ws2812_off();
            }
            buffering_color_ready = false;
            last_state = AUDIO_STATE_IDLE;
            vTaskDelay(pdMS_TO_TICKS(AUDIO_LED_UPDATE_MS));
            continue;
        }

        if (state != last_state && state != AUDIO_STATE_BUFFERING) {
            buffering_color_ready = false;
        }

        switch (state) {
            case AUDIO_STATE_BUFFERING:
                if (!buffering_color_ready || last_state != AUDIO_STATE_BUFFERING) {
                    uint8_t dummy_r = 0;
                    uint8_t dummy_g = 0;
                    uint8_t dummy_b = 0;
                    do {
                        dummy_r = (uint8_t)(esp_random() & 0xFF);
                        dummy_g = (uint8_t)((esp_random() >> 8) & 0xFF);
                        dummy_b = (uint8_t)((esp_random() >> 16) & 0xFF);
                    } while ((dummy_r + dummy_g + dummy_b) < 120);
                    ws2812_flash(dummy_r, dummy_g, dummy_b);
                    buffering_color_ready = true;
                }
                break;
            case AUDIO_STATE_PLAYING:
                audio_led_show_rainbow_breathing(ms_now);
                buffering_color_ready = false;
                break;
            case AUDIO_STATE_IDLE:
            case AUDIO_STATE_ERROR:
            case AUDIO_STATE_NO_WIFI:
            default:
                ws2812_off();
                buffering_color_ready = false;
                break;
        }

        last_state = state;
        vTaskDelay(pdMS_TO_TICKS(AUDIO_LED_UPDATE_MS));
    }
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
            if (primed) {
                max98357_deinit();
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
            max98357_deinit();
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

            if (max98357_init(sample_rate, channels, bits_per_sample) != ESP_OK) {
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
            max98357_deinit();
            audio_set_state(AUDIO_STATE_BUFFERING);
            vTaskDelay(pdMS_TO_TICKS(AUDIO_PCM_POLL_MS));
        }
    }
}

static void icy_filter_reset(audio_icy_filter_t *filter, int metaint)
{
    memset(filter, 0, sizeof(*filter));
    filter->metaint = metaint;
    filter->audio_bytes_left = metaint;
}

static size_t icy_filter_append(audio_icy_filter_t *filter, const uint8_t *input, size_t input_len,
                                uint8_t *output, size_t output_cap)
{
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

static esp_http_client_handle_t audio_stream_open(const char *url, audio_icy_filter_t *filter)
{
    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = AUDIO_HTTP_CHUNK_SIZE,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        return NULL;
    }

    esp_http_client_set_header(client, "Icy-MetaData", "1");
    esp_http_client_set_header(client, "User-Agent", "MOYU_Badge/1.0");

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return NULL;
    }

    if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGW(AUDIO_PLAYER_TAG, "fetch headers failed");
    }

    char *metaint_value = NULL;
    int metaint = 0;
    if (esp_http_client_get_header(client, "icy-metaint", &metaint_value) == ESP_OK && metaint_value != NULL) {
        metaint = atoi(metaint_value);
    }
    icy_filter_reset(filter, metaint);
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
        ESP_RETURN_ON_ERROR(esp_audio_dec_register_default(), AUDIO_PLAYER_TAG, "register decoders failed");
        ESP_RETURN_ON_ERROR(esp_audio_simple_dec_register_default(), AUDIO_PLAYER_TAG, "register simple decoders failed");
        s_decoder_registered = true;
    }

    if (*decoder == NULL) {
        esp_audio_simple_dec_cfg_t dec_cfg = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
            .dec_cfg = NULL,
            .cfg_size = 0,
            .use_frame_dec = false,
        };
        ESP_RETURN_ON_ERROR(esp_audio_simple_dec_open(&dec_cfg, decoder), AUDIO_PLAYER_TAG, "open mp3 decoder failed");
    } else {
        ESP_RETURN_ON_ERROR(esp_audio_simple_dec_reset(*decoder), AUDIO_PLAYER_TAG, "reset mp3 decoder failed");
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
    audio_icy_filter_t filter = {0};
    esp_err_t ret = ESP_FAIL;
    size_t raw_len = 0;
    int consecutive_decode_errors = 0;
    int consecutive_empty_reads = 0;

    ESP_RETURN_ON_ERROR(audio_decoder_prepare(&decoder), AUDIO_PLAYER_TAG, "decoder setup failed");
    audio_pcm_reset_or_release();

    client = audio_stream_open(s_stations[station_index].url, &filter);
    if (client == NULL) {
        esp_audio_simple_dec_close(decoder);
        return ESP_FAIL;
    }

    audio_set_state(AUDIO_STATE_BUFFERING);

    while (!audio_request_changed(token, station_index)) {
        if (!audio_wifi_is_ready()) {
            ret = ESP_ERR_INVALID_STATE;
            audio_set_state(AUDIO_STATE_NO_WIFI);
            goto cleanup;
        }

        int read_len = esp_http_client_read(client, (char *)net_buf, sizeof(net_buf));
        if (read_len < 0) {
            ret = ESP_FAIL;
            goto cleanup;
        }
        if (read_len == 0) {
            consecutive_empty_reads++;
            if (consecutive_empty_reads >= AUDIO_MAX_CONSECUTIVE_EMPTY_READS) {
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
                ret = ESP_ERR_NO_MEM;
                goto cleanup;
            }
            if (ret != ESP_AUDIO_ERR_OK) {
                consecutive_decode_errors++;
                if (consecutive_decode_errors >= AUDIO_MAX_CONSECUTIVE_DECODE_ERRORS || raw.len <= 1) {
                    ret = ESP_FAIL;
                    goto cleanup;
                }
                raw.buffer += 1;
                raw.len -= 1;
                raw.consumed = 0;
                continue;
            }

            if (out_frame.decoded_size > 0) {
                esp_audio_simple_dec_info_t info = {0};
                if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK) {
                    ret = ESP_FAIL;
                    goto cleanup;
                }
                if (info.bits_per_sample != 16 || (info.channel != 1 && info.channel != 2) || info.sample_rate == 0) {
                    ret = ESP_ERR_NOT_SUPPORTED;
                    goto cleanup;
                }

                audio_pcm_set_format(info.sample_rate, info.channel, info.bits_per_sample);
                ESP_GOTO_ON_ERROR(audio_pcm_push(out_frame.buffer, out_frame.decoded_size, token, station_index),
                                  cleanup, AUDIO_PLAYER_TAG, "pcm buffer push failed");
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

static void audio_task(void *arg)
{
    (void)arg;

    while (1) {
        if (!audio_is_active_locked()) {
            if (audio_get_state_locked() != AUDIO_STATE_IDLE) {
                audio_set_state(AUDIO_STATE_IDLE);
            }
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
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AUDIO_RETRY_DELAY_MS));
        }
    }
}

void audio_player_init(void)
{
    ws2812_init();
    ws2812_off();

    if (s_pcm_mutex == NULL) {
        s_pcm_mutex = xSemaphoreCreateMutex();
    }
    if (s_audio_led_task == NULL) {
        xTaskCreate(audio_led_task, "audio_led", AUDIO_LED_TASK_STACK_SIZE, NULL,
                    AUDIO_TASK_PRIORITY - 1, &s_audio_led_task);
    }
    if (s_audio_writer_task == NULL) {
        xTaskCreate(audio_writer_task, "audio_writer", AUDIO_TASK_STACK_SIZE, NULL,
                    AUDIO_TASK_PRIORITY + 1, &s_audio_writer_task);
    }
    if (s_audio_task_handle == NULL) {
        xTaskCreate(audio_task, "audio_task", AUDIO_TASK_STACK_SIZE, NULL, AUDIO_TASK_PRIORITY, &s_audio_task_handle);
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

size_t audio_player_get_station_count(void)
{
    return sizeof(s_stations) / sizeof(s_stations[0]);
}

bool audio_player_is_active(void)
{
    return audio_is_active_locked();
}

void audio_player_set_volume(uint8_t volume_percent)
{
    if (volume_percent > 100) {
        volume_percent = 100;
    }

    portENTER_CRITICAL(&s_audio_lock);
    s_volume_percent = volume_percent;
    portEXIT_CRITICAL(&s_audio_lock);
}

uint8_t audio_player_get_volume(void)
{
    return audio_get_volume_locked();
}
