#include "headfile.h"

EventGroupHandle_t wifi_ev = NULL; // 定义全局变量

static void wifi_state_callback(WIFI_STATE state)
{
    if(state == WIFI_STATE_CONNECTED)
    {
        xEventGroupSetBits(wifi_ev, WIFI_CONNECT_BIT);
    }
}

/**
 * @brief 同步任务：等待 WiFi 连接 -> 初始化 SNTP -> 等待对时成功
 */
void start_sync_task(void *pvParameters)
{
    wifi_manager_init(wifi_state_callback);
    wifi_manager_connect("MIKASAYA", "13531257359");

    xEventGroupWaitBits(wifi_ev, WIFI_CONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(500));
    // 获取时间和天气信息
    fetch_time();
    fetch_weather();
    // ← 无论成功失败，都放行其他任务（失败也总比卡死好）
    xEventGroupSetBits(wifi_ev, TIME_SYNC_BIT);

    // 不销毁任务，每30分钟刷新一次天气
    while (1) 
    {
        vTaskDelay(pdMS_TO_TICKS(30 * 60 * 1000));
        fetch_weather();
    }
}

void start_sensor_task(void *pvParameters)
{
    key_device_init();
    mpu6050_init();
    bmp280_init();

    static int slow_counter = 0;
    while(1)
    {
        key_scan();

        if(mode != MODE_BLOOD)
            imu_get_angle(&acc, &gyro, &euler_angle, SENSOR_PERIOD/1000.0f);
        if(mode == MODE_CLOCK)
        {
            // step_detect();
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
    max30102_init();
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
    u8g2_init();
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