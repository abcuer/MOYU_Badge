#include "headfile.h"

#define TEST "HELLO"

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

    // 同步时间
    xTaskCreate(time_sync_task, "time_sync", 8192, NULL, 8, NULL);

    // WiFi 初始化
    wifi_manager_init(wifi_state_callback);
    wifi_manager_connect("MIKASAYA", "13531257359");

    // 硬件初始化
    key_device_init();
    max30102_init();
    mpu6050_init();
    // bmp280_init();   
    u8g2_init();

    // 启动任务
    xTaskCreate(start_mpu_task, "mpu_task", 4096, NULL, 5, NULL);
    xTaskCreate(start_oled_task, "oled_ui", 8192, NULL, 4, NULL);
    // xTaskCreate(start_detect_task, "blood_task", 4096, NULL, 5, NULL);
    // xTaskCreate(onenet_upload_task, "upload_task", 4096, NULL, 5, NULL);

    // while(1)
    // {
    //     imu_get_angle(&acc, &gyro, &euler_angle, 20/1000.0f);
    //     ESP_LOGI("MPU", "p:%.2f, r: %.2f, y:%.2f\n", euler_angle.pitch, euler_angle.roll, euler_angle.yaw);
    //     vTaskDelay(pdMS_TO_TICKS(20));
    // }

}