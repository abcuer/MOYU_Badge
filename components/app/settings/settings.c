#include "settings.h"

#include "nvs.h"

#define SETTINGS_NVS_NAMESPACE "sys_cfg"
#define SETTINGS_NVS_KEY_VOLUME "volume"

static uint8_t s_volume = SETTINGS_DEFAULT_VOLUME;

static uint8_t settings_clamp_volume(uint8_t volume)
{
    return volume > 100 ? 100 : volume;
}

esp_err_t settings_init(void)
{
    nvs_handle_t nvs = 0;
    uint8_t stored_volume = SETTINGS_DEFAULT_VOLUME;
    esp_err_t ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &nvs);

    s_volume = SETTINGS_DEFAULT_VOLUME;

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_get_u8(nvs, SETTINGS_NVS_KEY_VOLUME, &stored_volume);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs);
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        nvs_close(nvs);
        return ret;
    }

    s_volume = settings_clamp_volume(stored_volume);

    nvs_close(nvs);
    return ESP_OK;
}

uint8_t settings_get_volume(void)
{
    return s_volume;
}

void settings_set_volume(uint8_t volume)
{
    s_volume = settings_clamp_volume(volume);
}

esp_err_t settings_save_volume(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_u8(nvs, SETTINGS_NVS_KEY_VOLUME, s_volume);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return ret;
}
