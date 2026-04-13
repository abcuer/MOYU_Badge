#include "headfile.h"

#define MAIN_SYNC_TASK_STACK_SIZE    8192U
#define MAIN_SYNC_TASK_PRIORITY      8U
#define MAIN_OLED_TASK_STACK_SIZE    8192U
#define MAIN_OLED_TASK_PRIORITY      4U
#define MAIN_SENSOR_TASK_STACK_SIZE  8192U
#define MAIN_SENSOR_TASK_PRIORITY    6U
#define MAIN_KEY_TASK_STACK_SIZE     4196U
#define MAIN_KEY_TASK_PRIORITY       7U
#define MAIN_ONENET_TASK_STACK_SIZE  8192U
#define MAIN_ONENET_TASK_PRIORITY    3U

TaskHandle_t sensor_task_handle = NULL;
TaskHandle_t sync_task_handle = NULL;
EventGroupHandle_t wifi_ev = NULL;

static void main_create_task(TaskFunction_t task_fn,
                             const char *task_name,
                             uint32_t stack_size,
                             UBaseType_t priority,
                             TaskHandle_t *task_handle)
{
    BaseType_t result = xTaskCreate(task_fn, task_name, stack_size, NULL, priority, task_handle);
    configASSERT(result == pdPASS);
}

static void main_start_background_tasks(void)
{
    main_create_task(start_sync_task,
                     "sync_task",
                     MAIN_SYNC_TASK_STACK_SIZE,
                     MAIN_SYNC_TASK_PRIORITY,
                     &sync_task_handle);
    main_create_task(start_oled_task,
                     "ui_task",
                     MAIN_OLED_TASK_STACK_SIZE,
                     MAIN_OLED_TASK_PRIORITY,
                     NULL);
    main_create_task(start_sensor_task,
                     "sensor_task",
                     MAIN_SENSOR_TASK_STACK_SIZE,
                     MAIN_SENSOR_TASK_PRIORITY,
                     &sensor_task_handle);
    main_create_task(start_key_task,
                     "key_task",
                     MAIN_KEY_TASK_STACK_SIZE,
                     MAIN_KEY_TASK_PRIORITY,
                     NULL);
    main_create_task(start_onenet_task,
                     "upload_task",
                     MAIN_ONENET_TASK_STACK_SIZE,
                     MAIN_ONENET_TASK_PRIORITY,
                     NULL);
}

void app_main(void)
{
    /* 新机器需要先配网：
     * 1. 连接热点 `MoYu_Modge`
     * 2. 输入密码 `12345678`
     * 配置完成后，设备会自动连接已保存的网络。
     */
    nvs_flash_init();
    wifi_ev = xEventGroupCreate();

    ap_wifi_go();
    main_start_background_tasks();

    settings_init();
    audio_player_init();
    recorder_init();
    audio_player_set_volume(settings_get_volume());
}
