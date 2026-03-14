idf_component_register(
    SRCS "bmp280.c"
    INCLUDE_DIRS "."
    REQUIRES iic esp_rom
)