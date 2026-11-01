cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

set(TEST_APP_DIR ${CMAKE_CURRENT_LIST_DIR})
set(TEST_APP_MAIN_DIR ${TEST_APP_DIR}/main)

if(DEFINED ENV{IDF_PATH} AND NOT BROOKESIA_SERVICE_PICODET_TEST_APPS_FORCE_PC)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/esp_platform.cmake)
    project(test_brookesia_service_picodet)
else()
    project(test_brookesia_service_picodet LANGUAGES CXX)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/pc_platform.cmake)
endif()

# esp-dl contains large C++ translation units. Keep its compile edges within
# the shared pool to avoid exhausting the CI runner memory.
brookesia_compile_tuning_apply_current_project(
    EXTERNAL_COMPONENTS espressif__esp-dl
)

if(COMMAND idf_build_get_property AND TARGET __idf_espressif__esp-dl)
    # esp-dl 3.3.6 uses a C++ anonymous typedef with a default member
    # initializer. Suppress only this external warning; the negative form is
    # accepted by toolchains that do not expose the warning name.
    target_compile_options(
        __idf_espressif__esp-dl
        PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-non-c-typedef-for-linkage>
    )
endif()
