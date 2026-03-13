#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ws2812.h"
#include "led.h"
#include "bmp280.h"
#include "ap_wifi.h"
#include "nvs_flash.h"

#define TAG "MAIN"

void wifi_state_changed(WIFI_STATE state) 
{
    if (state == WIFI_STATE_CONNECTED) {
        ESP_LOGI("MAIN", "WiFi Connected Successfully!");
    } else {
        ESP_LOGI("MAIN", "WiFi Disconnected.");
    }
}

void app_main(void)
{
    led_init();
    ws2812_init();
    bmp280_init();
    nvs_flash_init();
    ap_wifi_init(wifi_state_changed);
    ap_wifi_apcfg(true);

    while(1)
    {

        bmp280_read_data(&bmp280);

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
