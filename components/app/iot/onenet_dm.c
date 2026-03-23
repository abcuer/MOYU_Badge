#include "headfile.h"

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
    // 读取当前时间
    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", "123");
    cJSON_AddStringToObject(root, "version", "1.0");
    cJSON* params_js = cJSON_AddObjectToObject(root, "params");
    
    // 🌡️ 1. 处理温度
    cJSON* temp_js = cJSON_AddObjectToObject(params_js, "temp_in");
    // 直接强制使用 double 存储单片机计算出的干净截断值
    double final_temp = (int)(bmp280.temperature * 100) / 100.0; 
    cJSON_AddNumberToObject(temp_js, "value", final_temp);

    // 🌪️ 2. 处理气压
    cJSON* pres_js_obj = cJSON_AddObjectToObject(params_js, "pres"); 
    double final_pres = (int)(bmp280.pressure * 10) / 10.0;
    cJSON_AddNumberToObject(pres_js_obj, "value", final_pres);

    // 📅 3. 年
    cJSON* year_js = cJSON_AddObjectToObject(params_js, "year"); 
    cJSON_AddNumberToObject(year_js, "value", t.tm_year + 1900);

    // 📅 4. 月
    cJSON* month_js = cJSON_AddObjectToObject(params_js, "month"); 
    cJSON_AddNumberToObject(month_js, "value", t.tm_mon + 1);

    // 📅 5. 日
    cJSON* day_js = cJSON_AddObjectToObject(params_js, "day"); 
    cJSON_AddNumberToObject(day_js, "value", t.tm_mday);

    return root;
}
