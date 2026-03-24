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
    // 1. 读取系统当前时间戳
    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", "123");
    cJSON_AddStringToObject(root, "version", "1.0");
    cJSON* params_js = cJSON_AddObjectToObject(root, "params");

    // ── 📅 1. 日期 ───────────────────────────────────────────
    char date_buffer[32]; 
    snprintf(date_buffer, sizeof(date_buffer), "%04d-%02d-%02d", 
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

    cJSON* date_js = cJSON_AddObjectToObject(params_js, "dev_date"); 
    cJSON_AddStringToObject(date_js, "value", date_buffer);

    // ── 🌦️ 2. 天气、温度与城市 ────────────────────────────────
    // 天气状况 (字符串)
    cJSON* weather_js = cJSON_AddObjectToObject(params_js, "dev_weather");
    cJSON_AddStringToObject(weather_js, "value", weather_data.weather);

    // 城市信息
    cJSON* city_js = cJSON_AddObjectToObject(params_js, "dev_city");
    cJSON_AddStringToObject(city_js, "value", "惠州");

    // 1. 气压 (保留两位小数)
    cJSON* pres_js = cJSON_AddObjectToObject(params_js, "pres");
    double final_pres = (int)(bmp280.pressure * 100.0) / 100.0; // 注意：没有 f，使用 double 运算
    cJSON_AddNumberToObject(pres_js, "value", final_pres);

    // 2. 室内温度 (保留两位小数)
    cJSON* temp_in_js = cJSON_AddObjectToObject(params_js, "temp_in");
    double final_temp_in = (int)(bmp280.temperature * 100.0) / 100.0; // 注意：没有 f
    cJSON_AddNumberToObject(temp_in_js, "value", final_temp_in);
        
    // 室外温度 
    cJSON* temp_out_js = cJSON_AddObjectToObject(params_js, "temp_out");
    cJSON_AddNumberToObject(temp_out_js, "value", weather_data.temp_now);

    // ── 🩸 3. 心率血氧 (来自 bloods.c 里的全局变量 b_data) ───
    cJSON* hr_js = cJSON_AddObjectToObject(params_js, "heart_rate");
    cJSON_AddNumberToObject(hr_js, "value", b_data.heart);

    cJSON* spo2_js = cJSON_AddObjectToObject(params_js, "Sp02");
    // 保留一位小数
    double final_spo2 = (int)(b_data.SpO2 * 10) / 10.0; 
    cJSON_AddNumberToObject(spo2_js, "value", final_spo2);

    // ── 🧭 4. 欧拉角 (来自 mpu6050 运动解算的 euler_angle) ───
    cJSON* pitch_js = cJSON_AddObjectToObject(params_js, "pitch");
    cJSON_AddNumberToObject(pitch_js, "value", (int)(euler_angle.pitch * 10) / 10.0);

    cJSON* roll_js = cJSON_AddObjectToObject(params_js, "roll");
    cJSON_AddNumberToObject(roll_js, "value", (int)(euler_angle.roll * 10) / 10.0);

    cJSON* yaw_js = cJSON_AddObjectToObject(params_js, "yaw");
    cJSON_AddNumberToObject(yaw_js, "value", (int)(euler_angle.yaw * 10) / 10.0);


    return root;
}