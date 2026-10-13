cmake_minimum_required(VERSION 3.13)

# The embedding application owns Pico SDK initialization and its board.
if(NOT COMMAND pico_generate_pio_header)
    message(FATAL_ERROR "Initialize the Pico SDK before adding pn532-unleashed")
endif()
option(PNU_RF_TRACE "Bounded NFC-A bench trace, read only after RF stops" OFF)
option(PNU_HSU_TRACE "Cached HSU transport diagnostics" OFF)
option(PNU_ACTIVITY_LED "RP2040 Zero RGB LED for physical RF activity" OFF)
set(PNU_VERSION 0.1.0)
set(PNU_SYS_CLOCK_HZ 135600000 CACHE STRING
    "Required system clock: 135600000 or experimental 54240000 Hz")
set_property(CACHE PNU_SYS_CLOCK_HZ PROPERTY STRINGS 135600000 54240000)
if(NOT PNU_SYS_CLOCK_HZ STREQUAL "135600000" AND
   NOT PNU_SYS_CLOCK_HZ STREQUAL "54240000")
    message(FATAL_ERROR "PNU_SYS_CLOCK_HZ must be 135600000 or 54240000")
endif()

add_library(pn532_unleashed_lib STATIC
    src/core.c
    src/phy/nfca/sniff/sniff.c
    src/phy/sniff/capture.c
    src/phy/nfca/sniff/decode.c
    src/phy/nfcv/sniff/sniff.c
    src/phy/nfcv/sniff/decode.c
    src/board/board_config.c
    src/board/resources.c
    src/cards/nfca_select_validate.c
    src/cards/nfca_access.c
    src/cards/mifare/reader/mifare_access_reader.c
    src/cards/type2/reader/ntag_access_reader.c
    src/cards/image_copy.c
    src/cards/image.c
    src/cards/iso4/iso4_image.c
    src/cards/desfire/desfire_capture.c
    src/cards/desfire/desfire_commands.c
    src/cards/desfire/desfire_files.c
    src/cards/desfire/desfire_tag.c
    src/protocols/nfca/nfca_iso4.c
    src/emulation/emulation.c
    src/emulation/snapshot.c
    src/pn532/pn532_frame.c
    src/pn532/pn532_command.c
    src/pn532/pn532_transport.c
    src/pn532/pn532_spi.c
    src/pn532/pn532_hsu.c
    src/pn532/pn532_frontend.c
    src/board/tag_board.c
    src/phy/picc/picc_phy.c
    src/phy/picc/picc_tx_stream.c
    src/phy/nfca/picc/nfca_frame.c
    src/phy/nfca/picc/nfca_rx.c
    src/phy/nfca/picc/nfca_tx.c
    src/phy/nfcv/picc/nfcv_frame.c
    src/phy/nfcv/picc/nfcv_rx.c
    src/phy/nfcv/picc/nfcv_rx_decode.c
    src/phy/nfcv/picc/nfcv_tx.c
    src/common/crypto1.c
    src/protocols/nfca/nfca_activation.c
    src/cards/mifare/mifare_access.c
    src/cards/mifare/mifare_commands.c
    src/cards/mifare/mifare_product.c
    src/cards/mifare/tag/mifare_tag_session.c
    src/cards/mifare/tag/mifare_tag.c
    src/cards/type2/ntag_image.c
    src/cards/type2/ntag_config.c
    src/cards/type2/ntag_memory.c
    src/cards/type2/ntag_tag.c
    src/cards/type2/reader/ntag_reader.c
    src/cards/mifare/reader/mifare_write.c
    src/cards/mifare/reader/mifare_reader.c
    src/cards/mifare/reader/mifare_uid.c
    src/cards/mifare/reader/mifare_magic.c
    src/cards/mifare/reader/mifare_recovery.c
    src/cards/mifare/reader/mifare_darkside.c
    src/cards/slix/slix_tag.c
    src/cards/slix/slix_image.c
    src/protocols/nfcv/frame.c
    src/protocols/nfcv/nfcv_standard.c
    src/cards/nfcv/nfcv_memory_store.c
    src/phy/rf_capture_scratch.c
    src/protocols/nfcv/reader.c
    src/protocols/nfcv/reader_api.c
    src/protocols/nfcv/read_api.c
    src/protocols/nfca/reader.c
    src/protocols/nfca/reader_api.c
    src/protocols/nfca/read_api.c
    src/protocols/nfca/isodep_reader.c
    src/phy/nfca/pcd/nfca_recovery_clock.c
    src/phy/nfca/pcd/nfca_pcd.c
    src/phy/nfca/pcd/nfca_pcd_encode.c
    src/phy/nfca/pcd/nfca_pcd_decode.c
    src/phy/pcd/reader_subcarrier_level_rx.c
    src/phy/pcd/reader_tx_waveform.c
    src/phy/pcd/pn532_reader_sequence.c
    src/phy/nfcv/pcd/nfcv_pcd_encode.c
    src/phy/nfcv/pcd/nfcv_pcd_decode.c
    src/phy/nfcv/pcd/nfcv_pcd_rx.c
    src/phy/nfcv/pcd/nfcv_pcd_tx.c
)
foreach(PIO_FILE IN ITEMS
    src/phy/sniff/sniff_samples.pio
    src/phy/picc/picc_tx_stream.pio
    src/phy/nfca/picc/nfca_rx_levels.pio
    src/phy/nfcv/picc/nfcv_rx_sample.pio
    src/phy/nfcv/picc/nfcv_tx_stream.pio
    src/phy/pcd/reader_subcarrier_level_rx.pio
    src/phy/pcd/pn532_reader_sequence.pio)
    pico_generate_pio_header(pn532_unleashed_lib "${CMAKE_CURRENT_LIST_DIR}/${PIO_FILE}")
endforeach()
pico_generate_pio_header(pn532_unleashed_lib
    "${CMAKE_CURRENT_LIST_DIR}/src/pn532/pn532_spi.pio")
pico_generate_pio_header(pn532_unleashed_lib
    "${CMAKE_CURRENT_LIST_DIR}/src/pn532/pn532_hsu.pio")
if(PNU_RF_TRACE)
    target_sources(pn532_unleashed_lib PRIVATE
        src/pn532/pn532_frontend_trace.c
        src/phy/nfca/picc/nfca_trace.c
        src/phy/pcd/reader_rf_trace.c
        src/phy/nfca/sniff/trace.c)
    target_compile_definitions(pn532_unleashed_lib PUBLIC PNU_RF_TRACE=1)
endif()
if(PNU_HSU_TRACE)
    target_compile_definitions(pn532_unleashed_lib PUBLIC PNU_HSU_TRACE=1)
endif()
if(PNU_ACTIVITY_LED)
    target_sources(pn532_unleashed_lib PRIVATE src/board/activity_led.c)
    pico_generate_pio_header(pn532_unleashed_lib
        "${CMAKE_CURRENT_LIST_DIR}/src/board/activity_led.pio")
    target_compile_definitions(pn532_unleashed_lib PRIVATE PNU_ACTIVITY_LED=1)
endif()
target_include_directories(pn532_unleashed_lib
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
target_compile_features(pn532_unleashed_lib PUBLIC c_std_11)
target_compile_definitions(pn532_unleashed_lib PUBLIC
    PNU_REQUIRED_SYS_CLOCK_HZ=${PNU_SYS_CLOCK_HZ}u)
target_compile_options(pn532_unleashed_lib PRIVATE -Wall -Wextra -Werror)
get_target_property(CORE_SOURCES pn532_unleashed_lib SOURCES)
foreach(SOURCE IN LISTS CORE_SOURCES)
    if(SOURCE MATCHES "^src/.*\\.c$")
        set_property(SOURCE ${SOURCE} APPEND PROPERTY
            COMPILE_OPTIONS -Wshadow -Wundef -Wformat=2)
    endif()
endforeach()
target_link_libraries(pn532_unleashed_lib PUBLIC
    pico_stdlib pico_rand hardware_clocks hardware_pio hardware_pwm
    hardware_dma)

add_library(pn532_unleashed::pn532_unleashed ALIAS pn532_unleashed_lib)

set_target_properties(pn532_unleashed_lib PROPERTIES VERSION "${PNU_VERSION}")
