# main

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES app modules bsp  
    # 必须在这里显式要求这些组件，main 才能看到它们的头文件
)
spiffs_create_partition_image(html "${PROJECT_DIR}/components/app/ap_wifi/html" FLASH_IN_PROJECT)