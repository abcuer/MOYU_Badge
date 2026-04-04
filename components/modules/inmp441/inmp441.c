#include "inmp441.h"

#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define INMP441_DMA_DESC_NUM  6
#define INMP441_DMA_FRAME_NUM 256

static const char *TAG = "inmp441";

static i2s_chan_handle_t s_rx_handle = NULL;
static uint32_t s_sample_rate = 0;
static uint8_t s_bits_per_sample = 0;

void inmp441_deinit(void)
{
    if (s_rx_handle != NULL) {
        i2s_channel_disable(s_rx_handle);
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
    }

    s_sample_rate = 0;
    s_bits_per_sample = 0;
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

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = INMP441_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = INMP441_DMA_FRAME_NUM;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "alloc i2s channel failed: %d", ret);
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
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
    size_t bytes_read = 0;

    if (buffer == NULL || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_rx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2s_channel_read(s_rx_handle, buffer, sample_count * sizeof(int16_t), &bytes_read, timeout_ticks);
    if (samples_read != NULL) {
        *samples_read = bytes_read / sizeof(int16_t);
    }
    return ret;
}
