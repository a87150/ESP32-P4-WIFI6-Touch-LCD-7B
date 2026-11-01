set(
    BROOKESIA_TEST_BOOST_ROOT
    ""
    CACHE PATH "Optional Boost source root used when system Boost.Test is unavailable"
)

if(NOT TARGET brookesia::lib_utils)
    add_subdirectory(
        ${TEST_APP_DIR}/../../../utils/brookesia_lib_utils
        ${CMAKE_BINARY_DIR}/brookesia_lib_utils
    )
endif()
if(NOT TARGET brookesia::service_manager)
    add_subdirectory(
        ${TEST_APP_DIR}/../../../service/framework/brookesia_service_manager
        ${CMAKE_BINARY_DIR}/brookesia_service_manager
    )
endif()
if(NOT TARGET brookesia::service_helper)
    add_subdirectory(
        ${TEST_APP_DIR}/../../../service/framework/brookesia_service_helper
        ${CMAKE_BINARY_DIR}/brookesia_service_helper
    )
endif()
if(NOT TARGET brookesia::gui_interface)
    add_subdirectory(
        ${TEST_APP_DIR}/../../brookesia_gui_interface
        ${CMAKE_BINARY_DIR}/brookesia_gui_interface
    )
endif()

set(
    BROOKESIA_GUI_LVGL_TEST_APPS_LVGL_DIR
    "${TEST_APP_DIR}/../../../../lvgl"
    CACHE PATH "Optional LVGL source tree used to execute the GUI LVGL PC backend tests"
)
set(
    BROOKESIA_GUI_LVGL_TEST_APPS_LV_CONF_PATH
    "${TEST_APP_DIR}/../../../../../lv_conf.h"
    CACHE FILEPATH "LVGL configuration used by the GUI LVGL PC backend tests"
)
set(gui_lvgl_test_has_backend OFF)
if(EXISTS "${BROOKESIA_GUI_LVGL_TEST_APPS_LVGL_DIR}/CMakeLists.txt" AND
    EXISTS "${BROOKESIA_GUI_LVGL_TEST_APPS_LV_CONF_PATH}")
    set(LV_BUILD_CONF_PATH "${BROOKESIA_GUI_LVGL_TEST_APPS_LV_CONF_PATH}" CACHE FILEPATH "" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    find_package(SDL2 REQUIRED)
    add_subdirectory(
        ${BROOKESIA_GUI_LVGL_TEST_APPS_LVGL_DIR}
        ${CMAKE_BINARY_DIR}/lvgl
    )
    get_filename_component(gui_lvgl_test_lvgl_parent ${BROOKESIA_GUI_LVGL_TEST_APPS_LVGL_DIR} DIRECTORY)
    target_include_directories(lvgl PUBLIC ${gui_lvgl_test_lvgl_parent})
    if(SDL2_INCLUDE_DIRS)
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
    endif()
    if(TARGET SDL2::SDL2)
        target_link_libraries(lvgl PUBLIC SDL2::SDL2)
    else()
        target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
    endif()

    set(BROOKESIA_HAL_LINUX_MEDIA_BACKEND stub CACHE STRING "" FORCE)
    set(BROOKESIA_HAL_LINUX_VIDEO_BACKEND stub CACHE STRING "" FORCE)
    set(BROOKESIA_HAL_LINUX_WIFI_BACKEND stub CACHE STRING "" FORCE)
    set(BROOKESIA_HAL_LINUX_BLE_BACKEND stub CACHE STRING "" FORCE)
    set(BROOKESIA_HAL_LINUX_DISPLAY_BACKEND stub CACHE STRING "" FORCE)
    if(NOT TARGET brookesia::gui_lvgl)
        add_subdirectory(
            ${TEST_APP_DIR}/..
            ${CMAKE_BINARY_DIR}/brookesia_gui_lvgl
        )
    endif()
    set(gui_lvgl_test_has_backend ON)
else()
    message(
        STATUS
        "LVGL source/config not found; PC test will compile public headers and skip backend execution"
    )
endif()

file(GLOB TEST_APP_SRCS_CPP ${TEST_APP_MAIN_DIR}/*.cpp)
add_executable(test_brookesia_gui_lvgl ${TEST_APP_SRCS_CPP})

find_package(Boost COMPONENTS unit_test_framework QUIET)
if(Boost_unit_test_framework_FOUND)
    target_link_libraries(test_brookesia_gui_lvgl PRIVATE Boost::unit_test_framework)
    target_compile_definitions(test_brookesia_gui_lvgl PRIVATE BOOST_TEST_DYN_LINK)
else()
    if("${BROOKESIA_TEST_BOOST_ROOT}" STREQUAL "")
        message(FATAL_ERROR "Boost.Test not found. Set BROOKESIA_TEST_BOOST_ROOT to an esp-boost/src path.")
    endif()
    target_include_directories(test_brookesia_gui_lvgl PRIVATE ${BROOKESIA_TEST_BOOST_ROOT})
    target_compile_definitions(test_brookesia_gui_lvgl PRIVATE
        BROOKESIA_LIB_UTILS_TEST_ADAPTER_USE_INCLUDED_BOOST_TEST=1
    )
endif()

target_compile_features(test_brookesia_gui_lvgl PRIVATE cxx_std_23)
target_include_directories(
    test_brookesia_gui_lvgl
    PRIVATE
        ${TEST_APP_MAIN_DIR}
        ${TEST_APP_DIR}/../include
)
if(gui_lvgl_test_has_backend)
    target_link_libraries(test_brookesia_gui_lvgl PRIVATE brookesia::gui_lvgl)
    target_compile_definitions(test_brookesia_gui_lvgl PRIVATE BROOKESIA_GUI_LVGL_TEST_APPS_HAS_BACKEND=1)
else()
    target_link_libraries(test_brookesia_gui_lvgl PRIVATE brookesia::gui_interface)
    target_compile_definitions(test_brookesia_gui_lvgl PRIVATE BROOKESIA_GUI_LVGL_TEST_APPS_HAS_BACKEND=0)
endif()

enable_testing()
add_test(NAME test_brookesia_gui_lvgl COMMAND test_brookesia_gui_lvgl)
