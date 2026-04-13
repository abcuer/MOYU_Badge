#include "inmp441.h"

#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define INMP441_DMA_DESC_NUM      6
#define INMP441_DMA_FRAME_NUM     256
#define INMP441_I2S_PORT          I2S_NUM_0
#define INMP441_RAW_CHUNK_FRAMES  128

static const char *TAG = "inmp441";

static i2s_chan_handle_t s_rx_handle = NULL;
static uint32_t s_sample_rate = 0;
static uint8_t s_bits_per_sample = 0;
static int32_t s_hp_prev_x = 0;
static int32_t s_hp_prev_y = 0;
static int s_active_slot_index = -1;

void inmp441_deinit(void)
{
    if (s_rx_handle != NULL) {
        i2s_channel_disable(s_rx_handle);
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
    }

    s_sample_rate = 0;
    s_bits_per_sample = 0;
    s_hp_prev_x = 0;
    s_hp_prev_y = 0;
    s_active_slot_index = -1;
}

esp_err_t inmp441_init(uint32_t sample_rate, uint8_t bits_per_sample)
{
    if (sample_rate == 0 || bits_per_sample != 16) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_rx_handle != NULL && s_sample_rate == sample_rate && s_bits_per_sample == bits_per_sample) {
        return ESP_OK;
    }

    inmp441_deinit();

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(INMP441_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = INMP441_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = INMP441_DMA_FRAME_NUM;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "alloc i2s channel failed: %d", ret);
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = INMP441_SCK_PIN,
            .ws = INMP441_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = INMP441_SD_PIN,
            .invert_flags = {
                .mclk_inv = 0,
                .bclk_inv = 0,
                .ws_inv = 0,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "init i2s std failed: %d", ret);
        inmp441_deinit();
        return ret;
    }

    ret = i2s_channel_enable(s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "enable i2s rx failed: %d", ret);
        inmp441_deinit();
        return ret;
    }

    s_sample_rate = sample_rate;
    s_bits_per_sample = bits_per_sample;
    return ESP_OK;
}

esp_err_t inmp441_read(int16_t *buffer, size_t sample_count, size_t *samples_read, TickType_t timeout_ticks)
{
    static int32_t raw_buf[INMP441_RAW_CHUNK_FRAMES * 2];
    size_t total_samples = 0;

    if (buffer == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    while (total_samples < sample_count) {
        size_t bytes_read = 0;
        size_t want_frames = sample_count - total_samples;
        if (want_frames > INMP441_RAW_CHUNK_FRAMES) {
            want_frames = INMP441_RAW_CHUNK_FRAMES;
        }

        esp_err_t ret = i2s_channel_read(s_rx_handle, raw_buf, want_frames * sizeof(int32_t) * 2,
                                         &bytes_read, timeout_ticks);
        if (ret != ESP_OK) {
            if (samples_read != NULL) {
                *samples_read = total_samples;
            }
            return ret;
        }

        size_t got_frames = bytes_read / (sizeof(int32_t) * 2);

        if (got_frames > 0 && s_active_slot_index < 0) {
            uint32_t left_peak = 0;
            uint32_t right_peak = 0;

            for (size_t i = 0; i < got_frames; i++) {
                int32_t left = raw_buf[2 * i];
                int32_t right = raw_buf[2 * i + 1];
                uint32_t left_abs = (left < 0) ? (uint32_t)(-left) : (uint32_t)left;
                uint32_t right_abs = (right < 0) ? (uint32_t)(-right) : (uint32_t)right;
                if (left_abs > left_peak) {
                    left_peak = left_abs;
                }
                if (right_abs > right_peak) {
                    right_peak = right_abs;
                }
            }

            s_active_slot_index = (right_peak > left_peak) ? 1 : 0;
            ESP_LOGI(TAG, "select %s slot, left_peak=%lu right_peak=%lu",
                     s_active_slot_index == 0 ? "left" : "right",
                     (unsigned long)left_peak, (unsigned long)right_peak);
        }

        for (size_t i = 0; i < got_frames; i++) {
            int32_t scaled = raw_buf[2 * i + (s_active_slot_index > 0 ? 1 : 0)] >> 16;
            int32_t hp = scaled - s_hp_prev_x + ((s_hp_prev_y * 63) / 64);
            s_hp_prev_x = scaled;
            s_hp_prev_y = hp;
            scaled = hp * 2;
            if (scaled > INT16_MAX) {
                scaled = INT16_MAX;
            } else if (scaled < INT16_MIN) {
                scaled = INT16_MIN;
            }
            buffer[total_samples++] = (int16_t)scaled;
        }

        if (got_frames == 0) {
            break;
        }
    }

    if (samples_read != NULL) {
        *samples_read = total_samples;
    }
    return ESP_OK;
}
