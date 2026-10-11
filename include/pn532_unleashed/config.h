#pragma once

#include <stdint.h>

typedef enum
{
    NFC_TRANSPORT_SPI = 0,
    NFC_TRANSPORT_HSU,
} nfc_transport_t;

/* RP2040 GPIO numbers (0..29), not physical connector pin numbers. SIGOUT
 * feeds RP2040 RX; SIGIN carries RP2040 TX. HSU TX/RX are RP2040 directions.
 * Only the selected transport's fields are used. Active pins must be distinct
 * and available to the application; GPIO ownership is not tracked. */
typedef struct
{
    nfc_transport_t transport;
    uint8_t sigout_gpio;
    uint8_t sigin_gpio;
    struct
    {
        uint8_t sck_gpio;
        uint8_t miso_gpio;
        uint8_t mosi_gpio;
        uint8_t cs_gpio;
        uint8_t irq_gpio;
    } spi;
    struct
    {
        uint8_t tx_gpio;
        uint8_t rx_gpio;
    } hsu;
} nfc_config_t;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Return SPI as the default transport and reference GPIOs for both SPI
     * and HSU. Modify transport and any pins before nfc_init(); the library
     * copies the configuration and retains no caller pointer. */
    nfc_config_t nfc_config_default(void);

    /* Borrowed static string: "SPI", "HSU", or "unselected" before the
     * first transport binding. No bus IO; caller must not modify/free it. */
    const char *nfc_transport_name(void);

#ifdef __cplusplus
}
#endif

/* The embedding application sets this clock before init. CMake publishes the
 * selected value to both the library and its consumer. The 54.24 MHz bench
 * profile has SPI reference-app checks; new embedding applications need
 * their own on-air validation. 135.6 MHz remains the default. */
#ifndef PNU_REQUIRED_SYS_CLOCK_HZ
#define PNU_REQUIRED_SYS_CLOCK_HZ 135600000u
#endif

#if PNU_REQUIRED_SYS_CLOCK_HZ != 135600000u &&                                 \
    PNU_REQUIRED_SYS_CLOCK_HZ != 54240000u
#error "Supported system clocks are 135600000 and 54240000 Hz"
#endif
