#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ws_server.h"

static const char *TAG = "WebSocket Server";
static const char *http_html = NULL;
static ws_receive_cb ws_receive_fn = NULL;
static httpd_handle_t http_ws_server = NULL;
static int client_sockfd = -1;

#define CAPTIVE_PORTAL_URL "http://192.168.100.1/"

static esp_err_t handle_ws_req(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        client_sockfd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Save client_fds:%d", client_sockfd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }

        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_receive_fn) {
        ws_receive_fn(ws_pkt.payload, ws_pkt.len);
    }

    free(buf);
    return ESP_OK;
}

esp_err_t get_req_handler(httpd_req_t *req)
{
    if (http_html == NULL) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, http_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", CAPTIVE_PORTAL_URL);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "Redirecting to Wi-Fi setup portal", HTTPD_RESP_USE_STRLEN);
}

esp_err_t web_ws_send(uint8_t *data, int len)
{
    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(httpd_ws_frame_t));
    pkt.payload = data;
    pkt.len = len;
    pkt.type = HTTPD_WS_TYPE_TEXT;
    return httpd_ws_send_data(http_ws_server, client_sockfd, &pkt);
}

esp_err_t web_ws_start(ws_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_FAIL;
    }

    http_html = cfg->html_code;
    ws_receive_fn = cfg->receive_fn;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_uri_t uri_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_req_handler,
    };
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws_req,
        .is_websocket = true
    };
    httpd_uri_t captive_android = {
        .uri = "/generate_204",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
    };
    httpd_uri_t captive_apple = {
        .uri = "/hotspot-detect.html",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
    };
    httpd_uri_t captive_windows = {
        .uri = "/connecttest.txt",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
    };
    httpd_uri_t captive_ncsi = {
        .uri = "/ncsi.txt",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
    };
    httpd_uri_t captive_success = {
        .uri = "/library/test/success.html",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
    };
    httpd_uri_t captive_fallback = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = captive_redirect_handler,
    };

    if (httpd_start(&http_ws_server, &config) == ESP_OK) {
        httpd_register_uri_handler(http_ws_server, &uri_get);
        httpd_register_uri_handler(http_ws_server, &ws);
        httpd_register_uri_handler(http_ws_server, &captive_android);
        httpd_register_uri_handler(http_ws_server, &captive_apple);
        httpd_register_uri_handler(http_ws_server, &captive_windows);
        httpd_register_uri_handler(http_ws_server, &captive_ncsi);
        httpd_register_uri_handler(http_ws_server, &captive_success);
        httpd_register_uri_handler(http_ws_server, &captive_fallback);
    }

    return ESP_OK;
}

esp_err_t web_ws_stop(void)
{
    if (http_ws_server) {
        httpd_handle_t server = http_ws_server;
        http_ws_server = NULL;
        client_sockfd = -1;
        return httpd_stop(server);
    }

    return ESP_OK;
}
