#include "headfile.h"

#define SENSOR_PERIOD   20
#define SP02_PERIOD     10
#define OLED_PERIOD     30
#define onenet_PERIOD   1000

/**
 * @brief 时间同步任务：等待 WiFi 连接 -> 同步时间 -> 功成身退
 */
static const char *TAG_TIME = "NTP_TIME";

/**
 * @brief 设置北京时区
 */
static void set_timezone(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    ESP_LOGI(TAG_TIME, "时区设置为北京时间 (CST-8)");
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

    ESP_LOGI(TAG_TIME, "当前时间: %s", buffer);
}

/**
 * @brief 时间同步任务：等待 WiFi 连接 -> 初始化 SNTP -> 等待对时成功
 */
void time_sync_task(void *pvParameters)
{
    xEventGroupWaitBits(wifi_ev, WIFI_CONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(500));
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
        ESP_LOGI(TAG_TIME, "✅ NTP 时间同步成功！");
    } else {
        ESP_LOGW(TAG_TIME, "⚠️ 同步超时，使用备用方案重试");
        // 超时后销毁重建，强制重试一次
        esp_netif_sntp_deinit();
        vTaskDelay(pdMS_TO_TICKS(3000));

        esp_sntp_config_t retry_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("166.111.206.172");
        esp_netif_sntp_init(&retry_config);
        esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000));
    }

    print_current_time();
    esp_netif_sntp_deinit();
    // ← 无论成功失败，都放行其他任务（失败也总比卡死好）
    xEventGroupSetBits(wifi_ev, TIME_SYNC_BIT);

    vTaskDelete(NULL);
}

void start_sensor_task(void *pvParameters)
{
    static int slow_counter = 0;
    while(1)
    {
        key_scan();

        if(mode != MODE_BLOOD)
            imu_get_angle(&acc, &gyro, &euler_angle, SENSOR_PERIOD/1000.0f);
        if(mode == MODE_CLOCK)
        {
            step_detect(&acc);
            slow_counter++;
            if (slow_counter >= 10)  // 每100ms执行一次
            {
                slow_counter = 0;
                bmp280_read_data(&bmp280);  
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD));
    }
}

void start_sp02_task(void *pvParameters)
{
    while(1)
    {
        if(mode == MODE_BLOOD)  blood_detect();
        vTaskDelay(pdMS_TO_TICKS(SP02_PERIOD));
    }
}

void onenet_upload_task(void *pvParameters) 
{
    xEventGroupWaitBits(wifi_ev, WIFI_CONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    // 启动 OneNET MQTT 连接
    onenet_start();

    while (1) 
    {
        // 1. 读取传感器并生成 JSON
        cJSON *prop_json = onenet_property_upload_dm();
        
        if (prop_json != NULL) 
        {
            char *post_data = cJSON_PrintUnformatted(prop_json);
            
            // 2. 上报数据
            onenet_post_property_data(post_data);

            // 3. 必须释放内存
            cJSON_free(post_data);
            cJSON_Delete(prop_json);
        }

        vTaskDelay(pdMS_TO_TICKS(onenet_PERIOD));
    }
}

void start_oled_task(void *pvParameters)
{
    while (!(xEventGroupGetBits(wifi_ev) & TIME_SYNC_BIT)) {
        draw_syncing_ui(&u8g2);
        vTaskDelay(pdMS_TO_TICKS(OLED_PERIOD));
    }

    while (1)
    {
        if (in_select)
        {
            // 选择模式：显示选择UI（覆盖当前界面）
            draw_select_ui(&u8g2, game_list[selected_game]);
        }
        else
        {
            switch (mode)
            {
                case MODE_CLOCK: draw_main_clock_ui(&u8g2); break;
                case MODE_BALL:  draw_ball_game(&u8g2);     break;
                case MODE_DINO:  draw_dino_game(&u8g2);     break;
                case MODE_PLANE: draw_plane_game(&u8g2);    break;
                case MODE_BLOOD: draw_blood_ui(&u8g2);      break;
                default: break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(OLED_PERIOD));
    }
}