# app

file(GLOB_RECURSE SOURCES 
    "ap_wifi/*.c"
    "task/*.c"
    "iot/*.c"
    "mode/*.c" 
    "bloods/*c"
    "ui/*c"
    "*.c"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS 
        "task" 
        "ap_wifi" 
        "iot"
        "mode"
        "bloods" 
        "ui"
        "."

    REQUIRES 
        bsp modules nvs_flash esp_wifi esp_event esp_http_server json spiffs mbedtls mqtt freertos lwip esp_netif esp_timer
)