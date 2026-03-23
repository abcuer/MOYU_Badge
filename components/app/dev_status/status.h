#ifndef __STATUS_H
#define __STATUS_H

// 天气数据结构
typedef struct {
    char city[32];        // 城市
    char weather[32];     // 天气状况，如"晴"
    int  temp_now;        // 当前温度
    int  temp_high;       // 最高温
    int  temp_low;        // 最低温
    float sea_level_hpa;  // 当地海平面大气压
    char update_time[32]; // 更新时间
} WeatherData_t;

void fetch_time(void);
void fetch_weather(void);

extern WeatherData_t weather_data;

#endif
