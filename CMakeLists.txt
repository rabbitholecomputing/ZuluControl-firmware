# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico_w)
endif()

include(pico_sdk_import.cmake)

project(zulucontrol C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(zulucontrol)

target_sources(zulucontrol PRIVATE
    src/main.cpp
    src/url_decode.cpp
    src/ZuluControlI2CClient.cpp
    src/fw_upgrade.cpp
)

#pico_enable_stdio_uart(zulucontrol ENABLED)
pico_enable_stdio_usb(zulucontrol ENABLED)

add_dependencies(zulucontrol pico_i2c_slave pico_multicore)

pico_add_extra_outputs(zulucontrol)

target_compile_options(zulucontrol PRIVATE -Wall)

target_link_options(zulucontrol PRIVATE "-Wl,--print-memory-usage")

target_include_directories(zulucontrol PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        )

set(RESOURCE_FILES
    ${CMAKE_CURRENT_LIST_DIR}/resources/control.html
    ${CMAKE_CURRENT_LIST_DIR}/resources/fw_upgrade.html
    ${CMAKE_CURRENT_LIST_DIR}/resources/control.js
    ${CMAKE_CURRENT_LIST_DIR}/resources/version.js
    ${CMAKE_CURRENT_LIST_DIR}/resources/style.css
)

foreach(f IN LISTS RESOURCE_FILES)
    get_filename_component(name ${f} NAME)
    string(REPLACE "." "_" var ${name})
    string(TOUPPER ${var} var)
    file(READ ${f} ${var}_CONTENT)
endforeach()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RESOURCE_FILES})
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/src/index_html.in
    ${CMAKE_CURRENT_LIST_DIR}/src/index_html.h
    @ONLY
)

# Try to get git hash for version number
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE FW_GITHASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE GIT_RESULT
    ERROR_QUIET
)

if(GIT_RESULT EQUAL 0)
    add_compile_definitions(FW_GITHASH="${FW_GITHASH}")
endif()


target_compile_definitions(zulucontrol PRIVATE
        WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
        WIFI_SSID=\"${WIFI_SSID}\"
        )

target_link_libraries(zulucontrol
        pico_i2c_slave
        pico_multicore
        pico_stdlib
        pico_lwip_http
        boot_uf2_headers
        pico_cyw43_arch_lwip_threadsafe_background
)
