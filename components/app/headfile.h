#ifndef __HEADFILE_H
#define __HEADFILE_H

#include "math.h"
#include "stdio.h"
#include "string.h"

#include "time.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_netif_sntp.h"
#include "esp_http_client.h"
#include "esp_sleep.h"

#include "ws2812.h"
#include "led.h"
#include "bmp280.h"
#include "mpu6050.h"
#include "imu.h"
#include "oled.h"
#include "max30102.h"
#include "key.h"

#include "nvs_flash.h"
#include "wifi_manager.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"

#include "ap_wifi.h"
#include "mode.h"
#include "bloods.h"
#include "status.h"
#include "ui.h"
#include "user_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#endif