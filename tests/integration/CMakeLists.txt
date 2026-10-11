cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Reference board")
set(PICO_STACK_SIZE 4096)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(library_consumer C CXX ASM)
pico_sdk_init()

set(PNU_TRANSPORT SPI CACHE STRING "Application default: SPI or HSU")
set_property(CACHE PNU_TRANSPORT PROPERTY STRINGS SPI HSU)
if(NOT PNU_TRANSPORT STREQUAL "SPI" AND
   NOT PNU_TRANSPORT STREQUAL "HSU")
    message(FATAL_ERROR "PNU_TRANSPORT must be SPI or HSU")
endif()

# This consumer receives only public library headers and dependencies.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.." pn532_library)
add_executable(library_consumer library_consumer.cpp)
target_link_libraries(library_consumer PRIVATE
    pn532_unleashed::pn532_unleashed)
target_compile_options(library_consumer PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(library_consumer PRIVATE
    PNU_CONSUMER_TRANSPORT=NFC_TRANSPORT_${PNU_TRANSPORT})
pico_enable_stdio_usb(library_consumer 0)
pico_enable_stdio_uart(library_consumer 0)
pico_add_extra_outputs(library_consumer)

add_executable(diagnostics_consumer diagnostics_consumer.cpp)
target_link_libraries(diagnostics_consumer PRIVATE pn532_unleashed::pn532_unleashed)
target_compile_options(diagnostics_consumer PRIVATE -Wall -Wextra -Werror)
pico_enable_stdio_usb(diagnostics_consumer 0)
pico_enable_stdio_uart(diagnostics_consumer 0)
pico_add_extra_outputs(diagnostics_consumer)
