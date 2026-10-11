#include "board/board_config.h"

#include <string.h>

#define DEFAULT_PINS                                                           \
    {                                                                          \
        .transport = NFC_TRANSPORT_SPI, .sigout_gpio = 28u, .sigin_gpio = 29u, \
        .spi = {8u, 9u, 10u, 11u, 14u}, .hsu = {11u, 10u},                     \
    }

board_config_t board_config = {.pins = DEFAULT_PINS, .sigin_mask = 1u << 29u};

nfc_config_t nfc_config_default(void)
{
    return (nfc_config_t)DEFAULT_PINS;
}

/* Keep validation and retry comparison limited to the selected transport. */
static size_t active_pins(const nfc_config_t *config, uint8_t pins[7])
{
    pins[0] = config->sigout_gpio;
    pins[1] = config->sigin_gpio;
    if (config->transport == NFC_TRANSPORT_HSU)
    {
        pins[2] = config->hsu.tx_gpio;
        pins[3] = config->hsu.rx_gpio;
        return 4u;
    }
    pins[2] = config->spi.sck_gpio;
    pins[3] = config->spi.miso_gpio;
    pins[4] = config->spi.mosi_gpio;
    pins[5] = config->spi.cs_gpio;
    pins[6] = config->spi.irq_gpio;
    return 7u;
}

bool board_config_validate(const nfc_config_t *config)
{
    if (config == NULL || (config->transport != NFC_TRANSPORT_SPI &&
                           config->transport != NFC_TRANSPORT_HSU))
    {
        return false;
    }
    uint8_t pins[7];
    const size_t count = active_pins(config, pins);
    uint32_t used = 0u;
#if PNU_ACTIVITY_LED
    used = 1u << ACTIVITY_LED_GPIO;
#endif
    for (size_t i = 0u; i < count; i++)
    {
        if (pins[i] >= 30u)
        {
            return false;
        }
        const uint32_t mask = 1u << pins[i];
        if ((used & mask) != 0u)
        {
            return false;
        }
        used |= mask;
    }
    return true;
}

bool board_config_matches(const nfc_config_t *config)
{
    if (config == NULL || config->transport != board_config.pins.transport)
    {
        return false;
    }
    uint8_t selected[7], requested[7];
    const size_t count = active_pins(&board_config.pins, selected);
    active_pins(config, requested);
    return memcmp(selected, requested, count) == 0;
}

void board_config_apply(const nfc_config_t *config)
{
    board_config.pins = *config;
    board_config.sigin_mask = 1u << config->sigin_gpio;
}
