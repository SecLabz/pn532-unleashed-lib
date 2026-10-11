#pragma once

#include "phy/reader_frame.h"

#include <stdbool.h>
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

void reader_tx_waveform_init(reader_tx_waveform_t *waveform);
bool reader_tx_waveform_append(reader_tx_waveform_t *waveform,
                               reader_tx_level_t level,
                               uint16_t duration_fc);
