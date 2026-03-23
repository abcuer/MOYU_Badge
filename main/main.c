#include "headfile.h"

void app_main(void) 
{
    // ── 第一步：最高优先，WiFi尽早启动 ──────
    nvs_flash_init();
    wifi_ev = xEventGroupCreate();

    // ── 第二步：立即启动依赖WiFi的任务 ──────
    // WiFi已经在后台连接，同步任务会自己等待连接成功
    xTaskCreate(start_sync_task, "sync_status", 8192, NULL, 8, NULL);

    // ── 第三步：初始化显示，尽早给用户反馈 ──
    xTaskCreate(start_oled_task, "ui_task", 8192, NULL, 4, NULL);
    // OLED任务会显示"同步时间中..."，用户知道设备在工作

    // ── 第四步：启动传感器任务 ───────────────
    xTaskCreate(start_sensor_task, "sensor_task", 8192, NULL, 6, NULL);
    xTaskCreate(start_sp02_task,   "sp02_task",   8192, NULL, 5, NULL);
    xTaskCreate(onenet_upload_task, "upload_task", 4096, NULL, 3, NULL);

    // while(1)
    // {
    //     bmp280_read_data(&bmp280);
    //     ESP_LOGI("BMP", "Temp: %.2f C, Pres: %.2f hPa", bmp280.temperature, bmp280.pressure);
    //     vTaskDelay(pdMS_TO_TICKS(20));
    // }
}