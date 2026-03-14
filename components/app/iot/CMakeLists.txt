idf_component_register(SRCS "onenet_dm.c" "onenet_mqtt.c" "onenet_token.c"
                    INCLUDE_DIRS "."
                    REQUIRES bmp280 esp_driver_gpio espressif__led_strip esp_http_client mqtt json)