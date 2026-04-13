#include "max98357.h"

#include "driver/gpio.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "max98357";

static i2s_chan_handle_t s_tx_handle = NULL;
static uint32_t s_i2s_rate = 0;
static uint8_t s_i2s_channels = 0;
static SemaphoreHandle_t s_i2s_mutex = NULL;
static portMUX_TYPE s_i2s_mutex_lock = portMUX_INITIALIZER_UNLOCKED;

static void max98357_set_idle_gpio_levels(void)
{
    const gpio_num_t pins[] = {
        (gpio_num_t)MAX98357_BCLK_PIN,
        (gpio_num_t)MAX98357_LRC_PIN,
        (gpio_num_t)MAX98357_DIN_PIN,
    };

    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << pins[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
        gpio_set_level(pins[i], 0);
    }
}

static SemaphoreHandle_t max98357_get_mutex(void)
{
    SemaphoreHandle_t mutex;

    portENTER_CRITICAL(&s_i2s_mutex_lock);
    if (s_i2s_mutex == NULL) {
        s_i2s_mutex = xSemaphoreCreateMutex();
    }
    mutex = s_i2s_mutex;
    portEXIT_CRITICAL(&s_i2s_mutex_lock);

    return mutex;
}

static esp_err_t max98357_write_all(const uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    size_t total_written = 0;

    while (total_written < len) {
        size_t bytes_written = 0;
        size_t write_len = len - total_written;
        if (write_len > MAX98357_WRITE_CHUNK_BYTES) {
            write_len = MAX98357_WRITE_CHUNK_BYTES;
        }

        esp_err_t ret = i2s_channel_write(s_tx_handle, data + total_written, write_len,
                                          &bytes_written, timeout_ticks);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write failed ret=%d written=%lu remain=%lu",
                     ret, (unsigned long)bytes_written, (unsigned long)write_len);
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
    SemaphoreHandle_t mutex = max98357_get_mutex();
    if (mutex == NULL) {
        return;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    if (s_tx_handle != NULL) {
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }

    s_i2s_rate = 0;
    s_i2s_channels = 0;
    max98357_set_idle_gpio_levels();
    xSemaphoreGive(mutex);
}

esp_err_t max98357_init(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample)
{
    SemaphoreHandle_t mutex = max98357_get_mutex();
    if (mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (sample_rate == 0 || (channels != 1 && channels != 2) || bits_per_sample != 16) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    if (s_tx_handle != NULL && s_i2s_rate == sample_rate && s_i2s_channels == channels) {
        xSemaphoreGive(mutex);
        return ESP_OK;
    }

    if (s_tx_handle != NULL) {
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }
    s_i2s_rate = 0;
    s_i2s_channels = 0;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MAX98357_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = MAX98357_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = MAX98357_DMA_FRAME_NUM;
    chan_cfg.auto_clear_after_cb = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "alloc i2s channel failed: %d", ret);
        xSemaphoreGive(mutex);
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
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        max98357_set_idle_gpio_levels();
        xSemaphoreGive(mutex);
        return ret;
    }

    {
        static const uint8_t preload_silence[MAX98357_PRELOAD_SILENCE_BYTES] = {0};
        size_t bytes_loaded = 0;
        ret = i2s_channel_preload_data(s_tx_handle, preload_silence, sizeof(preload_silence), &bytes_loaded);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "preload silence failed: %d", ret);
        }
    }

    ret = i2s_channel_enable(s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "enable i2s failed: %d", ret);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        max98357_set_idle_gpio_levels();
        xSemaphoreGive(mutex);
        return ret;
    }

    s_i2s_rate = sample_rate;
    s_i2s_channels = channels;
    xSemaphoreGive(mutex);
    return ESP_OK;
}

esp_err_t max98357_write(const uint8_t *pcm_data, size_t pcm_len, uint8_t channels, TickType_t timeout_ticks)
{
    SemaphoreHandle_t mutex = max98357_get_mutex();
    if (mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    if (s_tx_handle == NULL) {
        xSemaphoreGive(mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (pcm_data == NULL || pcm_len == 0) {
        xSemaphoreGive(mutex);
        return ESP_OK;
    }
    if ((pcm_len % sizeof(int16_t)) != 0) {
        xSemaphoreGive(mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    if (channels == 2) {
        esp_err_t ret = max98357_write_all(pcm_data, pcm_len, timeout_ticks);
        xSemaphoreGive(mutex);
        return ret;
    }

    if (channels != 1 || pcm_len > MAX98357_MAX_INPUT_PCM_BYTES) {
        xSemaphoreGive(mutex);
        return ESP_ERR_INVALID_ARG;
    }

    static int16_t stereo_buf[(MAX98357_MAX_INPUT_PCM_BYTES / sizeof(int16_t)) * 2];
    const int16_t *src = (const int16_t *)pcm_data;
    size_t samples = pcm_len / sizeof(int16_t);

    for (size_t i = 0; i < samples; i++) {
        stereo_buf[2 * i] = src[i];
        stereo_buf[2 * i + 1] = src[i];
    }

    esp_err_t ret = max98357_write_all((const uint8_t *)stereo_buf, pcm_len * 2, timeout_ticks);
    xSemaphoreGive(mutex);
    return ret;
}
