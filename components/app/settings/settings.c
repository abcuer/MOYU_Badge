#include "settings.h"

#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#define SETTINGS_NVS_NAMESPACE "sys_cfg"
#define SETTINGS_NVS_KEY_VOLUME "volume"
#define SETTINGS_NVS_KEY_AI_URL "ai_url"
#define SETTINGS_NVS_KEY_AI_DEV "ai_dev"
#define SETTINGS_NVS_KEY_AI_TOK "ai_tok"
#define SETTINGS_NVS_KEY_AI_SEC "ai_sec"
#define SETTINGS_NVS_KEY_AI_APP "ai_app"
#define SETTINGS_NVS_KEY_AI_VOI "ai_voi"
#define SETTINGS_NVS_KEY_AI_RES "ai_res"

#define SETTINGS_AI_ENDPOINT_MAX_LEN 191
#define SETTINGS_AI_DEVICE_ID_MAX_LEN 63
#define SETTINGS_AI_TOKEN_MAX_LEN 191
#define SETTINGS_AI_SECRET_KEY_MAX_LEN 191
#define SETTINGS_AI_APP_ID_MAX_LEN 63
#define SETTINGS_AI_VOICE_TYPE_MAX_LEN 95
#define SETTINGS_AI_RESOURCE_ID_MAX_LEN 95

static uint8_t s_volume = SETTINGS_DEFAULT_VOLUME;
static char s_ai_endpoint[SETTINGS_AI_ENDPOINT_MAX_LEN + 1] = SETTINGS_DEFAULT_AI_ENDPOINT;
static char s_ai_device_id[SETTINGS_AI_DEVICE_ID_MAX_LEN + 1] = SETTINGS_DEFAULT_AI_DEVICE_ID;
static char s_ai_token[SETTINGS_AI_TOKEN_MAX_LEN + 1] = SETTINGS_DEFAULT_AI_TOKEN;
static char s_ai_secret_key[SETTINGS_AI_SECRET_KEY_MAX_LEN + 1] = SETTINGS_DEFAULT_AI_SECRET_KEY;
static char s_ai_app_id[SETTINGS_AI_APP_ID_MAX_LEN + 1] = SETTINGS_DEFAULT_AI_APP_ID;
static char s_ai_voice_type[SETTINGS_AI_VOICE_TYPE_MAX_LEN + 1] = SETTINGS_DEFAULT_AI_VOICE_TYPE;
static char s_ai_resource_id[SETTINGS_AI_RESOURCE_ID_MAX_LEN + 1] = SETTINGS_DEFAULT_AI_RESOURCE_ID;

static uint8_t settings_clamp_volume(uint8_t volume)
{
    return volume > 100 ? 100 : volume;
}

esp_err_t settings_init(void)
{
    nvs_handle_t nvs = 0;
    uint8_t stored_volume = SETTINGS_DEFAULT_VOLUME;
    size_t value_len = 0;
    esp_err_t ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &nvs);

    s_volume = SETTINGS_DEFAULT_VOLUME;
    snprintf(s_ai_endpoint, sizeof(s_ai_endpoint), "%s", SETTINGS_DEFAULT_AI_ENDPOINT);
    snprintf(s_ai_device_id, sizeof(s_ai_device_id), "%s", SETTINGS_DEFAULT_AI_DEVICE_ID);
    snprintf(s_ai_token, sizeof(s_ai_token), "%s", SETTINGS_DEFAULT_AI_TOKEN);
    snprintf(s_ai_secret_key, sizeof(s_ai_secret_key), "%s", SETTINGS_DEFAULT_AI_SECRET_KEY);
    snprintf(s_ai_app_id, sizeof(s_ai_app_id), "%s", SETTINGS_DEFAULT_AI_APP_ID);
    snprintf(s_ai_voice_type, sizeof(s_ai_voice_type), "%s", SETTINGS_DEFAULT_AI_VOICE_TYPE);
    snprintf(s_ai_resource_id, sizeof(s_ai_resource_id), "%s", SETTINGS_DEFAULT_AI_RESOURCE_ID);

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

    value_len = sizeof(s_ai_endpoint);
    if (nvs_get_str(nvs, SETTINGS_NVS_KEY_AI_URL, s_ai_endpoint, &value_len) != ESP_OK) {
        snprintf(s_ai_endpoint, sizeof(s_ai_endpoint), "%s", SETTINGS_DEFAULT_AI_ENDPOINT);
    }

    value_len = sizeof(s_ai_device_id);
    if (nvs_get_str(nvs, SETTINGS_NVS_KEY_AI_DEV, s_ai_device_id, &value_len) != ESP_OK) {
        snprintf(s_ai_device_id, sizeof(s_ai_device_id), "%s", SETTINGS_DEFAULT_AI_DEVICE_ID);
    }

    value_len = sizeof(s_ai_token);
    if (nvs_get_str(nvs, SETTINGS_NVS_KEY_AI_TOK, s_ai_token, &value_len) != ESP_OK) {
        snprintf(s_ai_token, sizeof(s_ai_token), "%s", SETTINGS_DEFAULT_AI_TOKEN);
    }

    value_len = sizeof(s_ai_secret_key);
    if (nvs_get_str(nvs, SETTINGS_NVS_KEY_AI_SEC, s_ai_secret_key, &value_len) != ESP_OK) {
        snprintf(s_ai_secret_key, sizeof(s_ai_secret_key), "%s", SETTINGS_DEFAULT_AI_SECRET_KEY);
    }

    value_len = sizeof(s_ai_app_id);
    if (nvs_get_str(nvs, SETTINGS_NVS_KEY_AI_APP, s_ai_app_id, &value_len) != ESP_OK) {
        snprintf(s_ai_app_id, sizeof(s_ai_app_id), "%s", SETTINGS_DEFAULT_AI_APP_ID);
    }

    value_len = sizeof(s_ai_voice_type);
    if (nvs_get_str(nvs, SETTINGS_NVS_KEY_AI_VOI, s_ai_voice_type, &value_len) != ESP_OK) {
        snprintf(s_ai_voice_type, sizeof(s_ai_voice_type), "%s", SETTINGS_DEFAULT_AI_VOICE_TYPE);
    }

    value_len = sizeof(s_ai_resource_id);
    if (nvs_get_str(nvs, SETTINGS_NVS_KEY_AI_RES, s_ai_resource_id, &value_len) != ESP_OK) {
        snprintf(s_ai_resource_id, sizeof(s_ai_resource_id), "%s", SETTINGS_DEFAULT_AI_RESOURCE_ID);
    }

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

static void settings_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

const char *settings_get_ai_endpoint(void)
{
    return s_ai_endpoint;
}

const char *settings_get_ai_device_id(void)
{
    return s_ai_device_id;
}

const char *settings_get_ai_token(void)
{
    return s_ai_token;
}

const char *settings_get_ai_secret_key(void)
{
    return s_ai_secret_key;
}

const char *settings_get_ai_app_id(void)
{
    return s_ai_app_id;
}

const char *settings_get_ai_voice_type(void)
{
    return s_ai_voice_type;
}

const char *settings_get_ai_resource_id(void)
{
    return s_ai_resource_id;
}

void settings_set_ai_endpoint(const char *endpoint)
{
    settings_copy_string(s_ai_endpoint, sizeof(s_ai_endpoint), endpoint);
}

void settings_set_ai_device_id(const char *device_id)
{
    settings_copy_string(s_ai_device_id, sizeof(s_ai_device_id), device_id);
}

void settings_set_ai_token(const char *token)
{
    settings_copy_string(s_ai_token, sizeof(s_ai_token), token);
}

void settings_set_ai_secret_key(const char *secret_key)
{
    settings_copy_string(s_ai_secret_key, sizeof(s_ai_secret_key), secret_key);
}

void settings_set_ai_app_id(const char *app_id)
{
    settings_copy_string(s_ai_app_id, sizeof(s_ai_app_id), app_id);
}

void settings_set_ai_voice_type(const char *voice_type)
{
    settings_copy_string(s_ai_voice_type, sizeof(s_ai_voice_type), voice_type);
}

void settings_set_ai_resource_id(const char *resource_id)
{
    settings_copy_string(s_ai_resource_id, sizeof(s_ai_resource_id), resource_id);
}

esp_err_t settings_save_ai_config(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(nvs, SETTINGS_NVS_KEY_AI_URL, s_ai_endpoint);
    if (ret == ESP_OK) {
        ret = nvs_set_str(nvs, SETTINGS_NVS_KEY_AI_DEV, s_ai_device_id);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(nvs, SETTINGS_NVS_KEY_AI_TOK, s_ai_token);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(nvs, SETTINGS_NVS_KEY_AI_SEC, s_ai_secret_key);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(nvs, SETTINGS_NVS_KEY_AI_APP, s_ai_app_id);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(nvs, SETTINGS_NVS_KEY_AI_VOI, s_ai_voice_type);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_str(nvs, SETTINGS_NVS_KEY_AI_RES, s_ai_resource_id);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return ret;
}
