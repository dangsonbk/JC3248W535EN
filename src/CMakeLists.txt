# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources 
    ${CMAKE_SOURCE_DIR}/src/*.c 
    ${CMAKE_SOURCE_DIR}/src/*.cpp 
    ${CMAKE_SOURCE_DIR}/src/*.S)

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "jpegdec" "mp3dec"
                       REQUIRES esp_http_client json esp_wifi nvs_flash spiffs fatfs sdmmc vfs driver)
