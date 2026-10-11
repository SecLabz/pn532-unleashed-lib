#include "phy/pcd/reader_subcarrier_level_rx.h"

#include "board/reader_board.h"
#include "phy/timing.h"

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "reader_subcarrier_level_rx.pio.h"

static const struct pio_program *active_program;
static uint active_pio_offset;
static uint8_t active_sample_fc;

static void __not_in_flash_func(reset_sm)(void)
{
    pio_sm_set_enabled(READER_RX_PIO, READER_RX_SM, false);
    pio_sm_clear_fifos(READER_RX_PIO, READER_RX_SM);
    pio_sm_restart(READER_RX_PIO, READER_RX_SM);
    pio_sm_clkdiv_restart(READER_RX_PIO, READER_RX_SM);
    pio_sm_exec(READER_RX_PIO, READER_RX_SM, pio_encode_jmp(active_pio_offset));
}

bool reader_subcarrier_level_rx_init(uint8_t sample_fc)
{
    gpio_init(READER_RX_GPIO);
    gpio_set_dir(READER_RX_GPIO, GPIO_IN);
    gpio_set_pulls(READER_RX_GPIO, READER_RX_PULL_UP, READER_RX_PULL_DOWN);
    gpio_set_input_hysteresis_enabled(READER_RX_GPIO, false);
    gpio_set_inover(READER_RX_GPIO, GPIO_OVERRIDE_NORMAL);

    if (sample_fc != READER_RX_NFCA_SAMPLE_FC &&
        sample_fc != READER_RX_NFCV_SAMPLE_FC)
    {
        return false;
    }
    reader_subcarrier_level_rx_release();
    const struct pio_program *program =
        sample_fc == READER_RX_NFCA_SAMPLE_FC
            ? &reader_subcarrier_level_rx_1fc_program
            : &reader_subcarrier_level_rx_8fc_program;
    if (!pio_can_add_program(READER_RX_PIO, program))
    {
        return false;
    }
    active_pio_offset = pio_add_program(READER_RX_PIO, program);
    active_program = program;
    active_sample_fc = sample_fc;
    pio_sm_config config =
        sample_fc == READER_RX_NFCA_SAMPLE_FC
            ? reader_subcarrier_level_rx_1fc_program_get_default_config(
                  active_pio_offset)
            : reader_subcarrier_level_rx_8fc_program_get_default_config(
                  active_pio_offset);
    sm_config_set_clkdiv(
        &config,
        (float)((sample_fc == READER_RX_NFCA_SAMPLE_FC ? 1u : 2u) *
                NFC_SYS_CLOCKS_PER_FC));

    sm_config_set_in_pins(&config, READER_RX_GPIO);
    sm_config_set_in_shift(&config, true, true, READER_RX_SAMPLES_PER_WORD);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    pio_sm_init(READER_RX_PIO, READER_RX_SM, active_pio_offset, &config);
    reset_sm();
    return true;
}

void reader_subcarrier_level_rx_release(void)
{
    /* Stop capture before releasing instructions for passive tag mode. */
    pio_sm_set_enabled(READER_RX_PIO, READER_RX_SM, false);
    pio_sm_clear_fifos(READER_RX_PIO, READER_RX_SM);

    if (active_program != NULL)
    {
        pio_remove_program(READER_RX_PIO, active_program, active_pio_offset);
        active_program = NULL;
    }
    active_pio_offset = 0u;
}

void __not_in_flash_func(reader_subcarrier_level_rx_capture)(
    uint32_t *sample_words,
    uint16_t word_capacity,
    uint32_t blank_us,
    uint32_t sof_timeout_us,
    uint32_t window_us,
    reader_subcarrier_level_rx_stats_t *stats)
{
    /* This runs between TX and RX. Keep initialization and blanking in RAM:
     * XIP misses here can consume the tag's fixed response delay. */
    volatile reader_subcarrier_level_rx_stats_t *reset = stats;
    reset->word_count = 0u;
    reset->first_edge_us = 0u;
    reset->capture_us = 0u;
    reset->truncated = false;
    reset->overflowed = false;
    if (sample_words == NULL || word_capacity == 0u)
    {
        stats->truncated = true;
        return;
    }

    const uint32_t blank_started_us = time_us_32();
    while (time_us_32() - blank_started_us < blank_us)
    {
        tight_loop_contents();
    }
    reset_sm();

    const uint32_t irq_state = save_and_disable_interrupts();
    const uint32_t stall_mask = 1u << (PIO_FDEBUG_RXSTALL_LSB + READER_RX_SM);
    READER_RX_PIO->fdebug = stall_mask;
    pio_sm_set_enabled(READER_RX_PIO, READER_RX_SM, true);
    const uint32_t start = time_us_32();
    uint32_t first_word_time = 0u;
    while ((uint32_t)(time_us_32() - start) < window_us &&
           stats->word_count < word_capacity)
    {
        while (!pio_sm_is_rx_fifo_empty(READER_RX_PIO, READER_RX_SM))
        {
            if (stats->word_count == word_capacity)
            {
                break;
            }
            const uint32_t word = pio_sm_get(READER_RX_PIO, READER_RX_SM);
            if (stats->word_count == 0u)
            {
                first_word_time = time_us_32();
            }
            sample_words[stats->word_count++] = word;
            /* Qualify the complete first NFC-A half-bit. The comparator can
             * hold its initial high pulse beyond the first 32/fc word before
             * the next subcarrier rise. Rejecting that word alone drops a
             * real SOF and anchors on data. A lone DC transition has no second
             * rise across the full 64/fc; discard it before retaining a grid.
             */
            if (active_sample_fc == READER_RX_NFCA_SAMPLE_FC &&
                stats->word_count == 2u)
            {
                const uint32_t first = sample_words[0];
                const uint32_t previous = (word << 1u) | (first >> 31u);
                if ((first & ~(first << 1u) & ~1u) == 0u &&
                    (word & ~previous) == 0u)
                {
                    stats->word_count = 0u;
                    reset_sm();
                    pio_sm_set_enabled(READER_RX_PIO, READER_RX_SM, true);
                    break;
                }
            }
        }
        if (stats->word_count == 0u &&
            (uint32_t)(time_us_32() - start) >= sof_timeout_us)
        {
            break;
        }
        tight_loop_contents();
    }
    pio_sm_set_enabled(READER_RX_PIO, READER_RX_SM, false);
    stats->overflowed = (READER_RX_PIO->fdebug & stall_mask) != 0u;
    restore_interrupts(irq_state);

    stats->capture_us = time_us_32() - start;
    stats->truncated = stats->word_count == word_capacity;
    if (stats->word_count != 0u)
    {
        const uint32_t word_duration_us =
            (active_sample_fc * READER_RX_SAMPLES_PER_WORD * 1000u +
             NFC_CARRIER_KHZ / 2u) /
            NFC_CARRIER_KHZ;
        const uint32_t first_word_delay = first_word_time - start;
        stats->first_edge_us = first_word_delay > word_duration_us
                                   ? first_word_delay - word_duration_us
                                   : 0u;
    }
}
