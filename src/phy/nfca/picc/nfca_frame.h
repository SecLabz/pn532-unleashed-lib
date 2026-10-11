#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NFCA_FRAME_MAX_BYTES 32u

#include "pn532_unleashed/nfca_frame.h"

/*
 * Reader modulation is reconstructed on the 8/fc grid. This is the coarsest
 * grid that still preserves the half-bit phase of Modified Miller pauses.
 * Four grid slots form one decoder quarter and sixteen form one data bit.
 */
#define NFCA_RX_SAMPLES_PER_WORD 32u
#define NFCA_RX_BITS_PER_BYTE_WITH_PARITY 9u
#define NFCA_RX_TAIL_SLOTS 64u
#define NFCA_RX_BIT_SLOTS 16u
#define NFCA_RX_DATA_START_SLOT 15u
#define NFCA_RX_EOF_GAP_SLOTS 39u
#define NFCA_RX_MAX_BITS                                                       \
    (NFCA_FRAME_MAX_BYTES * NFCA_RX_BITS_PER_BYTE_WITH_PARITY)
#define NFCA_RX_MAX_SLOTS                                                      \
    (NFCA_RX_DATA_START_SLOT + NFCA_RX_MAX_BITS * NFCA_RX_BIT_SLOTS +          \
     NFCA_RX_TAIL_SLOTS)

typedef struct
{
    uint8_t bytes[NFCA_FRAME_MAX_BYTES];
    uint8_t parity[NFCA_FRAME_MAX_BYTES];
    uint16_t size_bits;
    uint16_t size_bytes;
    bool short_frame;
    bool parity_ok;
    bool crc_ok;
} nfca_frame_t;

/*
 * Returns the ISO odd-parity bit for value.
 */

/* Adds up to 32 chronological 8/fc level samples to quarter masks. */
void nfca_quarters_add_level_samples(uint8_t *bit_quarter_masks,
                                     uint16_t first_slot,
                                     uint32_t level_samples,
                                     uint8_t sample_count);

/* Six bytes of cursor/CRC state; reuses the existing masks and frame buffer.
 * slot_count is the exclusive end of captured slots, increasing with each
 * consume call. Only complete bytes (including parity) are committed. Call
 * finish once at EOF; its slot_count may trim the last sampled FIFO word.
 * Invalid symbols stop consumption at the longest valid byte prefix, as in
 * the batch decoder. Command dispatch remains after finish, never consume. */
typedef struct
{
    uint16_t next_end_slot;
    uint16_t crc;
    bool previous_one;
    bool failed;
} nfca_frame_stream_t;

void nfca_frame_stream_init(nfca_frame_stream_t *stream, nfca_frame_t *out);
void nfca_frame_stream_consume(nfca_frame_stream_t *stream,
                               const uint8_t *bit_quarter_masks,
                               uint16_t slot_count,
                               nfca_frame_t *out);
bool nfca_frame_stream_finish(nfca_frame_stream_t *stream,
                              const uint8_t *bit_quarter_masks,
                              uint16_t slot_count,
                              nfca_frame_t *out);
