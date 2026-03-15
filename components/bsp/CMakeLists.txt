# bsp

file(GLOB_RECURSE SOURCES 
    "delay/*.c"
    "iic/*.c"
    "*.c"
)

# 2. 注册组件
idf_component_register(
    SRCS ${SOURCES}          # 直接使用刚才收集到的变量
    INCLUDE_DIRS 
        "delay" 
        "iic" 
        "."
    REQUIRES 
        esp_driver_gpio  esp_rom 
)