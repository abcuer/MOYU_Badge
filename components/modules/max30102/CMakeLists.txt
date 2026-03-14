idf_component_register(SRCS "max30102.c" "blood.c"
                    INCLUDE_DIRS "." 
                    REQUIRES esp_driver_gpio espressif__led_strip delay iic)
