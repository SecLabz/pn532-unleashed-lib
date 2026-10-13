#pragma once

#include "common/compiler.h"
#include "phy/reader_frame.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    READER_TX_LEVEL_OFF = 0,
    READER_TX_LEVEL_FULL,
} reader_tx_level_t;

typedef struct
{
    uint16_t duration_fc;
    uint8_t level;
} reader_tx_segment_t;

_Static_assert(sizeof(reader_tx_segment_t) == 4u,
               "reader TX segments must remain compact");

/* Modified Miller needs up to two transitions per data/parity bit.
 * This also covers NFC-V 1-out-of-4 coding and both frame boundaries. */
#define READER_TX_WAVEFORM_MAX_SEGMENTS (18u * READER_FRAME_MAX_BYTES + 24u)

typedef struct
{
    reader_tx_segment_t segments[READER_TX_WAVEFORM_MAX_SEGMENTS];
    uint16_t count;
} reader_tx_waveform_t;

static FORCE_INLINE void reader_tx_waveform_init(reader_tx_waveform_t *waveform)
{
    if (waveform != NULL)
    {
        waveform->count = 0u;
    }
}

static FORCE_INLINE bool
reader_tx_waveform_append(reader_tx_waveform_t *waveform,
                          reader_tx_level_t level,
                          uint16_t duration_fc)
{
    if (waveform == NULL || duration_fc == 0u || level > READER_TX_LEVEL_FULL)
    {
        return false;
    }

    if (waveform->count != 0u)
    {
        reader_tx_segment_t *last = &waveform->segments[waveform->count - 1u];
        if (last->level == level &&
            last->duration_fc <= (uint16_t)(UINT16_MAX - duration_fc))
        {
            last->duration_fc = (uint16_t)(last->duration_fc + duration_fc);
            return true;
        }
    }

    if (waveform->count >= READER_TX_WAVEFORM_MAX_SEGMENTS)
    {
        return false;
    }
    waveform->segments[waveform->count++] = (reader_tx_segment_t){
        .duration_fc = duration_fc,
        .level = level,
    };
    return true;
}

/* Expand into physical DMA slots and one final FULL slot. Capacity and count
 * include that final slot only in capacity: count is the waveform length.
 * Output may be partial on failure; never transmit it unless true is returned.
 */
bool reader_tx_waveform_slots(const reader_tx_waveform_t *waveform,
                              uint16_t tick_fc,
                              uint32_t *slots,
                              uint16_t capacity_slots,
                              uint16_t *count);
