set(COMPONENT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COMPONENT_SRC_DIR ${COMPONENT_DIR}/src)
set(COMPONENT_INCLUDE_DIRS ${COMPONENT_DIR}/include)
set(COMPONENT_PRIVATE_INCLUDE_DIRS ${COMPONENT_SRC_DIR})
# The hardware JPEG codec decodes frames into PSRAM; LVGL image descriptors let
# the panel draw those buffers without copying them into a GUI-side texture.
# MP4 files are decoded by the software H.264 decoder from esp_h264 (the
# ESP32-P4 only has a hardware H.264 encoder).
set(COMPONENT_REQUIRES "esp_driver_jpeg" "lvgl" "heap" "esp_h264")
set(COMPONENT_SRCS_C "")
set(COMPONENT_SRCS_CPP "")
file(GLOB_RECURSE APP_VIDEO_SRCS_C ${COMPONENT_SRC_DIR}/*.c)
file(GLOB_RECURSE APP_VIDEO_SRCS_CPP ${COMPONENT_SRC_DIR}/*.cpp)
list(APPEND COMPONENT_SRCS_C ${APP_VIDEO_SRCS_C})
list(APPEND COMPONENT_SRCS_CPP ${APP_VIDEO_SRCS_CPP})

if(ESP_PLATFORM)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/esp_platform.cmake)
endif()

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_source_files_properties(${COMPONENT_SRCS_C}
        PROPERTIES
            COMPILE_FLAGS "${COMPONENT_SRCS_C_COMPILE_FLAGS}"
    )
    set_source_files_properties(${COMPONENT_SRCS_CPP}
        PROPERTIES
            COMPILE_FLAGS "${COMPONENT_SRCS_CPP_COMPILE_FLAGS}"
    )
endif()
