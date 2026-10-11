#pragma once

#include <stdint.h>

#define RF_CAPTURE_READER_TX_SLOT_WORDS 1176u
#define RF_CAPTURE_READER_RX_SAMPLE_WORDS 1240u

typedef union
{
    uint32_t reader_tx_slot_words[RF_CAPTURE_READER_TX_SLOT_WORDS];
    uint32_t reader_rx_sample_words[RF_CAPTURE_READER_RX_SAMPLE_WORDS];
} rf_capture_scratch_t;

typedef enum
{
    RF_CAPTURE_SCRATCH_NONE = 0,
    RF_CAPTURE_SCRATCH_READER_TX,
    RF_CAPTURE_SCRATCH_READER_RX,
} rf_capture_scratch_owner_t;

/* One foreground reader owns this arena across DMA playback or RX/decode.
 * Reader TX and RX never overlap; tag backends do not borrow this memory. */
rf_capture_scratch_t *
rf_capture_scratch_acquire(rf_capture_scratch_owner_t owner);
void rf_capture_scratch_release(rf_capture_scratch_owner_t owner);
