# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

cmake_minimum_required(VERSION 3.16)

# 根目录
# 必须在 include 之前设置，注意路径层级
set(EXTRA_COMPONENT_DIRS 
    "components/app"
    "components/bsp"
    "components/modules"
    "components/audio"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_mqtt_system)
