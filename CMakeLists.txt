cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr-dw3000-decadriver-master
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(dwm3001_test)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr-dw3000-decadriver-master/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr-dw3000-decadriver-master/dwt_uwb_driver
)

target_sources(app PRIVATE src/main.c)
