#include "phy/nfcv/picc/nfcv_tx.h"

#include "board/tag_board.h"
#include "phy/nfcv/picc/nfcv_frame.h"
#include "phy/picc/picc_phy.h"

#include "nfcv_tx_stream.pio.h"
#include "pico/stdlib.h"

#include <string.h>

#define NFCV_LOAD_MASK board_config.sigin_mask
#define NFCV_TX_SOF_PATTERN NFCV_UPLINK_SOF
#define NFCV_TX_EOF_PATTERN NFCV_UPLINK_EOF
#define NFCV_TX_PIO RF_TX_PIO
#define NFCV_TX_SM RF_TX_SM
#define NFCV_TX_SUBCARRIER_WORD 0x33333333u
static uint nfcv_tx_offset;
static bool nfcv_tx_program_loaded;
/*
 * RX timestamps the IRQ that delivers the final EOF sample. With the PIO
 * sampling phase at 96/fc, that sample is 32/fc before the reader EOF; the
 * timestamp also includes IRQ latency. Preserve the validated 325 us delay
 * and prepare the TX FIFO before waiting, outside the launch path.
 */
#define NFCV_TX_CAPTURE_TO_RESPONSE_US 325u

static void __not_in_flash_func(wait_response_time)(uint32_t eof_time_us)
{
    const uint32_t target = eof_time_us + NFCV_TX_CAPTURE_TO_RESPONSE_US;
    while ((int32_t)(time_us_32() - target) < 0)
    {
        tight_loop_contents();
    }
}

static void __not_in_flash_func(send_symbol)(bool subcarrier_on)
{
    pio_sm_put_blocking(
        NFCV_TX_PIO, NFCV_TX_SM, subcarrier_on ? NFCV_TX_SUBCARRIER_WORD : 0u);
}

static void __not_in_flash_func(send_pattern_msb)(uint8_t pattern)
{
    for (uint8_t i = 0u; i < 8u; i++)
    {
        send_symbol((pattern & 0x80u) != 0u);
        pattern <<= 1;
    }
}

static void __not_in_flash_func(send_data_byte)(uint8_t value)
{
    for (uint8_t bit = 0u; bit < 8u; bit++)
    {
        if (value & 1u)
        {
            send_symbol(false);
            send_symbol(true);
        }
        else
        {
            send_symbol(true);
            send_symbol(false);
        }
        value >>= 1;
    }
}

bool nfcv_tx_init(void)
{
    /* NFC-V owns this deterministic PIO streamer at every supported clock. */
    tag_phy_init_sio();
    if (!nfcv_tx_program_loaded)
    {
        if (!pio_can_add_program(NFCV_TX_PIO, &nfcv_tx_stream_program))
        {
            return false;
        }
        nfcv_tx_offset = pio_add_program(NFCV_TX_PIO, &nfcv_tx_stream_program);
        nfcv_tx_program_loaded = true;
    }
    pio_gpio_init(NFCV_TX_PIO, TAG_LOAD_GPIO);
    pio_sm_set_consecutive_pindirs(
        NFCV_TX_PIO, NFCV_TX_SM, TAG_LOAD_GPIO, 1u, true);
    pio_sm_config config =
        nfcv_tx_stream_program_get_default_config(nfcv_tx_offset);
    sm_config_set_out_pins(&config, TAG_LOAD_GPIO, 1u);
    sm_config_set_out_shift(&config, true, true, 32u);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&config, (float)NFC_SYS_CLOCKS_PER_FC / 2.0f);
    pio_sm_init(NFCV_TX_PIO, NFCV_TX_SM, nfcv_tx_offset, &config);
    /* The PIO output latch survives role changes independently of the SIO
     * latch. Force the inactive load level before exposing the pin to the
     * reader field, otherwise the first inventory can see a constant load. */
    pio_sm_set_pins_with_mask(NFCV_TX_PIO, NFCV_TX_SM, 0u, NFCV_LOAD_MASK);
    pio_sm_set_enabled(NFCV_TX_PIO, NFCV_TX_SM, false);
    return true;
}

bool __not_in_flash_func(nfcv_tx_send_response)(uint32_t eof_time_us,
                                                const uint8_t *response,
                                                uint16_t response_len)
{
    if (response_len > NFCV_RESPONSE_MAX_BYTES - NFCV_CRC_SIZE)
    {
        return false;
    }

    uint8_t frame[NFCV_RESPONSE_MAX_BYTES];
    memcpy(frame, response, response_len);
    nfcv_append_crc(frame, response_len);
    response_len = (uint16_t)(response_len + NFCV_CRC_SIZE);

    /* Clear sticky TXSTALL only while stopped. An enabled empty streamer
     * can assert it again before the first FIFO write, making completion
     * appear immediate and allowing RX to rearm during our own response. */
    pio_sm_set_enabled(NFCV_TX_PIO, NFCV_TX_SM, false);
    pio_sm_clear_fifos(NFCV_TX_PIO, NFCV_TX_SM);
    pio_sm_restart(NFCV_TX_PIO, NFCV_TX_SM);
    pio_sm_exec(NFCV_TX_PIO, NFCV_TX_SM, pio_encode_jmp(nfcv_tx_offset));
    const uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + NFCV_TX_SM);
    NFCV_TX_PIO->fdebug = stall_mask;
    /* SOF is exactly eight words and fits the joined TX FIFO. */
    send_pattern_msb(NFCV_TX_SOF_PATTERN);
    wait_response_time(eof_time_us);
    pio_sm_set_enabled(NFCV_TX_PIO, NFCV_TX_SM, true);

    for (uint16_t i = 0u; i < response_len; i++)
    {
        send_data_byte(frame[i]);
    }
    send_pattern_msb(NFCV_TX_EOF_PATTERN);
    while ((NFCV_TX_PIO->fdebug & stall_mask) == 0u)
    {
        tight_loop_contents();
    }
    pio_sm_set_enabled(NFCV_TX_PIO, NFCV_TX_SM, false);
    pio_sm_set_pins_with_mask(NFCV_TX_PIO, NFCV_TX_SM, 0u, NFCV_LOAD_MASK);
    return true;
}
