file(GLOB_RECURSE SOURCES
    "*.c"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_http_client
        esp_timer
        esp_netif
        esp_wifi
        esp_driver_gpio
        esp_driver_i2s
        freertos
        espressif__esp_audio_codec
)
