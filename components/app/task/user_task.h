#ifndef __USER_TASK_H
#define __USER_TASK_H

#include "freertos/event_groups.h"
#define WIFI_CONNECT_BIT    BIT0
#define TIME_SYNC_BIT       BIT1 

#define SENSOR_PERIOD   20
#define SP02_PERIOD     20
#define OLED_PERIOD     30
#define onenet_PERIOD   3000

void start_sync_task(void *pvParameters);
void start_sensor_task(void *pvParameters);
void onenet_upload_task(void *pvParameters);
void start_oled_task(void *pvParameters);

extern EventGroupHandle_t  wifi_ev;

#endif