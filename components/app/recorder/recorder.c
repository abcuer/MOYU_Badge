#include "recorder.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inmp441.h"
#include "max98357.h"

static const char *TAG = "recorder";

static TaskHandle_t s_recorder_task = NULL;
static volatile bool s_active = false;
static volatile recorder_state_t s_state = RECORDER_STATE_IDLE;
static volatile uint16_t s_peak_level = 0;
static volatile size_t s_recorded_samples = 0;
static volatile bool s_task_idle = true;
static int16_t *s_record_buffer = NULL;
static size_t s_record_capacity = 0;
static size_t s_play_offset = 0;

static void recorder_reset_internal(void);
static void recorder_release_buffer(void);

static void recorder_start_recording(void)
{
    recorder_reset_internal();
    s_state = RECORDER_STATE_RECORDING;
}

static void recorder_start_playback(void)
{
    if (s_recorded_samples == 0) {
        s_state = RECORDER_STATE_IDLE;
        s_play_offset = 0;
        s_peak_level = 0;
        return;
    }

    s_play_offset = 0;
    s_state = RECORDER_STATE_PLAYING;
}

static uint16_t recorder_calculate_peak(const int16_t *samples, size_t count)
{
    uint16_t peak = 0;

    for (size_t i = 0; i < count; i++) {
        int32_t value = samples[i];
        uint32_t level = (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
        if (level > peak) {
            peak = (level > UINT16_MAX) ? UINT16_MAX : (uint16_t)level;
        }
    }

    return peak;
}

static int16_t recorder_apply_gain(int16_t sample)
{
    int32_t scaled = ((int32_t)sample) << RECORDER_GAIN_SHIFT;

    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

static esp_err_t recorder_ensure_buffer(void)
{
    if (s_record_buffer != NULL) {
        return ESP_OK;
    }

    s_record_capacity = RECORDER_SAMPLE_RATE * RECORDER_MAX_SECONDS;
    s_record_buffer = heap_caps_malloc(s_record_capacity * sizeof(int16_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_record_buffer == NULL) {
        s_record_buffer = heap_caps_malloc(s_record_capacity * sizeof(int16_t),
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_record_buffer == NULL) {
        ESP_LOGE(TAG, "alloc recorder pcm buffer failed");
        s_record_capacity = 0;
        return ESP_ERR_NO_MEM;
    }

    memset(s_record_buffer, 0, s_record_capacity * sizeof(int16_t));
    return ESP_OK;
}

static void recorder_release_buffer(void)
{
    if (s_record_buffer == NULL) {
        s_record_capacity = 0;
        return;
    }

    free(s_record_buffer);
    s_record_buffer = NULL;
    s_record_capacity = 0;
}

static void recorder_reset_internal(void)
{
    s_recorded_samples = 0;
    s_play_offset = 0;
    s_peak_level = 0;
}

static void recorder_task(void *arg)
{
    static int16_t pcm_chunk[RECORDER_CHUNK_SAMPLES];
    bool mic_ready = false;
    bool speaker_ready = false;

    (void)arg;

    while (1) {
        if (!s_active) {
            if (mic_ready) {
                inmp441_deinit();
                mic_ready = false;
            }
            if (speaker_ready) {
                max98357_deinit();
                speaker_ready = false;
            }
            s_task_idle = true;
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        s_task_idle = false;

        if (s_state == RECORDER_STATE_PLAYING) {
            if (mic_ready) {
                inmp441_deinit();
                mic_ready = false;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (!speaker_ready) {
                if (max98357_init(RECORDER_SAMPLE_RATE, RECORDER_CHANNELS, RECORDER_BITS_PER_SAMPLE) != ESP_OK) {
                    ESP_LOGE(TAG, "speaker init failed");
                    s_state = RECORDER_STATE_ERROR;
                    vTaskDelay(pdMS_TO_TICKS(50));
                    continue;
                }
                speaker_ready = true;
                vTaskDelay(pdMS_TO_TICKS(RECORDER_SPEAKER_WARMUP_MS));
            }

            if (s_play_offset >= s_recorded_samples) {
                max98357_deinit();
                speaker_ready = false;
                s_play_offset = 0;
                s_peak_level = 0;
                s_state = RECORDER_STATE_IDLE;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            size_t remain = s_recorded_samples - s_play_offset;
            size_t chunk_samples = remain > RECORDER_CHUNK_SAMPLES ? RECORDER_CHUNK_SAMPLES : remain;
            s_peak_level = recorder_calculate_peak(&s_record_buffer[s_play_offset], chunk_samples);
            esp_err_t ret = max98357_write((const uint8_t *)&s_record_buffer[s_play_offset],
                                           chunk_samples * sizeof(int16_t),
                                           RECORDER_CHANNELS,
                                           pdMS_TO_TICKS(RECORDER_IO_TIMEOUT_MS));
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "playback failed: %d", ret);
                max98357_deinit();
                speaker_ready = false;
                s_state = RECORDER_STATE_ERROR;
                continue;
            }
            s_play_offset += chunk_samples;
            continue;
        }

        if (speaker_ready) {
            max98357_deinit();
            speaker_ready = false;
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (s_state != RECORDER_STATE_RECORDING) {
            s_peak_level = 0;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!mic_ready) {
            if (inmp441_init(RECORDER_SAMPLE_RATE, RECORDER_BITS_PER_SAMPLE) != ESP_OK) {
                ESP_LOGE(TAG, "mic init failed");
                s_state = RECORDER_STATE_ERROR;
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            mic_ready = true;
        }

        size_t samples_read = 0;
        esp_err_t ret = inmp441_read(pcm_chunk, RECORDER_CHUNK_SAMPLES, &samples_read,
                                     pdMS_TO_TICKS(RECORDER_IO_TIMEOUT_MS));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "record read failed: %d", ret);
            s_state = RECORDER_STATE_ERROR;
            continue;
        }
        if (samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t free_samples = (s_record_capacity > s_recorded_samples) ? (s_record_capacity - s_recorded_samples) : 0;
        size_t copy_samples = samples_read > free_samples ? free_samples : samples_read;
        for (size_t i = 0; i < copy_samples; i++) {
            pcm_chunk[i] = recorder_apply_gain(pcm_chunk[i]);
        }
        s_peak_level = recorder_calculate_peak(pcm_chunk, copy_samples);

        if (copy_samples > 0) {
            memcpy(&s_record_buffer[s_recorded_samples], pcm_chunk, copy_samples * sizeof(int16_t));
            s_recorded_samples += copy_samples;
        }

        if (copy_samples < samples_read || s_recorded_samples >= s_record_capacity) {
            s_play_offset = 0;
            s_state = RECORDER_STATE_IDLE;
        }
    }
}

void recorder_init(void)
{
    if (s_recorder_task == NULL) {
        xTaskCreate(recorder_task, "recorder_task", RECORDER_TASK_STACK, NULL,
                    RECORDER_TASK_PRIORITY, &s_recorder_task);
    }
}

void recorder_enter_mode(void)
{
    if (recorder_ensure_buffer() != ESP_OK) {
        s_state = RECORDER_STATE_ERROR;
        return;
    }

    max98357_deinit();
    recorder_reset_internal();
    s_state = RECORDER_STATE_IDLE;
    s_active = true;
}

void recorder_exit_mode(void)
{
    s_active = false;

    for (uint8_t i = 0; i < 20 && !s_task_idle; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    recorder_reset_internal();
    recorder_release_buffer();
    s_state = RECORDER_STATE_IDLE;
}

void recorder_handle_short_press(void)
{
    if (!s_active) {
        return;
    }

    if (s_state == RECORDER_STATE_RECORDING) {
        recorder_start_playback();
        return;
    }

    recorder_start_recording();
}

void recorder_handle_long_press(void)
{
    if (!s_active || s_state == RECORDER_STATE_RECORDING) {
        return;
    }

    recorder_start_playback();
}

void recorder_stop_and_reset(void)
{
    recorder_reset_internal();
    if (s_active) {
        s_state = RECORDER_STATE_IDLE;
    }
}

recorder_state_t recorder_get_state(void)
{
    return s_state;
}

uint16_t recorder_get_peak_level(void)
{
    return s_peak_level;
}

uint32_t recorder_get_recorded_ms(void)
{
    return (uint32_t)((s_recorded_samples * 1000U) / RECORDER_SAMPLE_RATE);
}

bool recorder_is_active(void)
{
    return s_active;
}

bool recorder_has_recording(void)
{
    return s_recorded_samples > 0;
}
