#include "phy/picc/picc_tx_stream.h"

#include "board/tag_board.h"

#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "picc_tx_stream.pio.h"
#include "pico/stdlib.h"

#define TAG_TX_STREAM_PIO RF_TX_PIO
#define TAG_TX_STREAM_SM RF_TX_SM
#define TAG_TX_STREAM_LOAD_MASK board_config.sigin_mask
#define TAG_TX_STREAM_CLKDIV ((float)NFC_SYS_CLOCKS_PER_FC / 2.0f)
#define TAG_TX_STREAM_PULL_BITS 16u

static uint program_offset;
static bool program_loaded;

#ifdef PNU_RF_TRACE
bool __not_in_flash_func(tag_tx_stream_underrun)(void)
{
    const uint32_t mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + TAG_TX_STREAM_SM);
    return (TAG_TX_STREAM_PIO->fdebug & mask) != 0u;
}
#endif

void __not_in_flash_func(tag_tx_stream_prepare)(void)
{
    pio_sm_set_enabled(TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM, false);
    pio_sm_clear_fifos(TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM);
    pio_sm_restart(TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM);
    pio_sm_exec(
        TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM, pio_encode_jmp(program_offset));
    TAG_TX_STREAM_PIO->fdebug = 1u
                                << (PIO_FDEBUG_TXSTALL_LSB + TAG_TX_STREAM_SM);
}

bool tag_tx_stream_init(void)
{
    if (!program_loaded)
    {
        if (!pio_can_add_program(TAG_TX_STREAM_PIO, &tag_tx_stream_program))
        {
            return false;
        }
        program_offset =
            pio_add_program(TAG_TX_STREAM_PIO, &tag_tx_stream_program);
        program_loaded = true;
    }
    pio_gpio_init(TAG_TX_STREAM_PIO, TAG_LOAD_GPIO);
    pio_sm_set_consecutive_pindirs(
        TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM, TAG_LOAD_GPIO, 1u, true);

    pio_sm_config config =
        tag_tx_stream_program_get_default_config(program_offset);
    sm_config_set_out_pins(&config, TAG_LOAD_GPIO, 1u);
    sm_config_set_out_shift(&config, true, true, TAG_TX_STREAM_PULL_BITS);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&config, TAG_TX_STREAM_CLKDIV);
    pio_sm_init(TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM, program_offset, &config);
    pio_sm_set_pins_with_mask(
        TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM, 0u, TAG_TX_STREAM_LOAD_MASK);
    tag_tx_stream_prepare();
    return true;
}

void __not_in_flash_func(tag_tx_stream_put)(uint32_t pattern)
{
    pio_sm_put_blocking(TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM, pattern);
}

void __not_in_flash_func(tag_tx_stream_start)(void)
{
    /* Put the physical edge before bookkeeping, with one atomic write. */
    hw_set_bits(&TAG_TX_STREAM_PIO->ctrl, 1u << TAG_TX_STREAM_SM);
}

void __not_in_flash_func(tag_tx_stream_finish)(void)
{
    const uint32_t stall_mask = 1u
                                << (PIO_FDEBUG_TXSTALL_LSB + TAG_TX_STREAM_SM);
    while (!pio_sm_is_tx_fifo_empty(TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM))
    {
        tight_loop_contents();
    }
    /* An earlier underrun leaves a sticky TXSTALL. Wait for the final OSR
     * word to drain before allowing RX to rearm. */
    TAG_TX_STREAM_PIO->fdebug = stall_mask;
    while ((TAG_TX_STREAM_PIO->fdebug & stall_mask) == 0u)
    {
        tight_loop_contents();
    }
    tag_tx_stream_stop();
}

void __not_in_flash_func(tag_tx_stream_stop)(void)
{
    pio_sm_set_enabled(TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM, false);
    /* GPIO remains owned by PIO after the SM stops. Clearing only the SIO
     * latch cannot release the tag load. Force the PIO output latch inactive
     * between responses and before a protocol switch. */
    pio_sm_set_pins_with_mask(
        TAG_TX_STREAM_PIO, TAG_TX_STREAM_SM, 0u, TAG_TX_STREAM_LOAD_MASK);
    sio_hw->gpio_clr = TAG_TX_STREAM_LOAD_MASK;
}
