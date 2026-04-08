# app

file(GLOB_RECURSE SOURCES 
    "ai/*.c"
    "ap_wifi/*.c"
    "task/*.c"
    "iot/*.c"
    "mode/*.c" 
    "bloods/*c"
    "recorder/*.c"
    "radio/*.c"
    "settings/*.c"
    "ui/*c"
    "dev_status/*c"
    "*.c"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS 
        "ai"
        "task" 
        "ap_wifi" 
        "iot"
        "mode"
        "bloods" 
        "recorder"
        "radio"
        "settings"
        "ui"
        "dev_status"
        "."

    REQUIRES 
        bsp modules
        nvs_flash esp_wifi esp_event esp_http_server json esp_http_client
        spiffs mbedtls mqtt lwip esp_netif esp_timer app_update esp_https_ota
        espressif__esp_audio_codec espressif__esp_websocket_client
)

