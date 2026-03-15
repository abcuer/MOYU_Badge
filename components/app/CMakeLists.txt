# app

file(GLOB_RECURSE SOURCES 
    "ap_wifi/*.c"
    "task/*.c"
    "iot/*.c"
    "*.c"
)

# 2. 注册组件
idf_component_register(
    SRCS ${SOURCES}          # 直接使用刚才收集到的变量
    INCLUDE_DIRS 
        "task" 
        "ap_wifi" 
        "iot" 
        "."
    REQUIRES 
        bsp modules nvs_flash esp_wifi esp_event esp_http_server json spiffs mbedtls mqtt
        # 补上了 可能需要的 库
)