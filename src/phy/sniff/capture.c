#include "phy/sniff/capture.h"

#include "board/board_config.h"

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "sniff_samples.pio.h"

#include <string.h>

#define CAPTURE_WORDS 2048u
#define CAPTURE_RING_BITS 13u
#define CAPTURE_CHANNELS 3u

/* Each channel has its own naturally aligned DMA write ring. At 8/fc this
 * retains 38.6 ms. Completed words are immutable until a full ring laps. */
static uint32_t samples[CAPTURE_CHANNELS][CAPTURE_WORDS]
    __attribute__((aligned(1u << CAPTURE_RING_BITS)));
static struct
{
    int dma[CAPTURE_CHANNELS];
    uint offset;
    uint8_t channels;
    uint32_t consumed_words;
    uint32_t final_written_words[CAPTURE_CHANNELS];
    bool frozen;
    bool loaded;
    bool claimed;
    bool overrun;
    bool active;
} capture;

void sniff_capture_stop(void)
{
    if (capture.active)
    {
        pio_set_sm_mask_enabled(
            RF_RX_PIO, ((1u << capture.channels) - 1u), false);
        for (uint i = 0u; i < capture.channels; i++)
        {
            dma_channel_abort((uint)capture.dma[i]);
            dma_channel_unclaim((uint)capture.dma[i]);
            pio_sm_clear_fifos(RF_RX_PIO, i);
        }
        capture.active = false;
    }
    if (capture.loaded)
    {
        pio_remove_program(RF_RX_PIO, &sniff_samples_program, capture.offset);
        capture.loaded = false;
    }
    if (capture.claimed)
    {
        pio_sm_unclaim(RF_RX_PIO, 2u);
        capture.claimed = false;
    }
}

bool sniff_capture_start(const uint8_t *pins, uint8_t channels)
{
    if (pins == NULL || channels < 2u || channels > 3u ||
        (channels == 3u && pio_sm_is_claimed(RF_RX_PIO, 2u)) ||
        !pio_can_add_program(RF_RX_PIO, &sniff_samples_program))
    {
        return false;
    }
    int dma_channels[CAPTURE_CHANNELS];
    for (uint i = 0u; i < channels; i++)
    {
        dma_channels[i] = dma_claim_unused_channel(false);
        if (dma_channels[i] < 0)
        {
            for (uint j = 0u; j < i; j++)
            {
                dma_channel_unclaim((uint)dma_channels[j]);
            }
            return false;
        }
    }
    memset(&capture, 0, sizeof(capture));
    capture.channels = channels;
    memcpy(capture.dma, dma_channels, channels * sizeof(int));
    if (channels == 3u)
    {
        pio_sm_claim(RF_RX_PIO, 2u);
        capture.claimed = true;
    }
    capture.offset = pio_add_program(RF_RX_PIO, &sniff_samples_program);
    capture.loaded = true;
    for (uint i = 0u; i < capture.channels; i++)
    {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_disable_pulls(pins[i]);
        gpio_set_inover(pins[i], GPIO_OVERRIDE_NORMAL);
        gpio_set_input_hysteresis_enabled(pins[i], false);
        pio_sm_config sm =
            sniff_samples_program_get_default_config(capture.offset);
        sm_config_set_in_pins(&sm, pins[i]);
        sm_config_set_in_shift(&sm, true, true, 32u);
        sm_config_set_fifo_join(&sm, PIO_FIFO_JOIN_RX);
        sm_config_set_clkdiv(&sm, (float)NFC_SYS_CLOCKS_PER_FC);
        pio_sm_init(RF_RX_PIO, i, capture.offset, &sm);
        dma_channel_config dma =
            dma_channel_get_default_config((uint)dma_channels[i]);
        channel_config_set_transfer_data_size(&dma, DMA_SIZE_32);
        channel_config_set_read_increment(&dma, false);
        channel_config_set_write_increment(&dma, true);
        channel_config_set_ring(&dma, true, CAPTURE_RING_BITS);
        channel_config_set_dreq(&dma, pio_get_dreq(RF_RX_PIO, i, false));
        dma_channel_configure((uint)dma_channels[i],
                              &dma,
                              samples[i],
                              &RF_RX_PIO->rxf[i],
                              UINT32_MAX,
                              true);
    }
    RF_RX_PIO->fdebug = ((1u << channels) - 1u) << PIO_FDEBUG_RXSTALL_LSB;
    capture.active = true;
    pio_enable_sm_mask_in_sync(RF_RX_PIO, ((1u << capture.channels) - 1u));
    return true;
}

void sniff_capture_pause(void)
{
    if (!capture.active || capture.frozen)
    {
        return;
    }
    pio_set_sm_mask_enabled(RF_RX_PIO, ((1u << capture.channels) - 1u), false);
    busy_wait_us_32(10u);
    for (uint i = 0u; i < capture.channels; i++)
    {
        capture.final_written_words[i] =
            UINT32_MAX - dma_hw->ch[capture.dma[i]].transfer_count;
        dma_channel_abort((uint)capture.dma[i]);
    }
    capture.frozen = true;
}

bool sniff_capture_read(uint32_t levels[3])
{
    if (!capture.active || capture.overrun)
    {
        return false;
    }
    uint32_t available_words = UINT32_MAX;
    for (uint i = 0u; i < capture.channels; i++)
    {
        const uint32_t written_words =
            capture.frozen
                ? capture.final_written_words[i]
                : UINT32_MAX - dma_hw->ch[capture.dma[i]].transfer_count;
        if (written_words - capture.consumed_words >= CAPTURE_WORDS ||
            (!capture.frozen &&
             dma_hw->ch[capture.dma[i]].transfer_count == 0u))
        {
            capture.overrun = true;
            return false;
        }
        if (written_words < available_words)
        {
            available_words = written_words;
        }
    }
    if ((RF_RX_PIO->fdebug &
         (((1u << capture.channels) - 1u) << PIO_FDEBUG_RXSTALL_LSB)) != 0u)
    {
        capture.overrun = true;
        return false;
    }
    if (available_words == capture.consumed_words)
    {
        return false;
    }
    __dmb();
    const uint32_t index = capture.consumed_words & (CAPTURE_WORDS - 1u);
    for (uint i = 0u; i < capture.channels; i++)
    {
        levels[i] = samples[i][index];
    }
    capture.consumed_words++;
    return true;
}

bool sniff_capture_overrun(void)
{
    return capture.overrun;
}
