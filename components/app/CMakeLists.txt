# app

file(GLOB_RECURSE SOURCES 
    "ap_wifi/*.c"
    "task/*.c"
    "iot/*.c"
    "mode/*.c" 
    "bloods/*c"
    "ui/*c"
    "dev_status/*c"
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
        "dev_status"
        "."

    REQUIRES 
        bsp modules audio
        nvs_flash esp_wifi esp_event esp_http_server json esp_http_client
        spiffs mbedtls mqtt lwip esp_netif esp_timer
)
