# modules

file(GLOB_RECURSE SOURCES 
    "bmp280/*.c"
    "inmp441/*.c"
    "key/*.c"
    "led/*.c"
    "max98357/*.c"
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
        "bmp280"
        "inmp441"
        "key"      
        "led"      
        "max98357"
        "oled"
        "oled/u8g2"
        "ws2812"
        "mpu6050"
        "imu"
        "max30102"
        "." 

    REQUIRES 
        bsp 
        esp_driver_gpio esp_timer esp_driver_i2c esp_driver_i2s espressif__led_strip freertos
)
