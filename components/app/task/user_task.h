#ifndef __USER_TASK_H
#define __USER_TASK_H

#include "freertos/event_groups.h"
#define WIFI_CONNECT_BIT    BIT0

void start_mpu_task(void *p);
void start_detect_task(void *p);
void onenet_upload_task(void *pvParameters);

extern EventGroupHandle_t  wifi_ev;

#endif