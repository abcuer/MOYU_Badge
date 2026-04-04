idf_component_register(
    SRCS "bsp_iic.c"
    INCLUDE_DIRS "." 
    REQUIRES driver esp_rom delay
)