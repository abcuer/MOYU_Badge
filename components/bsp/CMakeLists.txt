# bsp

file(GLOB_RECURSE SOURCES 
    "delay/*.c"
    "iic/*.c"
    "*.c"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS 
        "delay" 
        "iic" 
        "."

    REQUIRES 
        esp_driver_gpio  esp_rom 
)