#include "max98357.h"

#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define MAX98357_DMA_DESC_NUM        8
#define MAX98357_DMA_FRAME_NUM       512
#define MAX98357_MAX_INPUT_PCM_BYTES 4096

static const char *TAG = "max98357";

static i2s_chan_handle_t s_tx_handle = NULL;
static uint32_t s_i2s_rate = 0;
static uint8_t s_i2s_channels = 0;

static esp_err_t max98357_write_all(const uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    size_t total_written = 0;

    while (total_written < len) {
        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_handle, data + total_written, len - total_written,
                                          &bytes_written, timeout_ticks);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write failed ret=%d written=%lu remain=%lu",
                     ret, (unsigned long)bytes_written, (unsigned long)(len - total_written));
            return ret;
        }
        if (bytes_written == 0) {
            return ESP_ERR_TIMEOUT;
        }
        total_written += bytes_written;
    }

    return ESP_OK;
}

void max98357_deinit(void)
{
    if (s_tx_handle != NULL) {
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }

    s_i2s_rate = 0;
    s_i2s_channels = 0;
}

esp_err_t max98357_init(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    if (sample_rate == 0 || (channels != 1 && channels != 2) || bits_per_sample != 16) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_tx_handle != NULL && s_i2s_rate == sample_rate && s_i2s_channels == channels) {
        return ESP_OK;
    }

    max98357_deinit();

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = MAX98357_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = MAX98357_DMA_FRAME_NUM;
    chan_cfg.auto_clear_after_cb = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "alloc i2s channel failed: %d", ret);
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MAX98357_BCLK_PIN,
            .ws = MAX98357_LRC_PIN,
            .dout = MAX98357_DIN_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = 0,
                .bclk_inv = 0,
                .ws_inv = 0,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "init i2s std failed: %d", ret);
        max98357_deinit();
        return ret;
    }

    ret = i2s_channel_enable(s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "enable i2s failed: %d", ret);
        max98357_deinit();
        return ret;
    }

    s_i2s_rate = sample_rate;
    s_i2s_channels = channels;
    return ESP_OK;
}

esp_err_t max98357_write(const uint8_t *pcm_data, size_t pcm_len, uint8_t channels, TickType_t timeout_ticks)
{
    if (s_tx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pcm_data == NULL || pcm_len == 0) {
        return ESP_OK;
    }
    if ((pcm_len % sizeof(int16_t)) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (channels == 2) {
        return max98357_write_all(pcm_data, pcm_len, timeout_ticks);
    }

    if (channels != 1 || pcm_len > MAX98357_MAX_INPUT_PCM_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    static int16_t stereo_buf[(MAX98357_MAX_INPUT_PCM_BYTES / sizeof(int16_t)) * 2];
    const int16_t *src = (const int16_t *)pcm_data;
    size_t samples = pcm_len / sizeof(int16_t);

    for (size_t i = 0; i < samples; i++) {
        stereo_buf[2 * i] = src[i];
        stereo_buf[2 * i + 1] = src[i];
    }

    return max98357_write_all((const uint8_t *)stereo_buf, pcm_len * 2, timeout_ticks);
}
