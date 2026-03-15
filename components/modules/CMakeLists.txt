# modules

file(GLOB_RECURSE SOURCES 
    "bmp280/*.c"
    "button/*.c"
    "imu/*.c"
    "led/*.c"
    "max30102/*.c"
    "mpu6050/*.c"
    "oled/*.c"
    "oled/u8g2/*.c"
    "ws2812/*.c"
    "*.c"
)

# 2. 注册组件
idf_component_register(
    SRCS ${SOURCES}          # 直接使用刚才收集到的变量
    INCLUDE_DIRS 
        "bmp280" 
        "button" 
        "imu"
        "led"
        "max30102"
        "mpu6050"
        "oled"
        "oled/u8g2"
        "ws2812"
        "."
    REQUIRES 
        bsp esp_driver_gpio esp_timer esp_driver_i2c espressif__led_strip
)