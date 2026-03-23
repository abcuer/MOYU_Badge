# modules

file(GLOB_RECURSE SOURCES 
    "key/*.c"
    "led/*.c"
    "oled/*.c" 
    "oled/u8g2/*c"
    "ws2812/*c"
    "mpu6050/*c"
    "imu/*c"
    "max30102/*c"
    "*.c"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS 
        "key"      
        "led"      
        "oled"
        "oled/u8g2"
        "ws2812"
        "bmp280" 
        "mpu6050"
        "imu"
        "max30102"
        "." 

    REQUIRES 
        app bsp 
        esp_driver_gpio esp_timer esp_driver_i2c espressif__led_strip freertos
)