#include "phy/nfcv/picc/nfcv_rx.h"

#include "board/tag_board.h"
#include "phy/nfcv/picc/nfcv_rx_decode.h"
#include "phy/picc/picc_phy.h"

#include "hardware/irq.h"
#include "hardware/pio.h"
#include "nfcv_rx_sample.pio.h"
#include "pico/stdlib.h"

#define NFCV_RX_1OF4_MAX_SAMPLES                                               \
    (1u + NFCV_FRAME_MAX_BYTES * NFCV_1OF4_SYMBOLS_PER_BYTE + 1u)
#define NFCV_RX_IRQ_SAMPLE_BUDGET 8u
#define NFCV_RX_SAMPLE_BITS 8u

typedef enum
{
    NFCV_RX_MODE_UNKNOWN = 0,
    NFCV_RX_MODE_1OF4,
    NFCV_RX_MODE_1OF256,
} nfcv_rx_mode_t;

static uint pio_offset;
static bool program_loaded;
static volatile bool rx_enabled = true;
static nfcv_rx_frame_handler_t frame_handler;

static union
{
    uint8_t oneof4_samples[NFCV_RX_1OF4_MAX_SAMPLES];
    nfcv_1of256_decoder_t oneof256_decoder;
} capture_storage;
static volatile uint16_t raw_sample_count;
static volatile bool raw_overflow;
static volatile uint32_t raw_eof_time_us;
static volatile nfcv_rx_mode_t rx_mode;

static void rx_irq_handler(void);

#define NFCV_RX_PROGRAM nfcv_rx_sample_program
#define NFCV_RX_PROGRAM_CONFIG nfcv_rx_sample_program_get_default_config
#define NFCV_RX_PIO_CLKDIV ((float)NFC_SYS_CLOCKS_PER_FC / 2.0f)

static inline void rearm_capture(void)
{
    raw_sample_count = 0u;
    raw_overflow = false;
    rx_mode = NFCV_RX_MODE_UNKNOWN;
    nfcv_1of256_decoder_reset(&capture_storage.oneof256_decoder);
    tag_rx_pio_rearm(pio_offset);
}

/*
 * A 1-of-4 capture is only worth keeping while every drained byte is a
 * legal symbol: SOF first, then data symbols, then EOF. Anything else
 * means the state machine triggered on noise or on the reader field
 * going away (phones gate the carrier between technology polls), so the
 * capture is aborted and re-armed at once. This mirrors the legacy
 * decoder, which rejected a frame on its first invalid sample group;
 * without it a false trigger keeps the sampler busy for milliseconds
 * and the real frame lands mid-buffer, where decode rejects it.
 */
static inline bool sample_1of4_valid(uint8_t sample, uint16_t index)
{
    if (index == 0u)
    {
        return sample == NFCV_1OF4_SOF;
    }
    if (nfcv_sample_is_eof(sample))
    {
        return true;
    }
    uint8_t two_bits;
    return nfcv_decode_1of4_sample(sample, &two_bits);
}

static inline bool __not_in_flash_func(process_sample)(uint8_t sample,
                                                       bool *complete)
{
    if (rx_mode == NFCV_RX_MODE_UNKNOWN)
    {
        if (sample == NFCV_1OF4_SOF)
        {
            rx_mode = NFCV_RX_MODE_1OF4;
            capture_storage.oneof4_samples[raw_sample_count++] = sample;
            return true;
        }
        if (sample == NFCV_1OF256_SOF)
        {
            rx_mode = NFCV_RX_MODE_1OF256;
            return true;
        }
        return false;
    }

    if (rx_mode == NFCV_RX_MODE_1OF4)
    {
        if (!sample_1of4_valid(sample, raw_sample_count))
        {
            return false;
        }
        if (raw_sample_count < NFCV_RX_1OF4_MAX_SAMPLES)
        {
            capture_storage.oneof4_samples[raw_sample_count++] = sample;
        }
        else
        {
            raw_overflow = true;
            *complete = true;
            return true;
        }

        if (sample == NFCV_1OF4_EOF)
        {
            raw_eof_time_us = time_us_32();
            *complete = true;
        }
        return true;
    }

    if (rx_mode == NFCV_RX_MODE_1OF256)
    {
        const nfcv_1of256_step_t step =
            nfcv_1of256_decoder_push(&capture_storage.oneof256_decoder, sample);
        if (step == NFCV_1OF256_STEP_INVALID)
        {
            return false;
        }
        if (step == NFCV_1OF256_STEP_COMPLETE)
        {
            raw_eof_time_us = time_us_32();
            *complete = true;
        }
        return true;
    }

    return false;
}

static void __not_in_flash_func(dispatch_frame)(void)
{
    nfcv_frame_t frame;
    bool decoded = false;
    if (!raw_overflow && rx_mode == NFCV_RX_MODE_1OF4)
    {
        decoded = nfcv_rx_decode_samples(
            capture_storage.oneof4_samples, raw_sample_count, &frame);
    }
    else if (!raw_overflow && rx_mode == NFCV_RX_MODE_1OF256)
    {
        decoded = nfcv_1of256_decoder_finish(&capture_storage.oneof256_decoder,
                                             &frame);
    }
    if (decoded && frame_handler != NULL)
    {
        frame_handler(&frame, raw_eof_time_us);
    }
    if (rx_enabled)
    {
        rearm_capture();
        irq_set_enabled(TAG_RX_IRQ, true);
    }
}

static void __not_in_flash_func(rx_irq_handler)(void)
{
    uint8_t drained = 0u;
    bool complete = false;
    bool abort = false;
    while (drained < NFCV_RX_IRQ_SAMPLE_BUDGET && !complete &&
           !pio_sm_is_rx_fifo_empty(TAG_RX_PIO, TAG_RX_SM))
    {
        const uint8_t sample = (uint8_t)pio_sm_get(TAG_RX_PIO, TAG_RX_SM);
        drained++;
        if (!process_sample(sample, &complete))
        {
            abort = true;
            break;
        }
    }

    if (abort)
    {
        while (!pio_sm_is_rx_fifo_empty(TAG_RX_PIO, TAG_RX_SM))
        {
            (void)pio_sm_get(TAG_RX_PIO, TAG_RX_SM);
        }
        rearm_capture();
        irq_clear(TAG_RX_IRQ);
        return;
    }

    if (complete)
    {
        irq_set_enabled(TAG_RX_IRQ, false);
        tag_rx_pio_stop();
        /* Keep RX stopped through decoding, handling and synchronous TX.
         * Dispatch at EOF so response launch never waits for foreground work.
         */
        dispatch_frame();
    }
}

bool nfcv_rx_init(nfcv_rx_frame_handler_t handler)
{
    irq_set_enabled(TAG_RX_IRQ, false);
    pio_sm_set_enabled(TAG_RX_PIO, TAG_RX_SM, false);
    frame_handler = handler;

    if (!program_loaded)
    {
        if (!pio_can_add_program(TAG_RX_PIO, &NFCV_RX_PROGRAM))
        {
            frame_handler = NULL;
            return false;
        }
        pio_offset = pio_add_program(TAG_RX_PIO, &NFCV_RX_PROGRAM);
        program_loaded = true;
    }

    tag_rx_pio_prepare_input();

    pio_sm_config config = NFCV_RX_PROGRAM_CONFIG(pio_offset);
    sm_config_set_clkdiv(&config, NFCV_RX_PIO_CLKDIV);
    sm_config_set_jmp_pin(&config, TAG_RX_GPIO);
    sm_config_set_in_pins(&config, TAG_RX_GPIO);
    sm_config_set_in_shift(&config, false, true, NFCV_RX_SAMPLE_BITS);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    pio_sm_init(TAG_RX_PIO, TAG_RX_SM, pio_offset, &config);
    tag_rx_pio_reset(pio_offset);

    tag_rx_pio_fifo_irq_set_enabled(true);
    irq_set_exclusive_handler(TAG_RX_IRQ, rx_irq_handler);
    irq_set_priority(TAG_RX_IRQ, PICO_HIGHEST_IRQ_PRIORITY);
    rx_enabled = true;
    rearm_capture();
    irq_set_enabled(TAG_RX_IRQ, true);
    return true;
}

static void nfcv_rx_set_enabled(bool enabled)
{
    rx_enabled = enabled;
    irq_set_enabled(TAG_RX_IRQ, false);
    tag_rx_pio_fifo_irq_set_enabled(false);
    tag_rx_pio_reset(pio_offset);
    irq_clear(TAG_RX_IRQ);

    if (enabled)
    {
        rearm_capture();
        tag_rx_pio_fifo_irq_set_enabled(true);
        irq_set_enabled(TAG_RX_IRQ, true);
    }
}

void nfcv_rx_deinit(void)
{
    nfcv_rx_set_enabled(false);
    irq_remove_handler(TAG_RX_IRQ, rx_irq_handler);
    if (program_loaded)
    {
        pio_remove_program(TAG_RX_PIO, &NFCV_RX_PROGRAM, pio_offset);
        program_loaded = false;
    }
    frame_handler = NULL;
}
