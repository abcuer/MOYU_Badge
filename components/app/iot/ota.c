#include "ota.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_manager.h"

#define OTA_URL_MAX_LEN 512
#define OTA_VERSION_MAX_LEN 64

static const char *TAG = "app_ota";

typedef struct {
    char url[OTA_URL_MAX_LEN];
    char version[OTA_VERSION_MAX_LEN];
} ota_request_t;

static TaskHandle_t s_ota_task = NULL;

static const char *json_find_string_recursive(cJSON *node, const char *key)
{
    if (node == NULL || key == NULL) {
        return NULL;
    }

    if (cJSON_IsObject(node)) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(node, key);
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            return item->valuestring;
        }

        cJSON *child = NULL;
        cJSON_ArrayForEach(child, node) {
            const char *found = json_find_string_recursive(child, key);
            if (found != NULL) {
                return found;
            }
        }
    } else if (cJSON_IsArray(node)) {
        cJSON *child = NULL;
        cJSON_ArrayForEach(child, node) {
            const char *found = json_find_string_recursive(child, key);
            if (found != NULL) {
                return found;
            }
        }
    }

    return NULL;
}

static const char *json_find_first_string(cJSON *root, const char *const *keys, size_t key_count)
{
    for (size_t i = 0; i < key_count; ++i) {
        const char *value = json_find_string_recursive(root, keys[i]);
        if (value != NULL && value[0] != '\0') {
            return value;
        }
    }
    return NULL;
}

void ota_mark_app_valid_if_needed(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
        ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Marked OTA app valid");
        } else {
            ESP_LOGW(TAG, "Failed to mark OTA app valid: %s", esp_err_to_name(err));
        }
    }
}

static void ota_task(void *arg)
{
    ota_request_t *request = (ota_request_t *)arg;
    ESP_LOGI(TAG, "Starting OTA from %s", request->url);

    esp_http_client_config_t http_config = {
        .url = request->url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    if (request->version[0] != '\0') {
        ESP_LOGI(TAG, "Target firmware version: %s", request->version);
    }

    esp_err_t err = esp_https_ota(&ota_config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful, restarting");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }

    free(request);
    s_ota_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ota_start_from_url(const char *url, const char *version)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!wifi_manager_is_connect()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ota_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ota_request_t *request = calloc(1, sizeof(ota_request_t));
    if (request == NULL) {
        return ESP_ERR_NO_MEM;
    }

    strlcpy(request->url, url, sizeof(request->url));
    if (version != NULL) {
        strlcpy(request->version, version, sizeof(request->version));
    }

    BaseType_t created = xTaskCreate(ota_task, "ota_task", 10240, request, 5, &s_ota_task);
    if (created != pdPASS) {
        free(request);
        s_ota_task = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ota_start_from_onenet_payload(const char *payload, size_t payload_len)
{
    static const char *const url_keys[] = {
        "url",
        "downloadUrl",
        "download_url",
        "targetUrl",
        "target_url",
        "packageUrl",
        "fileUrl"
    };
    static const char *const version_keys[] = {
        "version",
        "targetVersion",
        "target_version",
        "ver"
    };

    if (payload == NULL || payload_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_ParseWithLength(payload, payload_len);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse OTA payload");
        return ESP_FAIL;
    }

    const char *url = json_find_first_string(root, url_keys, sizeof(url_keys) / sizeof(url_keys[0]));
    const char *version = json_find_first_string(root, version_keys, sizeof(version_keys) / sizeof(version_keys[0]));
    if (url == NULL) {
        ESP_LOGE(TAG, "No OTA url found in payload");
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Parsed OTA url=%s version=%s", url, version != NULL ? version : "unknown");
    esp_err_t err = ota_start_from_url(url, version);
    cJSON_Delete(root);
    return err;
}
