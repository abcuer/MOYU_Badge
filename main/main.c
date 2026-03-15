#include <stdio.h>
#include "esp_log.h"

#include "ws2812.h"
#include "led.h"
#include "bmp280.h"
#include "mpu6050.h"
#include "imu.h"
#include "oled.h"
#include "blood.h"
#include "max30102.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "user_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

EventGroupHandle_t wifi_ev = NULL; // 定义全局变量

static void wifi_state_callback(WIFI_STATE state)
{
    if(state == WIFI_STATE_CONNECTED)
    {
        xEventGroupSetBits(wifi_ev, WIFI_CONNECT_BIT);
    }
}

void app_main(void) 
{
    nvs_flash_init();
    wifi_ev = xEventGroupCreate(); // 必须最先创建！
    // WiFi 初始化
    wifi_manager_init(wifi_state_callback);
    wifi_manager_connect("MIKASAYA", "13531257359");

    // 硬件初始化
    mpu_init();
    max30102_init();
    u8g2_init();

    // 启动任务
    // xTaskCreate(start_mpu_task, "mpu_task", 4096, NULL, 5, NULL);
    // xTaskCreate(start_detect_task, "blood_task", 4096, NULL, 5, NULL);
    // xTaskCreate(onenet_upload_task, "upload_task", 4096, NULL, 5, NULL);

    // while(1) 
    // {

    // }
}