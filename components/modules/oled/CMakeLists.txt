# 1. 查找 u8g2 文件夹下所有的 .c 文件
file(GLOB_RECURSE U8G2_SRCS "u8g2/*.c")

# 2. 注册组件
idf_component_register(
    SRCS "oled.c" "oled_font.c"
         ${U8G2_SRCS}             # 将自动搜寻到的 u8g2 源码加入
    INCLUDE_DIRS "." 
                 "u8g2"           # 必须包含 u8g2 文件夹，否则找不到 u8g2.h
    REQUIRES esp_driver_gpio delay iic
)