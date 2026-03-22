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
    // ── 第一步：最高优先，WiFi尽早启动 ──────
    nvs_flash_init();
    wifi_ev = xEventGroupCreate();
    wifi_manager_init(wifi_state_callback);
    wifi_manager_connect("MIKASAYA", "13531257359");

    // ── 第二步：立即启动依赖WiFi的任务 ──────
    // WiFi已经在后台连接，这两个任务会自己等待连接成功
    xTaskCreate(time_sync_task, "time_sync", 8192, NULL, 8, NULL);

    // ── 第三步：初始化显示，尽早给用户反馈 ──
    u8g2_init();
    xTaskCreate(start_oled_task, "ui_task", 8192, NULL, 4, NULL);
    // OLED任务会显示"同步时间中..."，用户知道设备在工作

    // ── 第四步：其他硬件初始化 ───────────────
    key_device_init();
    max30102_init();
    mpu6050_init();
    bmp280_init();

    // ── 第五步：启动传感器任务 ───────────────
    xTaskCreate(start_sensor_task, "sensor_task", 8192, NULL, 6, NULL);
    xTaskCreate(start_sp02_task,   "sp02_task",   8192, NULL, 5, NULL);
    // xTaskCreate(onenet_upload_task, "upload_task", 4096, NULL, 3, NULL);

    // while(1)
    // {
    //     bmp280_read_data(&bmp280);
    //     ESP_LOGI("BMP", "Temp: %.2f C, Pres: %.2f hPa", bmp280.temperature, bmp280.pressure);
    //     vTaskDelay(pdMS_TO_TICKS(20));
    // }
}