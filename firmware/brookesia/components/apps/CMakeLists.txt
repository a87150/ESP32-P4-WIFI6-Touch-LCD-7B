set(APPS_DIR ./)
file(GLOB_RECURSE APPS_C_SRCS ${APPS_DIR}/*.c)
file(GLOB_RECURSE APPS_CPP_SRCS ${APPS_DIR}/*.cpp)

idf_component_register(
    SRCS ${APPS_C_SRCS} ${APPS_CPP_SRCS}
    INCLUDE_DIRS ${APPS_DIR}
    REQUIRES lvgl__lvgl esp_event esp_wifi nvs_flash esp_driver_jpeg esp_mm esp-brookesia product_audio esp32_p4_wifi6_touch_lcd_7b esp_video pedestrian_detect human_face_detect espressif__esp_lcd_touch_gt911)

target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
    $<$<COMPILE_LANGUAGE:C>:
        -Wno-format
        -Wno-incompatible-pointer-types
        -Wno-int-conversion
    >
    $<$<COMPILE_LANGUAGE:CXX>:
        -Wno-format
        -Wno-conversion-null
        -Wno-attributes
        -Wno-write-strings
    >
    -DLV_LVGL_H_INCLUDE_SIMPLE
)

# LVGL v8 compatibility shim injected into every source (types/aliases live in
# lv_api_map_v8.h; this header covers the remaining v8 identifiers)
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
    "--include=${CMAKE_CURRENT_LIST_DIR}/lvgl8_compat.h"
)
