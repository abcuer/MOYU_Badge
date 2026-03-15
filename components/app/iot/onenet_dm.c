#include <string.h>
#include "esp_log.h"
#include "bmp280.h"
#include "onenet_dm.h"
#include "onenet_mqtt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "onenet_dm";

/**
 * 物模型数据初始化
 */
void onenet_dm_init(void)
{
    ESP_LOGI(TAG, "OneNET DM Sensor mode initialized.");
}

/**
 * 处理onenet下行的数据
 */
void onenet_property_handle(cJSON* property_js)
{
    // 由于 LED 已删除，这里仅打印接收到的原始数据，不做逻辑处理
    char *raw_data = cJSON_PrintUnformatted(property_js);
    ESP_LOGI(TAG, "Downlink data received (No action defined): %s", raw_data);
    cJSON_free(raw_data);
}


cJSON* onenet_property_upload_dm(void)
{
    bmp280_read_data(&bmp280); 

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", "123");
    cJSON_AddStringToObject(root, "version", "1.0");
    cJSON* params_js = cJSON_AddObjectToObject(root, "params");
    
    // 1. 处理温度
    cJSON* temp_js = cJSON_AddObjectToObject(params_js, "CurrentTemperature");
    // 使用 %.2f 强制截断，只留两位小数
    char temp_buf[32];
    snprintf(temp_buf, sizeof(temp_buf), "%.2f", bmp280.temperature); 
    cJSON_AddNumberToObject(temp_js, "value", atof(temp_buf));

    // 2. 处理气压
    cJSON* pres_js = cJSON_AddObjectToObject(params_js, "CurrentPresure"); 
    char pres_buf[32];
    snprintf(pres_buf, sizeof(pres_buf), "%.2f", bmp280.pressure);
    cJSON_AddNumberToObject(pres_js, "value", atof(pres_buf));

    return root;
}