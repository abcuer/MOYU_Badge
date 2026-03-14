#include <stdio.h>
#include "esp_log.h"
#include "wifi_manager.h"
#include "ws2812.h"
#include "led.h"
#include "bmp280.h"
#include "mpu6050.h"
#include "imu.h"
#include "oled.h"
#include "blood.h"
#include "max30102.h"
#include "nvs_flash.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define TAG "MAIN"

#define WIFI_CONNECT_BIT    BIT0
static EventGroupHandle_t   wifi_ev = NULL;

void onenet_upload_task(void *pvParameters) 
{
    // 等待 WiFi 连接成功后再开始 MQTT 流程
    ESP_LOGI(TAG, "Wait for WiFi...");
    xEventGroupWaitBits(wifi_ev, WIFI_CONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    
    // 启动 OneNET MQTT 连接
    onenet_start();

    while (1) {
        // 1. 读取传感器并生成 JSON
        cJSON *prop_json = onenet_property_upload_dm();
        
        if (prop_json != NULL) {
            char *post_data = cJSON_PrintUnformatted(prop_json);
            
            // 2. 上报数据
            onenet_post_property_data(post_data);
            
            ESP_LOGI(TAG, "Uploaded Data: %s", post_data);

            // 3. 必须释放内存
            cJSON_free(post_data);
            cJSON_Delete(prop_json);
        }

        // 4. 每 10 秒上报一次
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// static void wifi_state_callback(WIFI_STATE state)
// {
//     if(state == WIFI_STATE_CONNECTED)
//     {
//         xEventGroupSetBits(wifi_ev, WIFI_CONNECT_BIT);
//     }
// }

void app_main(void)
{
    // 基础初始化
    // esp_err_t ret = nvs_flash_init();
    // if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    //     ESP_ERROR_CHECK(nvs_flash_erase());
    //     ret = nvs_flash_init();
    // }
    // ESP_ERROR_CHECK(ret);

    // // 初始化事件组
    // wifi_ev = xEventGroupCreate();

    // 硬件初始化
    // bmp280_init();
    mpu_init();
    max30102_init();
    u8g2_init();
    OLED_DrawBluetoothIcon(48, 16);
    // 网络初始化
    // wifi_manager_init(wifi_state_callback);
    // wifi_manager_connect("MIKASAYA", "13531257359");

    // 创建上报任务 (任务内部会处理等待 WiFi 的逻辑)
    // xTaskCreate(onenet_upload_task, "onenet_upload_task", 1024 * 4, NULL, 5, NULL);

    // 重点：app_main 是一个任务，完成后必须进入阻塞或直接返回
    // 绝对不能写一个没有任何 delay 的空 while(1)
    while(1) 
    {
        BloodDataUpdate();     // 采集 512 个点
        BloodDataTranslate();  // 算法处理
        
        printf("心率: %d bpm, 血氧: %.2f%%\n", b_data.heart, b_data.SpO2);
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}