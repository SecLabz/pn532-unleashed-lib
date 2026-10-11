#pragma once

#include "phy/nfcv/picc/nfcv_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A 1-of-256 downlink has one pause in each group of 256 positions.  The
 * fixed-grid PIO sample packs four positions in one byte, so the receiver
 * needs only one decoded byte and the current position while the group is in
 * flight.  Keeping this state machine here lets the tag receiver and the
 * stopped-buffer decoder use exactly the same framing rules.
 */
typedef struct
{
    uint8_t bytes[NFCV_FRAME_MAX_BYTES];
    uint8_t size_bytes;
    uint8_t sample_index;
    bool pause_seen;
    bool eof_seen;
    bool invalid;
} nfcv_1of256_decoder_t;

typedef enum
{
    NFCV_1OF256_STEP_INVALID = 0,
    NFCV_1OF256_STEP_CONTINUE,
    NFCV_1OF256_STEP_COMPLETE,
} nfcv_1of256_step_t;

static inline void nfcv_1of256_decoder_reset(nfcv_1of256_decoder_t *decoder)
{
    decoder->size_bytes = 0u;
    decoder->sample_index = 0u;
    decoder->pause_seen = false;
    decoder->eof_seen = false;
    decoder->invalid = false;
}

static inline bool nfcv_1of256_pause_position(uint8_t sample, uint8_t *position)
{
    switch (sample)
    {
    case 0xbfu:
        *position = 0u;
        return true;
    case 0xefu:
        *position = 1u;
        return true;
    case 0xfbu:
        *position = 2u;
        return true;
    case 0xfeu:
        *position = 3u;
        return true;
    default:
        return false;
    }
}

/* Push one sample after the physical 1-of-256 SOF. */
static inline nfcv_1of256_step_t
nfcv_1of256_decoder_push(nfcv_1of256_decoder_t *decoder, uint8_t sample)
{
    if (decoder == NULL || decoder->invalid || decoder->eof_seen)
    {
        if (decoder != NULL)
        {
            decoder->invalid = true;
        }
        return NFCV_1OF256_STEP_INVALID;
    }

    if (sample == NFCV_1OF256_EOF)
    {
        if (decoder->sample_index != 0u || decoder->pause_seen)
        {
            decoder->invalid = true;
            return NFCV_1OF256_STEP_INVALID;
        }
        decoder->eof_seen = true;
        return NFCV_1OF256_STEP_COMPLETE;
    }

    if (decoder->size_bytes >= NFCV_FRAME_MAX_BYTES)
    {
        decoder->invalid = true;
        return NFCV_1OF256_STEP_INVALID;
    }

    if (sample != NFCV_IDLE_SAMPLE)
    {
        uint8_t position = 0u;
        if (!nfcv_1of256_pause_position(sample, &position) ||
            decoder->pause_seen)
        {
            decoder->invalid = true;
            return NFCV_1OF256_STEP_INVALID;
        }
        decoder->bytes[decoder->size_bytes] =
            (uint8_t)(decoder->sample_index * NFCV_RAW_POSITIONS_PER_BYTE +
                      position);
        decoder->pause_seen = true;
    }

    if (decoder->sample_index == NFCV_1OF256_RAW_BYTES_PER_DATA_BYTE - 1u)
    {
        if (!decoder->pause_seen)
        {
            decoder->invalid = true;
            return NFCV_1OF256_STEP_INVALID;
        }
        decoder->size_bytes++;
        decoder->sample_index = 0u;
        decoder->pause_seen = false;
    }
    else
    {
        decoder->sample_index++;
    }

    return NFCV_1OF256_STEP_CONTINUE;
}

/* Decodes one stopped PIO sample buffer, including fixed idle after EOF. */
bool nfcv_rx_decode_samples(const uint8_t *samples,
                            uint16_t sample_count,
                            nfcv_frame_t *frame);

/* Finalizes a streaming 1-of-256 capture without retaining raw samples. */
bool nfcv_1of256_decoder_finish(const nfcv_1of256_decoder_t *decoder,
                                nfcv_frame_t *frame);
