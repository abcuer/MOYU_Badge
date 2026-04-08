#include "recorder.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inmp441.h"
#include "max98357.h"

#define RECORDER_SAMPLE_RATE             16000
#define RECORDER_PLAYBACK_RATE           16000
#define RECORDER_BITS_PER_SAMPLE         16
#define RECORDER_CHANNELS                1
#define RECORDER_CHUNK_SAMPLES           320
#define RECORDER_MAX_SECONDS             8
#define RECORDER_TASK_STACK              4096
#define RECORDER_TASK_PRIORITY           5
#define RECORDER_PLAY_TIMEOUT_MS         500

static const char *TAG = "recorder";

static TaskHandle_t s_recorder_task = NULL;
static volatile bool s_active = false;
static volatile recorder_state_t s_state = RECORDER_STATE_IDLE;
static volatile uint16_t s_peak_level = 0;
static volatile size_t s_recorded_samples = 0;
static int32_t *s_record_buffer = NULL;
static size_t s_record_capacity = 0;
static size_t s_play_offset = 0;
static const char *s_variant_name = "Play Voice";

static uint16_t recorder_calculate_peak_raw(const int32_t *samples, size_t count)
{
    uint16_t peak = 0;

    for (size_t i = 0; i < count; i++) {
        int32_t scaled = samples[i] >> 16;
        uint32_t level = (scaled < 0) ? (uint32_t)(-scaled) : (uint32_t)scaled;
        if (level > peak) {
            peak = (level > UINT16_MAX) ? UINT16_MAX : (uint16_t)level;
        }
    }

    return peak;
}

static esp_err_t recorder_ensure_buffer(void)
{
    if (s_record_buffer != NULL) {
        return ESP_OK;
    }

    s_record_capacity = RECORDER_SAMPLE_RATE * RECORDER_MAX_SECONDS;
    s_record_buffer = heap_caps_malloc(s_record_capacity * sizeof(int32_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_record_buffer == NULL) {
        s_record_buffer = heap_caps_malloc(s_record_capacity * sizeof(int32_t),
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_record_buffer == NULL) {
        ESP_LOGE(TAG, "alloc recorder buffer failed");
        s_record_capacity = 0;
        return ESP_ERR_NO_MEM;
    }

    memset(s_record_buffer, 0, s_record_capacity * sizeof(int32_t));
    return ESP_OK;
}

static void recorder_reset_internal(void)
{
    s_recorded_samples = 0;
    s_play_offset = 0;
    s_peak_level = 0;
}

static int16_t recorder_decode_sample(int32_t raw,
                                      int32_t *hp_prev_x, int32_t *hp_prev_y,
                                      int32_t *lp_prev_y)
{
    int32_t scaled;

    {
        int32_t x = raw >> 16;
        // Stay close to the earlier HP path that could recover speech on this
        // board, but ease the shaping so it does not sound overly processed.
        int32_t hp = x - *hp_prev_x + ((*hp_prev_y * 31) / 32);
        *hp_prev_x = x;
        *hp_prev_y = hp;

        int32_t abs_hp = (hp < 0) ? -hp : hp;
        if (abs_hp < 24) {
            hp = 0;
        } else if (hp > 0) {
            hp -= 24;
        } else {
            hp += 24;
        }

        int32_t lp = ((*lp_prev_y * 2) + (hp * 2)) / 4;
        *lp_prev_y = lp;
        scaled = lp * 2;
    }

    if (scaled > INT16_MAX) {
        scaled = INT16_MAX;
    } else if (scaled < INT16_MIN) {
        scaled = INT16_MIN;
    }
    return (int16_t)scaled;
}

static void recorder_task(void *arg)
{
    (void)arg;

    static int32_t mic_chunk[RECORDER_CHUNK_SAMPLES];
    static int16_t playback_chunk[RECORDER_CHUNK_SAMPLES * 4];
    uint32_t last_diag_log_ms = 0;
    bool mic_ready = false;
    bool speaker_ready = false;
    bool have_prev_play_sample = false;
    int16_t prev_play_sample = 0;
    int32_t hp_prev_x = 0;
    int32_t hp_prev_y = 0;
    int32_t lp_prev_y = 0;

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
            last_diag_log_ms = 0;
            have_prev_play_sample = false;
            prev_play_sample = 0;
            hp_prev_x = 0;
            hp_prev_y = 0;
            lp_prev_y = 0;
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        if (s_state == RECORDER_STATE_PLAYING) {
            if (mic_ready) {
                inmp441_deinit();
                mic_ready = false;
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (!speaker_ready) {
                if (max98357_init(RECORDER_PLAYBACK_RATE, 2, RECORDER_BITS_PER_SAMPLE) != ESP_OK) {
                    s_state = RECORDER_STATE_ERROR;
                    vTaskDelay(pdMS_TO_TICKS(50));
                    continue;
                }
                speaker_ready = true;
            }

            if (s_play_offset >= s_recorded_samples) {
                max98357_deinit();
                speaker_ready = false;
                s_play_offset = 0;
                have_prev_play_sample = false;
                prev_play_sample = 0;
                hp_prev_x = 0;
                hp_prev_y = 0;
                lp_prev_y = 0;
                s_state = RECORDER_STATE_IDLE;
                s_peak_level = 0;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            size_t remain = s_recorded_samples - s_play_offset;
            size_t chunk_samples = remain > RECORDER_CHUNK_SAMPLES ? RECORDER_CHUNK_SAMPLES : remain;
            for (size_t i = 0; i < chunk_samples; i++) {
                int16_t sample = recorder_decode_sample(s_record_buffer[s_play_offset + i],
                                                        &hp_prev_x, &hp_prev_y, &lp_prev_y);
                int16_t interp_a = have_prev_play_sample
                                       ? (int16_t)(((int32_t)prev_play_sample * 3 + (int32_t)sample) / 4)
                                       : sample;
                int16_t interp_b = have_prev_play_sample
                                       ? (int16_t)(((int32_t)prev_play_sample + (int32_t)sample * 3) / 4)
                                       : sample;
                size_t out = i * 4;
                playback_chunk[out] = interp_a;
                playback_chunk[out + 1] = interp_a;
                playback_chunk[out + 2] = interp_b;
                playback_chunk[out + 3] = interp_b;
                prev_play_sample = sample;
                have_prev_play_sample = true;
            }

            esp_err_t ret = max98357_write((const uint8_t *)playback_chunk,
                                           (chunk_samples * 4) * sizeof(int16_t),
                                           2,
                                           pdMS_TO_TICKS(RECORDER_PLAY_TIMEOUT_MS));
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "playback failed: %d", ret);
                max98357_deinit();
                speaker_ready = false;
                s_state = RECORDER_STATE_ERROR;
                continue;
            }
            s_play_offset += chunk_samples;
            s_peak_level = recorder_calculate_peak_raw(&s_record_buffer[s_play_offset - chunk_samples], chunk_samples);
            continue;
        }

        if (speaker_ready) {
            max98357_deinit();
            speaker_ready = false;
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (!mic_ready) {
            if (inmp441_init(RECORDER_SAMPLE_RATE, RECORDER_BITS_PER_SAMPLE) != ESP_OK) {
                s_state = RECORDER_STATE_ERROR;
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            mic_ready = true;
        }

        size_t samples_read = 0;
        esp_err_t ret = inmp441_read_raw(mic_chunk, RECORDER_CHUNK_SAMPLES, &samples_read, pdMS_TO_TICKS(50));
        if (ret != ESP_OK || samples_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        s_peak_level = recorder_calculate_peak_raw(mic_chunk, samples_read);

        if (s_state == RECORDER_STATE_RECORDING) {
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            size_t free_samples = (s_record_capacity > s_recorded_samples) ? (s_record_capacity - s_recorded_samples) : 0;
            size_t copy_samples = samples_read > free_samples ? free_samples : samples_read;
            if (copy_samples > 0) {
                memcpy(&s_record_buffer[s_recorded_samples], mic_chunk, copy_samples * sizeof(int32_t));
                s_recorded_samples += copy_samples;
            }
            if (last_diag_log_ms == 0 || (now_ms - last_diag_log_ms) >= 1000) {
                inmp441_diag_t diag;
                inmp441_get_diag(&diag);
                ESP_LOGI(TAG,
                         "mic diag peak=%u raw[min=%ld max=%ld] abs{8=%lu 10=%lu 12=%lu 14=%lu 16=%lu}",
                         (unsigned)s_peak_level,
                         (long)diag.raw_min,
                         (long)diag.raw_max,
                         (unsigned long)diag.abs_max_shift_8,
                         (unsigned long)diag.abs_max_shift_10,
                         (unsigned long)diag.abs_max_shift_12,
                         (unsigned long)diag.abs_max_shift_14,
                         (unsigned long)diag.abs_max_shift_16);
                last_diag_log_ms = now_ms;
            }
            if (copy_samples < samples_read || s_recorded_samples >= s_record_capacity) {
                s_play_offset = 0;
                have_prev_play_sample = false;
                prev_play_sample = 0;
                hp_prev_x = 0;
                hp_prev_y = 0;
                lp_prev_y = 0;
                s_state = RECORDER_STATE_PLAYING;
            }
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
    recorder_reset_internal();
    s_state = RECORDER_STATE_IDLE;
}

void recorder_handle_short_press(void)
{
    if (!s_active) {
        return;
    }

    if (s_state == RECORDER_STATE_RECORDING) {
        s_play_offset = 0;
        s_state = (s_recorded_samples > 0) ? RECORDER_STATE_PLAYING : RECORDER_STATE_IDLE;
        return;
    }

    if (s_state == RECORDER_STATE_PLAYING) {
        s_play_offset = 0;
        s_state = RECORDER_STATE_IDLE;
        return;
    }

    recorder_reset_internal();
    s_state = RECORDER_STATE_RECORDING;
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

const char *recorder_get_play_variant_name(void)
{
    return s_variant_name;
}
