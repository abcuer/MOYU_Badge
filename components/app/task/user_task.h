#ifndef __USER_TASK_H
#define __USER_TASK_H

#include "freertos/event_groups.h"
#define WIFI_CONNECT_BIT    BIT0
#define TIME_SYNC_BIT       BIT1 

#define SENSOR_PERIOD   20
#define OLED_PERIOD     30
#define onenet_PERIOD   1000 * 60 * 30

void start_sync_task(void *pvParameters);
void start_sensor_task(void *pvParameters);
void start_onenet_task(void *pvParameters);
void start_oled_task(void *pvParameters);
void start_key_task(void *pvParameters);
void sensor_request_power_sync(void);

extern EventGroupHandle_t  wifi_ev;
extern volatile bool is_first_sync_done;
extern volatile bool has_started_ap_config;

#endif
