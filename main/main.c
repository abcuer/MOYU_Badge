#include "headfile.h"

TaskHandle_t sensor_task_handle = NULL;
TaskHandle_t sync_task_handle = NULL;
EventGroupHandle_t wifi_ev = NULL;

/* 新机器需要配网:
    连接wifi: ESP32-AP 密码：12345678，浏览器配网搜索"192.168.100.1"
    配置完毕后，之后 ESP32 会搜索已配置的网络自动连接
*/

void app_main(void) 
{
    // ── 第一步：最高优先，WiFi尽早启动 ──────
    nvs_flash_init();
    ota_mark_app_valid_if_needed();
    settings_init();
    wifi_ev = xEventGroupCreate();
    ap_wifi_go();
    audio_player_init();
    recorder_init();
    ai_chat_init();
    audio_player_set_volume(settings_get_volume());

    // ── 第二步：立即启动依赖WiFi的任务 ──────
    // WiFi已经在后台连接，同步任务会自己等待连接成功
    xTaskCreate(start_sync_task, "sync_task", 8192, NULL, 8, &sync_task_handle);
    // ── 第三步：初始化显示，尽早给用户反馈 ──
    xTaskCreate(start_oled_task, "ui_task", 8192, NULL, 4, NULL);
    // ── 第四步：启动传感器任务 ───────────────
    xTaskCreate(start_sensor_task, "sensor_task", 8192, NULL, 6, &sensor_task_handle);
    xTaskCreate(start_key_task, "key_task", 4196, NULL, 7, NULL);
    xTaskCreate(start_onenet_task, "upload_task", 8192, NULL, 3, NULL);
}
