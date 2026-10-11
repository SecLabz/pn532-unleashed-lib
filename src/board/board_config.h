#pragma once

#include "phy/timing.h"
#include "pn532_unleashed/config.h"

#include "hardware/pio.h"

#include <stdbool.h>

/* Set once, before frontend initialization. RF reads only RAM-resident state;
 * no configuration lookup or pin-mask calculation is needed during TX. */
typedef struct
{
    nfc_config_t pins;
    uint32_t sigin_mask;
} board_config_t;

extern board_config_t board_config;
bool board_config_validate(const nfc_config_t *config);
bool board_config_matches(const nfc_config_t *config);
void board_config_apply(const nfc_config_t *config);

#define TAG_RX_GPIO board_config.pins.sigout_gpio
#define TAG_LOAD_GPIO board_config.pins.sigin_gpio
#define TAG_RX_PULL_UP 0
#define TAG_RX_PULL_DOWN 0
#define NFCA_TAG_RX_INPUT_INVERTED 1
#define NFCA_TIMER_SLICE 0u
/* Reader and tag roles reuse the same RF resources; modes never overlap. */
#define RF_RX_PIO pio0
#define RF_RX_SM 0u
#define RF_RX_IRQ PIO0_IRQ_0
#define RF_TX_PIO pio0
#define RF_TX_SM 1u
#define NFCA_RX_PIO RF_RX_PIO
#define NFCA_RX_SM RF_RX_SM
#define NFCA_RX_IRQ RF_RX_IRQ

#define PN532_CONTROL_PIO pio1
#define PN532_CONTROL_SM 0u
#define PN532_SPI_PIO PN532_CONTROL_PIO
#define PN532_SPI_SM PN532_CONTROL_SM
#define PN532_SPI_SCK_GPIO board_config.pins.spi.sck_gpio
#define PN532_SPI_MISO_GPIO board_config.pins.spi.miso_gpio
#define PN532_SPI_MOSI_GPIO board_config.pins.spi.mosi_gpio
#define PN532_SPI_CS_GPIO board_config.pins.spi.cs_gpio
#define PN532_SPI_IRQ_GPIO board_config.pins.spi.irq_gpio

#define PN532_HSU_PIO PN532_CONTROL_PIO
#define PN532_HSU_TX_SM PN532_CONTROL_SM
#define PN532_HSU_RX_SM 1u
#define PN532_HSU_TX_GPIO board_config.pins.hsu.tx_gpio
#define PN532_HSU_RX_GPIO board_config.pins.hsu.rx_gpio
#define PN532_HSU_BAUD 115200u

#ifndef PNU_ACTIVITY_LED
#define PNU_ACTIVITY_LED 0
#endif
#if PNU_ACTIVITY_LED
#define ACTIVITY_LED_GPIO 16u
#define ACTIVITY_LED_PIO pio1
#define ACTIVITY_LED_SM 2u
#endif
