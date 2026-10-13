#include "phy/pcd/pn532_reader_sequence.h"

#include "board/reader_board.h"
#include "phy/nfca/pcd/nfca_recovery_clock.h"
#include "phy/pcd/reader_rf_trace.h"
#include "phy/rf_capture_scratch.h"
#include "pn532/pn532_frontend.h"

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "pn532_reader_sequence.pio.h"

#include <stddef.h>

#define TX_PIO RF_TX_PIO
#define TX_SM RF_TX_SM
#define SETUP_TIMEOUT_US 1000u
/* At 13.56 MHz, one carrier period is exactly 25/339 us. */
#define CARRIER_PERIOD_US_NUMERATOR 25u
#define CARRIER_PERIOD_US_DENOMINATOR 339u

static int dma_channel = -1;
static uint program_offset;
static bool loaded;

static bool __not_in_flash_func(expired)(uint32_t deadline)
{
    return (int32_t)(time_us_32() - deadline) >= 0;
}

static void __not_in_flash_func(stop_sequence)(void)
{
    pio_sm_set_enabled(TX_PIO, TX_SM, false);
    dma_channel_abort((uint)dma_channel);
    pio_sm_clear_fifos(TX_PIO, TX_SM);
    /* The SDK mask helper lives in flash. A configured single-pin SET keeps
     * the inactive carrier level and the TX-to-RX handoff entirely in RAM. */
    pio_sm_exec(TX_PIO, TX_SM, pio_encode_set(pio_pins, 1u));
    rf_capture_scratch_release(RF_CAPTURE_SCRATCH_READER_TX);
}

void pn532_reader_sequence_release(void)
{
    if (!loaded)
    {
        return;
    }
    pio_sm_set_enabled(TX_PIO, TX_SM, false);
    dma_channel_abort((uint)dma_channel);
    pio_sm_clear_fifos(TX_PIO, TX_SM);
    pio_remove_program(TX_PIO, &pn532_reader_sequence_program, program_offset);
    loaded = false;
}

bool pn532_reader_sequence_init(void)
{
    if (dma_channel < 0)
    {
        dma_channel = dma_claim_unused_channel(false);
        if (dma_channel < 0)
        {
            return false;
        }
    }
    if (!loaded)
    {
        if (!pio_can_add_program(TX_PIO, &pn532_reader_sequence_program))
        {
            return false;
        }
        program_offset =
            pio_add_program(TX_PIO, &pn532_reader_sequence_program);
        loaded = true;
    }
    pio_gpio_init(TX_PIO, TAG_LOAD_GPIO);
    pio_sm_set_consecutive_pindirs(TX_PIO, TX_SM, TAG_LOAD_GPIO, 1u, true);
    pio_sm_config config =
        pn532_reader_sequence_program_get_default_config(program_offset);
    sm_config_set_out_pins(&config, TAG_LOAD_GPIO, 1u);
    sm_config_set_set_pins(&config, TAG_LOAD_GPIO, 1u);
    /* One FIFO word per physical slot; DMA uses the existing RF scratch. */
    sm_config_set_out_shift(&config, true, true, 1u);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    pio_sm_init(TX_PIO, TX_SM, program_offset, &config);
    pio_sm_set_pins_with_mask(
        TX_PIO, TX_SM, 1u << TAG_LOAD_GPIO, 1u << TAG_LOAD_GPIO);
    return true;
}

bool __not_in_flash_func(pn532_reader_sequence_play_at)(
    const reader_tx_waveform_t *waveform,
    uint16_t tick_fc,
    uint32_t deadline_ticks)
{
    if (!loaded || !nfc_rf_ready())
    {
        READER_RF_TRACE_REASON(READER_TRACE_NOT_READY);
        return false;
    }
    rf_capture_scratch_t *scratch =
        rf_capture_scratch_acquire(RF_CAPTURE_SCRATCH_READER_TX);
    if (scratch == NULL)
    {
        READER_RF_TRACE_REASON(READER_TRACE_TX_SCRATCH_BUSY);
        return false;
    }
    uint32_t *slots = scratch->reader_tx_slot_words;
    uint16_t count;
    if (!reader_tx_waveform_slots(
            waveform, tick_fc, slots, RF_CAPTURE_READER_TX_SLOT_WORDS, &count))
    {
        rf_capture_scratch_release(RF_CAPTURE_SCRATCH_READER_TX);
        READER_RF_TRACE_REASON(READER_TRACE_FRAME_INVALID);
        return false;
    }
    pio_sm_set_enabled(TX_PIO, TX_SM, false);
    pio_sm_clear_fifos(TX_PIO, TX_SM);
    pio_sm_restart(TX_PIO, TX_SM);
    pio_sm_exec(TX_PIO, TX_SM, pio_encode_jmp(program_offset));
    pio_sm_set_clkdiv(TX_PIO, TX_SM, (float)(tick_fc * NFC_SYS_CLOCKS_PER_FC));
    pio_sm_clkdiv_restart(TX_PIO, TX_SM);
    dma_channel_config config =
        dma_channel_get_default_config((uint)dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, pio_get_dreq(TX_PIO, TX_SM, true));
    dma_channel_configure((uint)dma_channel,
                          &config,
                          &TX_PIO->txf[TX_SM],
                          slots,
                          count + 1u,
                          true);
    uint32_t timeout = time_us_32() + SETUP_TIMEOUT_US;
    while (pio_sm_is_tx_fifo_empty(TX_PIO, TX_SM))
    {
        if (expired(timeout))
        {
            stop_sequence();
            READER_RF_TRACE_REASON(READER_TRACE_FIFO_TIMEOUT);
            return false;
        }
        tight_loop_contents();
    }
    /* Physical duration in microseconds, rounded up, plus DMA/PIO margin. */
    const uint32_t duration_us =
        ((uint32_t)(count + 1u) * tick_fc * CARRIER_PERIOD_US_NUMERATOR +
         CARRIER_PERIOD_US_DENOMINATOR - 1u) /
            CARRIER_PERIOD_US_DENOMINATOR +
        SETUP_TIMEOUT_US;
    const uint32_t stall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + TX_SM);
    TX_PIO->fdebug = stall_mask;
    if (deadline_ticks != 0u && !nfca_recovery_clock_wait(deadline_ticks))
    {
        stop_sequence();
        return false;
    }
    timeout = time_us_32() + duration_us;
    /* The divider free-runs while the SM is disabled. Restart it atomically
     * with TX enable so the first slot cannot drift by up to 32/fc relative
     * to the scheduled PRNG tick as preparation time changes.
     */
    if (deadline_ticks != 0u)
    {
        pio_enable_sm_mask_in_sync(TX_PIO, 1u << TX_SM);
    }
    else
    {
        pio_sm_set_enabled(TX_PIO, TX_SM, true);
    }
    while (dma_channel_is_busy((uint)dma_channel))
    {
        if (expired(timeout))
        {
            stop_sequence();
            READER_RF_TRACE_REASON(READER_TRACE_TX_TIMEOUT);
            return false;
        }
        tight_loop_contents();
    }
    while (!pio_sm_is_tx_fifo_empty(TX_PIO, TX_SM))
    {
        if (expired(timeout))
        {
            stop_sequence();
            READER_RF_TRACE_REASON(READER_TRACE_TX_TIMEOUT);
            return false;
        }
        tight_loop_contents();
    }
    TX_PIO->fdebug = stall_mask;
    while ((TX_PIO->fdebug & stall_mask) == 0u)
    {
        if (expired(timeout))
        {
            stop_sequence();
            READER_RF_TRACE_REASON(READER_TRACE_TX_TIMEOUT);
            return false;
        }
        tight_loop_contents();
    }
    stop_sequence();
    return true;
}

bool pn532_reader_sequence_play(const reader_tx_waveform_t *waveform,
                                uint16_t tick_fc)
{
    return pn532_reader_sequence_play_at(waveform, tick_fc, 0u);
}
