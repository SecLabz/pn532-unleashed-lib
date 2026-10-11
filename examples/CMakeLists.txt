cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Reference board")
set(PICO_STACK_SIZE 4096)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
option(PICO_NO_PICOTOOL "Use the local RP2040 UF2 converter" ON)
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR "Set PICO_SDK_PATH to an installed Raspberry Pi Pico SDK")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(pn532_unleashed_examples C CXX ASM)
pico_sdk_init()

set(PNU_TRANSPORT SPI CACHE STRING "Application default: SPI or HSU")
set_property(CACHE PNU_TRANSPORT PROPERTY STRINGS SPI HSU)
if(NOT PNU_TRANSPORT STREQUAL "SPI" AND
   NOT PNU_TRANSPORT STREQUAL "HSU")
    message(FATAL_ERROR "PNU_TRANSPORT must be SPI or HSU")
endif()
if(PICO_NO_PICOTOOL)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." pn532_library)

pico_define_boot_stage2(examples_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(examples_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
foreach(example IN ITEMS
    nfca_reader slix_reader mifare_classic_emulator ntag_emulator
    slix_emulator ntag_custom_handler)
    add_executable(${example} ${example}/main.c)
    target_link_libraries(${example} PRIVATE pn532_unleashed::pn532_unleashed)
    target_compile_options(${example} PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(${example} PRIVATE
        PNU_EXAMPLE_TRANSPORT=NFC_TRANSPORT_${PNU_TRANSPORT}
        PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=1500
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=10000)
    pico_enable_stdio_usb(${example} 1)
    pico_enable_stdio_uart(${example} 0)
    pico_set_boot_stage2(${example} examples_boot2)
    # An example update must preserve the reference firmware's saved tags.
    target_link_options(${example} PRIVATE
        "LINKER:--script=${CMAKE_CURRENT_LIST_DIR}/storage.ld")
    set_property(TARGET ${example} APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/storage.ld")
    pico_add_extra_outputs(${example})
    if(PICO_NO_PICOTOOL)
        add_custom_command(TARGET ${example} POST_BUILD
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/../tools/bin2uf2.py"
                "$<TARGET_FILE_DIR:${example}>/${example}.bin"
                "$<TARGET_FILE_DIR:${example}>/${example}.uf2"
            BYPRODUCTS "${example}.uf2"
            VERBATIM)
    endif()
endforeach()
