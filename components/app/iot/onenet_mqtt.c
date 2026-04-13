#include "onenet_mqtt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "onenet_dm.h"
#include "onenet_token.h"
#include "onenet_ota.h"
#include "wifi_manager.h"

#define TAG "onenet_mqtt"
#define ONENET_WATCHDOG_PERIOD_MS 5000
#define ONENET_RECREATE_DELAY_US (15LL * 1000 * 1000)
#define ONENET_TOKEN_EXPIRE_TIME 1899805375U

static bool onenet_connected_flg = false;
static bool s_onenet_enabled = false;
static int64_t s_last_disconnect_us = 0;
static TaskHandle_t s_onenet_watchdog_task = NULL;
static esp_mqtt_client_handle_t s_onenet_client = NULL;

static void onenet_property_ack(const char *id, int code, const char *message);
static void onenet_ota_ack(const char *id, int code, const char *message);
static esp_err_t onenet_subscribe(void);
static esp_err_t onenet_client_start_locked(void);
static void onenet_client_destroy_locked(void);
static void onenet_watchdog_task(void *arg);

static void onenet_mqtt_event_handler(void *event_handler_arg,
                                      esp_event_base_t event_base,
                                      int32_t event_id,
                                      void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: 
            ESP_LOGI(TAG, "Onenet mqtt connected");
            onenet_connected_flg = true;
            s_last_disconnect_us = 0;
            onenet_subscribe();
            cJSON *property_js = onenet_property_upload_dm();
            char *data = cJSON_PrintUnformatted(property_js);
            onenet_post_property_data(data);
            onenet_ota_upload_version();
            set_app_vaild(true);
            cJSON_free(data);
            cJSON_Delete(property_js);
            break;
        
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Onenet mqtt disconnected");
            onenet_connected_flg = false;
            s_last_disconnect_us = esp_timer_get_time();
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Onenet mqtt subscribed ack, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Onenet mqtt publish ack, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);

            if (strstr(event->topic, "/property/set")) 
            {
                cJSON *property_js = cJSON_Parse(event->data);
                cJSON *id_js = cJSON_GetObjectItem(property_js, "id");

                onenet_property_handle(property_js);
                onenet_property_ack(cJSON_GetStringValue(id_js), 200, "success");
                cJSON_Delete(property_js);
            } 
            else if(strstr(event->topic,"/ota/inform"))
            {
                cJSON *ota_js = cJSON_Parse(event->data);
                cJSON *id_js = cJSON_GetObjectItem(ota_js,"id");
                onenet_ota_ack(cJSON_GetStringValue(id_js),200,"success");
                cJSON_Delete(ota_js);
                onenet_ota_start();
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");

            break;
        default:
            break;
    }
}

static void onenet_build_device_token(char *token, size_t token_size)
{
    if (token == NULL || token_size == 0) {
        return;
    }

    memset(token, 0, token_size);
    dev_token_generate(token, SIG_METHOD_SHA256, ONENET_TOKEN_EXPIRE_TIME,
                       ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, ONENET_ACCESS_KEY);
}

static void onenet_property_ack(const char *id, int code, const char *message)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set_reply",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);

    cJSON *reply_js = cJSON_CreateObject();
    cJSON_AddStringToObject(reply_js, "id", id);
    cJSON_AddNumberToObject(reply_js, "code", code);
    cJSON_AddStringToObject(reply_js, "message", message);
    char *data = cJSON_PrintUnformatted(reply_js);
    esp_mqtt_client_publish(s_onenet_client, topic, data, strlen(data), 1, 0);
    cJSON_free(data);
    cJSON_Delete(reply_js);
}

static void onenet_ota_ack(const char *id, int code, const char *message)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/ota/inform_reply",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);

    cJSON *reply_js = cJSON_CreateObject();
    cJSON_AddStringToObject(reply_js, "id", id);
    cJSON_AddNumberToObject(reply_js, "code", code);
    cJSON_AddStringToObject(reply_js, "message", message);
    char *data = cJSON_PrintUnformatted(reply_js);
    esp_mqtt_client_publish(s_onenet_client, topic, data, strlen(data), 1, 0);
    cJSON_free(data);
    cJSON_Delete(reply_js);
}

static void onenet_client_destroy_locked(void)
{
    if (s_onenet_client != NULL) {
        esp_mqtt_client_stop(s_onenet_client);
        esp_mqtt_client_destroy(s_onenet_client);
        s_onenet_client = NULL;
    }

    onenet_connected_flg = false;
}

static esp_err_t onenet_client_start_locked(void)
{
    static char token[256];
    esp_mqtt_client_config_t mqtt_client = {0};

    if (!s_onenet_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_manager_is_connect()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_onenet_client != NULL) {
        return ESP_OK;
    }

    mqtt_client.broker.address.uri = "mqtt://mqtts.heclouds.com";
    mqtt_client.broker.address.port = 1883;
    mqtt_client.credentials.client_id = ONENET_DEVICE_NAME;
    mqtt_client.credentials.username = ONENET_PRODUCT_ID;
    onenet_build_device_token(token, sizeof(token));
    mqtt_client.credentials.authentication.password = token;

    ESP_LOGI(TAG, "onenet connect->clientId:%s,username:%s",
             mqtt_client.credentials.client_id, mqtt_client.credentials.username);

    s_onenet_client = esp_mqtt_client_init(&mqtt_client);
    if (s_onenet_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_onenet_client, ESP_EVENT_ANY_ID,
                                   onenet_mqtt_event_handler, s_onenet_client);

    esp_err_t ret = esp_mqtt_client_start(s_onenet_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(ret));
        onenet_client_destroy_locked();
        return ret;
    }

    s_last_disconnect_us = 0;
    return ESP_OK;
}

static void onenet_watchdog_task(void *arg)
{
    const TickType_t check_interval = pdMS_TO_TICKS(ONENET_WATCHDOG_PERIOD_MS);

    while (true) {
        if (!s_onenet_enabled) {
            if (s_onenet_client != NULL) {
                onenet_client_destroy_locked();
            }
            vTaskDelay(check_interval);
            continue;
        }

        if (!wifi_manager_is_connect()) {
            if (s_onenet_client != NULL) {
                ESP_LOGI(TAG, "Wi-Fi unavailable, stopping OneNET client");
                onenet_client_destroy_locked();
            }
            vTaskDelay(check_interval);
            continue;
        }

        if (s_onenet_client == NULL) {
            ESP_LOGI(TAG, "Wi-Fi ready, starting OneNET client");
            onenet_client_start_locked();
            vTaskDelay(check_interval);
            continue;
        }

        if (!onenet_connected_flg && s_last_disconnect_us != 0 &&
            (esp_timer_get_time() - s_last_disconnect_us) > ONENET_RECREATE_DELAY_US) {
            ESP_LOGW(TAG, "MQTT disconnected too long, recreating client");
            onenet_client_destroy_locked();
            onenet_client_start_locked();
        }

        vTaskDelay(check_interval);
    }
}

esp_err_t onenet_start(void)
{
    s_onenet_enabled = true;

    esp_err_t ret = onenet_client_start_locked();

    if (s_onenet_watchdog_task == NULL) {
        if (xTaskCreate(onenet_watchdog_task, "onenet_watchdog", 4096, NULL, 4,
                        &s_onenet_watchdog_task) != pdPASS) {
            s_onenet_watchdog_task = NULL;
            s_onenet_enabled = false;
            onenet_client_destroy_locked();
            return ESP_FAIL;
        }
    }

    return ret;
}

esp_err_t onenet_stop(void)
{
    s_onenet_enabled = false;
    onenet_client_destroy_locked();
    return ESP_OK;
}

esp_err_t onenet_post_property_data(const char *data)
{
    if (!s_onenet_enabled || !wifi_manager_is_connect() ||
        !onenet_connected_flg || s_onenet_client == NULL) {
        return ESP_FAIL;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    ESP_LOGI(TAG, "Upload topic:%s,payload:%s", topic, data);
    return esp_mqtt_client_publish(s_onenet_client, topic, data, strlen(data), 1, 0);
}

esp_err_t onenet_subscribe(void)
{
    if (!onenet_connected_flg || s_onenet_client == NULL) {
        return ESP_FAIL;
    }

    char topic[128];

    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post/reply",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(s_onenet_client, topic, 1);

    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(s_onenet_client, topic, 1);

    snprintf(topic, sizeof(topic), "$sys/%s/%s/ota/inform",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    return esp_mqtt_client_subscribe_single(s_onenet_client, topic, 1);
}
