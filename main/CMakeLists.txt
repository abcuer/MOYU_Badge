# main

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bsp modules app audio
)
spiffs_create_partition_image(html "${PROJECT_DIR}/components/app/ap_wifi/html" FLASH_IN_PROJECT)
