#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "ap_wifi.h"
#include "ws_server.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "user_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

#include "ui.h"

static char *s_index_html = NULL;
static EventGroupHandle_t s_apcfg_event = NULL;
static char s_current_ssid[32];
static char s_current_password[64];

static TaskHandle_t s_dns_task_handle = NULL;
static TaskHandle_t s_saved_wifi_timeout_task = NULL;
static int s_dns_sock = -1;
static volatile bool s_dns_running = false;
static volatile bool s_ap_config_active = false;
static volatile bool s_saved_wifi_connected = false;

static const uint8_t s_ap_ip[4] = {192, 168, 100, 1};

static char *ap_wifi_load_web_page_buffer(void);
static void ap_wifi_handle_scan_finished(int numbers, wifi_ap_record_t *ap_records);
static void ap_wifi_handle_ws_receive(uint8_t *payload, int len);
static void ap_wifi_event_task(void *param);
static void captive_dns_start(void);
static void captive_dns_stop(void);
static void captive_dns_task(void *param);
static int captive_dns_question_len(const uint8_t *packet, int len);
static int captive_dns_build_response(const uint8_t *request, int request_len,
                                      uint8_t *response, int response_cap);
static void ap_wifi_saved_wifi_timeout_task(void *param);
static void ap_wifi_handle_state_change(WIFI_STATE state);
static bool ap_wifi_load_from_nvs(char *ssid, size_t ssid_len, char *password, size_t pwd_len);

static char *ap_wifi_load_web_page_buffer(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "html",
        .max_files = 5,
        .format_if_mount_failed = false,
    };

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    struct stat st;
    if (stat(AP_WIFI_INDEX_HTML_PATH, &st) != 0) {
        ESP_LOGE(AP_WIFI_TAG, "apcfg.html not found");
        return NULL;
    }

    char *page = (char *)malloc(st.st_size + 1);
    if (page == NULL) {
        return NULL;
    }

    memset(page, 0, st.st_size + 1);
    FILE *fp = fopen(AP_WIFI_INDEX_HTML_PATH, "r");
    if (fp == NULL) {
        free(page);
        return NULL;
    }

    if (fread(page, st.st_size, 1, fp) == 0) {
        free(page);
        fclose(fp);
        ESP_LOGE(AP_WIFI_TAG, "fread failed");
        return NULL;
    }

    fclose(fp);
    return page;
}

static void ap_wifi_handle_scan_finished(int numbers, wifi_ap_record_t *ap_records)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *wifilist_js = cJSON_AddArrayToObject(root, "wifi_list");

    for (int i = 0; i < numbers; i++) {
        cJSON *wifi_js = cJSON_CreateObject();
        cJSON_AddStringToObject(wifi_js, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(wifi_js, "rssi", ap_records[i].rssi);
        cJSON_AddBoolToObject(wifi_js, "encrypted", ap_records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(wifilist_js, wifi_js);
    }

    char *data = cJSON_Print(root);
    ESP_LOGI(AP_WIFI_TAG, "WS send:%s", data);
    web_ws_send((uint8_t *)data, strlen(data));
    cJSON_free(data);
    cJSON_Delete(root);
}

static void ap_wifi_handle_ws_receive(uint8_t *payload, int len)
{
    cJSON *root = cJSON_Parse((char *)payload);
    (void)len;

    if (root == NULL) {
        ESP_LOGE(AP_WIFI_TAG, "Receive invalid json");
        return;
    }

    cJSON *scan_js = cJSON_GetObjectItem(root, "scan");
    cJSON *ssid_js = cJSON_GetObjectItem(root, "ssid");
    cJSON *password_js = cJSON_GetObjectItem(root, "password");

    if (scan_js != NULL) {
        char *scan_value = cJSON_GetStringValue(scan_js);
        if (scan_value != NULL && strcmp(scan_value, "start") == 0) {
            wifi_manager_scan(ap_wifi_handle_scan_finished);
        }
    }

    if (ssid_js != NULL && password_js != NULL) {
        char *ssid = cJSON_GetStringValue(ssid_js);
        char *password = cJSON_GetStringValue(password_js);
        if (ssid != NULL && password != NULL) {
            snprintf(s_current_ssid, sizeof(s_current_ssid), "%s", ssid);
            snprintf(s_current_password, sizeof(s_current_password), "%s", password);
            save_wifi_to_nvs(ssid, password);
            reset_sync_ui_timer();
            ESP_LOGI(AP_WIFI_TAG, "Receive ssid:%s,password:%s,now stop http server", s_current_ssid, s_current_password);
            xEventGroupSetBits(s_apcfg_event, AP_WIFI_CONFIG_BIT);
        }
    }

    cJSON_Delete(root);
}

static int captive_dns_question_len(const uint8_t *packet, int len)
{
    int pos = 12;

    if (len <= 12) {
        return -1;
    }

    while (pos < len && packet[pos] != 0) {
        int label_len = packet[pos];
        pos += label_len + 1;
    }

    if ((pos + 5) > len) {
        return -1;
    }

    return (pos + 1 + 4) - 12;
}

static int captive_dns_build_response(const uint8_t *request, int request_len,
                                      uint8_t *response, int response_cap)
{
    int question_len = captive_dns_question_len(request, request_len);
    int answer_pos;

    if (question_len < 0 || response_cap < (12 + question_len + 16)) {
        return -1;
    }

    memset(response, 0, response_cap);
    memcpy(response, request, 2);
    response[2] = 0x81;
    response[3] = 0x80;
    response[4] = 0x00;
    response[5] = 0x01;
    response[6] = 0x00;
    response[7] = 0x01;

    memcpy(response + 12, request + 12, question_len);
    answer_pos = 12 + question_len;

    response[answer_pos++] = 0xC0;
    response[answer_pos++] = 0x0C;
    response[answer_pos++] = 0x00;
    response[answer_pos++] = 0x01;
    response[answer_pos++] = 0x00;
    response[answer_pos++] = 0x01;
    response[answer_pos++] = 0x00;
    response[answer_pos++] = 0x00;
    response[answer_pos++] = 0x00;
    response[answer_pos++] = 0x3C;
    response[answer_pos++] = 0x00;
    response[answer_pos++] = 0x04;
    memcpy(response + answer_pos, s_ap_ip, sizeof(s_ap_ip));
    answer_pos += sizeof(s_ap_ip);

    return answer_pos;
}

static void captive_dns_task(void *param)
{
    (void)param;

    uint8_t rx_buf[AP_WIFI_DNS_MAX_PACKET_SIZE];
    uint8_t tx_buf[AP_WIFI_DNS_MAX_PACKET_SIZE];

    while (s_dns_running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int len = recvfrom(s_dns_sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&from_addr, &from_len);
        if (len <= 0) {
            continue;
        }

        int tx_len = captive_dns_build_response(rx_buf, len, tx_buf, sizeof(tx_buf));
        if (tx_len > 0) {
            sendto(s_dns_sock, tx_buf, tx_len, 0, (struct sockaddr *)&from_addr, from_len);
        }
    }

    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

static void captive_dns_start(void)
{
    if (s_dns_task_handle != NULL) {
        return;
    }

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_sock < 0) {
        ESP_LOGE(AP_WIFI_TAG, "create dns socket failed");
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(AP_WIFI_DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(s_dns_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGE(AP_WIFI_TAG, "bind dns socket failed");
        closesocket(s_dns_sock);
        s_dns_sock = -1;
        return;
    }

    struct timeval timeout = {
        .tv_sec = 1,
        .tv_usec = 0,
    };
    setsockopt(s_dns_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    s_dns_running = true;
    xTaskCreatePinnedToCore(captive_dns_task, "captive_dns", 4096, NULL, 3, &s_dns_task_handle, 0);
}

static void captive_dns_stop(void)
{
    if (s_dns_task_handle == NULL) {
        return;
    }

    s_dns_running = false;

    if (s_dns_sock >= 0) {
        closesocket(s_dns_sock);
        s_dns_sock = -1;
    }

    while (s_dns_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void ap_wifi_event_task(void *param)
{
    (void)param;

    while (1) {
        EventBits_t ev = xEventGroupWaitBits(s_apcfg_event, AP_WIFI_CONFIG_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10 * 1000));
        if (ev & AP_WIFI_CONFIG_BIT) {
            s_ap_config_active = false;
            captive_dns_stop();
            web_ws_stop();
            wifi_manager_connect(s_current_ssid, s_current_password);
        }
    }
}

void ap_wifi_init(p_wifi_state_callback f)
{
    s_index_html = ap_wifi_load_web_page_buffer();
    wifi_manager_init(f);
    s_apcfg_event = xEventGroupCreate();
    xTaskCreatePinnedToCore(ap_wifi_event_task, "apcfg", 4096, NULL, 2, NULL, 1);
}

void ap_wifi_set(const char *ssid, const char *password)
{
    wifi_manager_connect(ssid, password);
}

void ap_wifi_apcfg(bool enable)
{
    if (enable) {
        s_ap_config_active = true;
        wifi_manager_ap();
        ws_cfg_t ws = {
            .html_code = s_index_html,
            .receive_fn = ap_wifi_handle_ws_receive,
        };
        web_ws_start(&ws);
        captive_dns_start();
    }
}

static char s_saved_ssid[32] = {0};
static char s_saved_password[64] = {0};

static void ap_wifi_handle_state_change(WIFI_STATE state)
{
    if (state == WIFI_STATE_CONNECTED) {
        s_saved_wifi_connected = true;
        s_ap_config_active = false;
        xEventGroupSetBits(wifi_ev, WIFI_CONNECT_BIT);
    } else if (state == WIFI_STATE_DISCONNECTED) {
        xEventGroupClearBits(wifi_ev, WIFI_CONNECT_BIT);
    }
}

static void ap_wifi_saved_wifi_timeout_task(void *param)
{
    (void)param;

    vTaskDelay(pdMS_TO_TICKS(AP_WIFI_SAVED_CONNECT_TIMEOUT_MS));

    if (!s_saved_wifi_connected && !s_ap_config_active) {
        ESP_LOGW(AP_WIFI_TAG, "Saved Wi-Fi connect timeout, switching to AP config mode");
        ap_wifi_apcfg(true);
    }

    s_saved_wifi_timeout_task = NULL;
    vTaskDelete(NULL);
}

void save_wifi_to_nvs(const char *ssid, const char *password)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(AP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_str(my_handle, AP_WIFI_NVS_KEY_SSID, ssid);
        nvs_set_str(my_handle, AP_WIFI_NVS_KEY_PASSWORD, password);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI("NVS", "Wi-Fi info saved to NVS");
    }
}

static bool ap_wifi_load_from_nvs(char *ssid, size_t ssid_len, char *password, size_t pwd_len)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(AP_WIFI_NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t s_len = ssid_len;
    size_t p_len = pwd_len;

    err = nvs_get_str(my_handle, AP_WIFI_NVS_KEY_SSID, ssid, &s_len);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return false;
    }

    err = nvs_get_str(my_handle, AP_WIFI_NVS_KEY_PASSWORD, password, &p_len);
    nvs_close(my_handle);

    return (err == ESP_OK);
}

void ap_wifi_go(void)
{
    bool has_saved_wifi = ap_wifi_load_from_nvs(s_saved_ssid, sizeof(s_saved_ssid), s_saved_password, sizeof(s_saved_password));
    s_saved_wifi_connected = false;
    s_ap_config_active = false;

    if (has_saved_wifi) {
        ESP_LOGI("MAIN", "Found saved Wi-Fi config: %s, trying auto connect...", s_saved_ssid);
        ap_wifi_init(ap_wifi_handle_state_change);
        wifi_manager_connect(s_saved_ssid, s_saved_password);
        if (s_saved_wifi_timeout_task == NULL) {
            xTaskCreatePinnedToCore(ap_wifi_saved_wifi_timeout_task, "wifi_timeout", 3072, NULL, 2,
                                    &s_saved_wifi_timeout_task, 1);
        }
    } else {
        ESP_LOGI("MAIN", "No Wi-Fi config found, starting AP captive portal...");
        ap_wifi_init(ap_wifi_handle_state_change);
        ap_wifi_apcfg(true);
    }
}

void erase_wifi_from_nvs(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(AP_WIFI_NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_erase_key(my_handle, AP_WIFI_NVS_KEY_SSID);
        nvs_erase_key(my_handle, AP_WIFI_NVS_KEY_PASSWORD);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI("NVS", "Saved Wi-Fi config erased");
    }
}

bool ap_wifi_is_config_mode_active(void)
{
    return s_ap_config_active;
}
