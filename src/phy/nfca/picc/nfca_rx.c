#include "phy/nfca/picc/nfca_rx.h"

#include "board/tag_board.h"
#include "common/compiler.h"
#include "phy/nfca/picc/nfca_trace.h"
#include "phy/nfca/picc/nfca_tx.h"
#include "phy/picc/picc_phy.h"

#include "hardware/irq.h"
#include "hardware/pio.h"
#include "nfca_rx_levels.pio.h"
#include "pico/stdlib.h"

#include <string.h>

#define NFCA_RX_FIRST_LEVEL_SLOT 2u
#define NFCA_RX_EOF_PIPELINE_SLOTS 3u
#define NFCA_RX_EOF_TRIM_SLOTS 2u
#define NFCA_RX_SOF_LEVEL_MASK 0x07u
#define NFCA_RX_EOF_CONFIRM_SLOTS                                              \
    (NFCA_RX_EOF_GAP_SLOTS + NFCA_RX_EOF_PIPELINE_SLOTS)
static uint pio_offset;
static bool program_loaded;
static volatile bool rx_enabled;
static nfca_rx_frame_handler_t frame_handler;
static uint8_t bit_quarter_masks[NFCA_RX_MAX_BITS];

static void rx_irq_handler(void);

#if NFC_SYS_CLOCKS_PER_FC == 4u
#define NFCA_LEVEL_PROGRAM nfca_rx_levels_4fc_program
#define NFCA_LEVEL_PROGRAM_CONFIG nfca_rx_levels_4fc_program_get_default_config
#else
#define NFCA_LEVEL_PROGRAM nfca_rx_levels_5fc_program
#define NFCA_LEVEL_PROGRAM_CONFIG nfca_rx_levels_5fc_program_get_default_config
#endif

static inline bool pio_level_word_try_get(uint32_t *levels)
{
    if (pio_sm_is_rx_fifo_empty(NFCA_RX_PIO, NFCA_RX_SM))
    {
        return false;
    }

    *levels = pio_sm_get(NFCA_RX_PIO, NFCA_RX_SM);
    return true;
}

static bool __not_in_flash_func(capture_levels_until_eof)(
    uint16_t *sampled_slots, nfca_frame_stream_t *stream, nfca_frame_t *frame)
{
    uint16_t first_slot = NFCA_RX_FIRST_LEVEL_SLOT;
    uint16_t last_rise_slot = 0u;
    bool previous_high = false;
    bool have_rise = false;

    while (first_slot < NFCA_RX_MAX_SLOTS)
    {
        uint32_t levels;
        if (!pio_level_word_try_get(&levels))
        {
            tight_loop_contents();
            continue;
        }

        /* Reject a residual envelope without a physical SOF immediately,
         * rather than retaining idle samples until the next reader frame. */
        if (first_slot == NFCA_RX_FIRST_LEVEL_SLOT &&
            (levels & NFCA_RX_SOF_LEVEL_MASK) == 0u)
        {
            *sampled_slots =
                NFCA_RX_FIRST_LEVEL_SLOT + NFCA_RX_SAMPLES_PER_WORD;
            return false;
        }

        /* A full idle word needs no edge reconstruction. Recognize the same
         * EOF before capture bookkeeping so it does not shift the stopwatch. */
        if (levels == 0u && have_rise &&
            first_slot + (NFCA_RX_SAMPLES_PER_WORD - 1u) < NFCA_RX_MAX_SLOTS &&
            first_slot + (NFCA_RX_SAMPLES_PER_WORD - 1u) >=
                last_rise_slot + NFCA_RX_EOF_CONFIRM_SLOTS)
        {
            const uint16_t elapsed =
                (uint16_t)((first_slot + (NFCA_RX_SAMPLES_PER_WORD - 1u) -
                            last_rise_slot - NFCA_RX_EOF_CONFIRM_SLOTS) *
                           NFC_8FC_SYS_CYCLES);
            nfca_tx_fdt_start_elapsed(elapsed);
            *sampled_slots = (uint16_t)(last_rise_slot + NFCA_RX_EOF_GAP_SLOTS +
                                        NFCA_RX_EOF_TRIM_SLOTS);
            return true;
        }

        const uint16_t remaining = (uint16_t)(NFCA_RX_MAX_SLOTS - first_slot);
        const uint8_t count = remaining < NFCA_RX_SAMPLES_PER_WORD
                                  ? (uint8_t)remaining
                                  : NFCA_RX_SAMPLES_PER_WORD;
        const uint16_t word_end_slot = (uint16_t)(first_slot + count - 1u);

        if (count < NFCA_RX_SAMPLES_PER_WORD)
        {
            levels &= (1u << count) - 1u;
        }

        const uint32_t shifted = (levels << 1u) | (uint32_t)previous_high;
        const uint32_t rises = levels & ~shifted;
        bool eof_reached = false;
        uint16_t elapsed = 0u;

        if (have_rise)
        {
            const uint16_t eof_confirm_slot =
                (uint16_t)(last_rise_slot + NFCA_RX_EOF_CONFIRM_SLOTS);
            if (rises != 0u)
            {
                const uint16_t first_rise_slot =
                    (uint16_t)(first_slot + fast_ctz32(rises));
                if (first_rise_slot >= eof_confirm_slot)
                {
                    eof_reached = true;
                }
            }
            else if (word_end_slot >= eof_confirm_slot)
            {
                eof_reached = true;
            }

            if (eof_reached)
            {
                elapsed = (uint16_t)((word_end_slot - eof_confirm_slot) *
                                     NFC_8FC_SYS_CYCLES);
                /* Establish the time origin before result bookkeeping. */
                nfca_tx_fdt_start_elapsed(elapsed);
            }
        }

        if (levels != 0u)
        {
            nfca_quarters_add_level_samples(
                bit_quarter_masks, first_slot, levels, count);
        }
        if (eof_reached)
        {
            *sampled_slots = (uint16_t)(last_rise_slot + NFCA_RX_EOF_GAP_SLOTS +
                                        NFCA_RX_EOF_TRIM_SLOTS);
            return true;
        }
        previous_high = ((levels >> (count - 1u)) & 1u) != 0u;

        if (rises != 0u)
        {
            last_rise_slot =
                (uint16_t)(first_slot + (NFCA_RX_SAMPLES_PER_WORD - 1u) -
                           fast_clz32(rises));
            have_rise = true;
        }

        /* The FIFO word closes these slots permanently. Decode complete
         * bytes while the next word is sampled, without interpreting commands.
         */
        nfca_frame_stream_consume(
            stream, bit_quarter_masks, (uint16_t)(word_end_slot + 1u), frame);
        first_slot = (uint16_t)(first_slot + count);
    }

    nfca_tx_fdt_start_elapsed(0u);
    *sampled_slots = NFCA_RX_MAX_SLOTS;
    return false;
}

/* Sample the sticky FIFO flag after response handling; do not add work to
 * the EOF stopwatch or the frame handler's FDT budget. */
#ifdef PNU_RF_TRACE
#define RECORD_RX_TRACE()                                                      \
    do                                                                         \
    {                                                                          \
        const uint32_t stall_mask = 1u                                         \
                                    << (PIO_FDEBUG_RXSTALL_LSB + NFCA_RX_SM);  \
        const bool stalled = (NFCA_RX_PIO->fdebug & stall_mask) != 0u;         \
        NFCA_RX_PIO->fdebug = stall_mask;                                      \
        NFCA_TRACE_RECORD(&rx_frame,                                           \
                          time_us_32(),                                        \
                          sampled_slots,                                       \
                          eof_seen,                                            \
                          decoded,                                             \
                          stream.failed,                                       \
                          stalled);                                            \
    } while (0)
#else
#define RECORD_RX_TRACE() ((void)0)
#endif

static void __not_in_flash_func(rx_irq_handler)(void)
{
    irq_set_enabled(NFCA_RX_IRQ, false);

    nfca_frame_t rx_frame;
    if (pio_sm_is_rx_fifo_empty(NFCA_RX_PIO, NFCA_RX_SM))
    {
        if (rx_enabled)
        {
            tag_rx_pio_rearm(pio_offset);
            irq_set_enabled(NFCA_RX_IRQ, true);
        }
        return;
    }

    memset(bit_quarter_masks, 0, sizeof(bit_quarter_masks));

    nfca_frame_stream_t stream;
    nfca_frame_stream_init(&stream, &rx_frame);
    uint16_t sampled_slots;
    nfca_tx_fdt_prepare();
    const bool eof_seen =
        capture_levels_until_eof(&sampled_slots, &stream, &rx_frame);

    tag_rx_pio_stop_and_drain();

    const bool decoded =
        eof_seen && nfca_frame_stream_finish(
                        &stream, bit_quarter_masks, sampled_slots, &rx_frame);

    if (decoded && frame_handler != NULL)
    {
        const bool last_bit =
            rx_frame.short_frame
                ? ((rx_frame.bytes[0] &
                    (1u << (NFCA_SHORT_REQUEST_BITS - 1u))) != 0u)
                : (rx_frame.parity[rx_frame.size_bytes - 1u] != 0u);
        nfca_tx_fdt_set_last_bit(last_bit);

        frame_handler(&rx_frame);
    }

    RECORD_RX_TRACE();

    if (rx_enabled)
    {
        tag_rx_pio_rearm(pio_offset);
        irq_set_enabled(NFCA_RX_IRQ, true);
    }
}

bool nfca_rx_init(nfca_rx_frame_handler_t handler)
{
    irq_set_enabled(NFCA_RX_IRQ, false);
    pio_sm_set_enabled(NFCA_RX_PIO, NFCA_RX_SM, false);
    frame_handler = handler;

    if (!program_loaded)
    {
        if (!pio_can_add_program(NFCA_RX_PIO, &NFCA_LEVEL_PROGRAM))
        {
            frame_handler = NULL;
            return false;
        }
        pio_offset = pio_add_program(NFCA_RX_PIO, &NFCA_LEVEL_PROGRAM);
        program_loaded = true;
    }

    tag_rx_pio_prepare_input();
#if NFCA_TAG_RX_INPUT_INVERTED
    gpio_set_inover(TAG_RX_GPIO, GPIO_OVERRIDE_INVERT);
#endif

    pio_sm_config config = NFCA_LEVEL_PROGRAM_CONFIG(pio_offset);
    sm_config_set_jmp_pin(&config, TAG_RX_GPIO);
    sm_config_set_in_pins(&config, TAG_RX_GPIO);
    sm_config_set_in_shift(&config, true, true, NFCA_RX_SAMPLES_PER_WORD);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&config, NFCA_RX_PIO_CLKDIV);

    pio_sm_init(NFCA_RX_PIO, NFCA_RX_SM, pio_offset, &config);
#ifdef PNU_RF_TRACE
    NFCA_RX_PIO->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + NFCA_RX_SM);
#endif
    tag_rx_pio_start();

    tag_rx_pio_fifo_irq_set_enabled(true);
    irq_set_exclusive_handler(NFCA_RX_IRQ, rx_irq_handler);
    irq_set_priority(NFCA_RX_IRQ, PICO_HIGHEST_IRQ_PRIORITY);
    rx_enabled = true;
    irq_set_enabled(NFCA_RX_IRQ, true);
    return true;
}

static void nfca_rx_set_enabled(bool enabled)
{
    rx_enabled = enabled;
    irq_set_enabled(NFCA_RX_IRQ, false);
    tag_rx_pio_fifo_irq_set_enabled(false);
    tag_rx_pio_stop_and_drain();
    irq_clear(NFCA_RX_IRQ);

    if (enabled)
    {
        tag_rx_pio_rearm(pio_offset);
        tag_rx_pio_fifo_irq_set_enabled(true);
        irq_set_enabled(NFCA_RX_IRQ, true);
    }
}

void nfca_rx_deinit(void)
{
    nfca_rx_set_enabled(false);
    irq_remove_handler(NFCA_RX_IRQ, rx_irq_handler);
    if (program_loaded)
    {
        pio_remove_program(NFCA_RX_PIO, &NFCA_LEVEL_PROGRAM, pio_offset);
        program_loaded = false;
    }
    frame_handler = NULL;
}
