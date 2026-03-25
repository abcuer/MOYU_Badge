#include "headfile.h"

volatile bool is_first_sync_done = false;

/**
 * @brief 同步任务：等待 WiFi 连接 -> 初始化 SNTP -> 等待对时成功
 */
void start_sync_task(void *pvParameters)
{
    // 🎯 1. 【开机第一次】：死等网络连接成功
    xEventGroupWaitBits(wifi_ev, WIFI_CONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    fetch_time();     // 开机抓时间
    fetch_weather();  // 开机抓天气

    xEventGroupSetBits(wifi_ev, TIME_SYNC_BIT); // 释放时间同步标志
    is_first_sync_done = true; // 宣布开机大功告成！

    while (1) 
    {
        vTaskDelay(pdMS_TO_TICKS(30 * 60 * 1000)); // 挂起 30 分钟
        fetch_weather(); // 30 分钟后更新一次天气
    }
}

void start_sensor_task(void *pvParameters)
{
    mpu6050_init();
    bmp280_init();
    max30102_init();
    xEventGroupWaitBits(wifi_ev, TIME_SYNC_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    while(1)
    {   
        if(mode == MODE_BALL || mode == MODE_DINO || mode == MODE_PLANE) {
            imu_get_angle(&acc, &gyro, &euler_angle, 20.0f/1000.0f);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        else if(mode == MODE_CLOCK) {
            bmp280_read_data(&bmp280);  
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        else if(mode == MODE_BLOOD) {
            blood_detect();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        else{
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

void start_onenet_task(void *pvParameters) 
{
    xEventGroupWaitBits(wifi_ev, TIME_SYNC_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
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

void start_key_task(void *pvParameters) 
{
    key_device_init(); // 初始化中断和 GPIO
    
    while(1) 
    {
        // 🛑 阻塞在这里，不耗 CPU。直到中断按下 Give 了信号，它才瞬间醒来！
        if (xSemaphoreTake(key_sem, portMAX_DELAY) == pdTRUE) 
        {
            // 💡 醒来后，开始跑状态机轮询，直到按键释放回到 IDLE 状态
            while (1) 
            {
                key_scan(); 
        
                if (key_is_idle(KEY_USER)) {
                    break; 
                }

                vTaskDelay(pdMS_TO_TICKS(10)); 
            }
        }
    }
}

void start_oled_task(void *pvParameters)
{
    u8g2_init(); //

    // 🎯 1. 只有开机第一次没同步完，才进这里
    while (!is_first_sync_done) 
    {
        draw_syncing_ui(&u8g2); //
        vTaskDelay(pdMS_TO_TICKS(OLED_PERIOD)); 
    }

    // 🎯 2. 用一个绝对无法跳出的外层 while(1) 锁死任务，绝不允许代码坠落到上面去！
    last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS; // 进场先刷一次时间戳

    while (1) // 内部 UI 刷新小循环
    {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS; 

        // 🎯 低功耗守卫 (35秒无操作)
        if ((mode == MODE_CLOCK || mode == MODE_GAME_SELECT || mode == MODE_SETTING) && !in_select && (now - last_action_time > 35000)) 
        {
            enter_light_sleep(); //
            
            // 🚀 苏醒瞬间，立刻刷新 OLED 任务自己的本地时间戳，防止滑动坠落！
            last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            continue; //
        }

        if(in_select) //
        {
            draw_select_ui(&u8g2, selected_game); //
        }
        else //
        {
            switch (mode) //
            {
                case MODE_CLOCK:   draw_main_clock_ui(&u8g2); break; //
                case MODE_BALL:    draw_ball_game(&u8g2);     break; //
                case MODE_DINO:    draw_dino_game(&u8g2);     break; //
                case MODE_PLANE:   draw_plane_game(&u8g2);    break; //
                case MODE_BLOOD:   draw_blood_ui(&u8g2);      break; //
                case MODE_SETTING: draw_setting_ui(&u8g2);    break; //
                default: break; //
            }
        }

        vTaskDelay(pdMS_TO_TICKS(OLED_PERIOD)); //
    }
}