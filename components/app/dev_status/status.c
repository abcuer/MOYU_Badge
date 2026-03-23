#include "headfile.h"
#include "esp_crt_bundle.h"

/**
 * @brief 设置北京时区
 */
static void set_timezone(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    ESP_LOGI("NTP_TIME", "时区设置为北京时间 (CST-8)");
}

/**
 * @brief 打印当前系统时间
 */
static void print_current_time(void)
{
    time_t now = time(NULL);
    struct tm timeinfo;
    char buffer[64];

    localtime_r(&now, &timeinfo);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %A", &timeinfo);

    ESP_LOGI("NTP_TIME", "当前时间: %s", buffer);
}

/**
 * @brief 时间同步任务：等待 WiFi 连接 -> 同步时间 -> 功成身退
 */

void fetch_time(void)
{ 
    set_timezone();

    // 同时配置3个服务器，用直接IP避免DNS问题
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST(
            "166.111.206.172"  // 清华源 直接IP
            "120.25.115.20",   // 腾讯云 直接IP
            "203.107.6.88",    // 阿里云 直接IP
        )
    );
    esp_netif_sntp_init(&config);

    // 超时改为60秒，给足时间
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(60000));
    if (err == ESP_OK) {
        ESP_LOGI("NTP_TIME", "✅ NTP 时间同步成功！");
    } else {
        ESP_LOGW("NTP_TIME", "⚠️ 同步超时，使用备用方案重试");
        // 超时后销毁重建，强制重试一次
        esp_netif_sntp_deinit();
        vTaskDelay(pdMS_TO_TICKS(3000));

        esp_sntp_config_t retry_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("166.111.206.172");
        esp_netif_sntp_init(&retry_config);
        esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000));
    }

    print_current_time();
    esp_netif_sntp_deinit();
}

WeatherData_t weather_data = {0};

// HTTP响应缓冲
static char http_buf[2048];
static int  http_buf_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int copy_len = evt->data_len;
        if (http_buf_len + copy_len < sizeof(http_buf)) {
            memcpy(http_buf + http_buf_len, evt->data, copy_len);
            http_buf_len += copy_len;
            http_buf[http_buf_len] = '\0';
        }
    }
    return ESP_OK;
}

void fetch_weather(void)
{
    http_buf_len = 0;
    memset(http_buf, 0, sizeof(http_buf));

    // Open-Meteo 不需要API Key
    const char *url = "http://api.open-meteo.com/v1/forecast"
                      "?latitude=23.09"     // 所在地区经纬度
                      "&longitude=114.41"
                      "&current=temperature_2m,surface_pressure,weathercode"
                      "&timezone=Asia%2FShanghai";

    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = http_event_handler,
        .timeout_ms    = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE("WEATHER", "HTTP client 初始化失败");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        cJSON *root = cJSON_Parse(http_buf);
        if (root) {
            cJSON *current = cJSON_GetObjectItem(root, "current");
            if (current) {
                cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
                cJSON *pres = cJSON_GetObjectItem(current, "surface_pressure");
                cJSON *code = cJSON_GetObjectItem(current, "weathercode");

                if (temp) weather_data.temp_now = (int)temp->valuedouble;

                if (pres) weather_data.sea_level_hpa = (float)pres->valuedouble;

                if (code) {
                    int wc = code->valueint;
                    if      (wc == 0)  strncpy(weather_data.weather, "Sunny", 31);
                    else if (wc <= 3)  strncpy(weather_data.weather, "Cloudy", 31);
                    else if (wc <= 67) strncpy(weather_data.weather, "Rainy", 31);
                    else if (wc <= 77) strncpy(weather_data.weather, "Snowy", 31);
                    else               strncpy(weather_data.weather, "Storm", 31);
                }

                // 只打印这四项
                ESP_LOGI("WEATHER", "城市: 惠州");  // Open-Meteo不返回城市名，直接写死
                ESP_LOGI("WEATHER", "天气: %s",   weather_data.weather);
                ESP_LOGI("WEATHER", "温度: %d°C", weather_data.temp_now);
                ESP_LOGI("WEATHER", "气压: %.1f hPa", weather_data.sea_level_hpa);
            }
            cJSON_Delete(root);
        }
    }
    else {
        ESP_LOGW("WEATHER", "天气获取失败: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// 心知天气接口
/*
void fetch_weather(void)
{
    http_buf_len = 0;
    memset(http_buf, 0, sizeof(http_buf));

    // 替换为你的 API Key 和城市
    // location 可以用城市拼音，如 shenzhen/beijing/guangzhou
    // 或用经纬度：location=113.92:22.53
    const char *url = "https://api.seniverse.com/v3/weather/now.json"
                      "?key=SWEwZCGgkIQ3PlQ3D" // 密钥
                      "&location=huizhou"   // 城市
                      "&language=zh-Hans"   // 语言
                      "&unit=c";

    esp_http_client_config_t cfg = {
        .url             = url,
        .event_handler   = http_event_handler,
        .timeout_ms      = 10000,
        .transport_type  = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = esp_crt_bundle_attach,  // ← 加这行，使用内置证书束
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) 
    {                          // ← 加这个判断
        ESP_LOGE("WEATHER", "HTTP client 初始化失败，检查URL");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        // 解析JSON
        cJSON *root = cJSON_Parse(http_buf);
        if (root) {
            cJSON *results = cJSON_GetObjectItem(root, "results");
            if (results && cJSON_IsArray(results)) {
                cJSON *first  = cJSON_GetArrayItem(results, 0);
                cJSON *loc    = cJSON_GetObjectItem(first, "location");
                cJSON *now    = cJSON_GetObjectItem(first, "now");
                cJSON *upd    = cJSON_GetObjectItem(first, "last_update");

                if (loc && now) {
                    strncpy(weather_data.city,
                            cJSON_GetObjectItem(loc, "name")->valuestring, 31);
                    strncpy(weather_data.weather,
                            cJSON_GetObjectItem(now, "text")->valuestring, 31);
                    weather_data.temp_now =
                            atoi(cJSON_GetObjectItem(now, "temperature")->valuestring);
                    if (upd)
                        strncpy(weather_data.update_time, upd->valuestring, 31);

                    ESP_LOGI("WEATHER", "城市:%s 天气:%s 温度:%d°C",
                             weather_data.city,
                             weather_data.weather,
                             weather_data.temp_now);
                }
            }
            cJSON_Delete(root);
        }
    } else {
        ESP_LOGW("WEATHER", "天气获取失败: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

*/