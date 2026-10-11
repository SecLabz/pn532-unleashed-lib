#pragma once

#include "phy/pcd/reader_subcarrier_level_rx.h"
#include "phy/reader_frame.h"

/* READ_SIG is the largest supported response: 32 data bytes plus CRC-A. */
#define NFCA_PCD_RX_MAX_BYTES 34u
#define NFCA_PCD_RX_SAMPLES_PER_HALF 64u
#define NFCA_PCD_RX_WORDS_PER_HALF                                             \
    (NFCA_PCD_RX_SAMPLES_PER_HALF / READER_RX_SAMPLES_PER_WORD)
#define NFCA_PCD_RX_BOUNDARY_HALVES 6u
#define NFCA_PCD_RX_HALVES                                                     \
    (2u * 9u * NFCA_PCD_RX_MAX_BYTES + NFCA_PCD_RX_BOUNDARY_HALVES)

typedef struct
{
    uint8_t data[NFCA_PCD_RX_MAX_BYTES];
    uint8_t parity[NFCA_PCD_RX_MAX_BYTES];
    uint16_t size_bits;
    bool framing_valid;
    bool parity_valid;
    bool collision;
    bool sof_seen;
} nfca_pcd_response_t;

bool nfca_pcd_init(void);
void nfca_pcd_release(void);
/* Capture with IRQs masked, decode after restore. Output is always reset.
 * Full capture buffers are accepted only when the decoder sees physical EOF;
 * a stalled PIO cannot represent the fixed grid and is always rejected. */
/* Returns whether TX and capture completed without a hardware fault.
 * Tag silence is a completed capture; RX framing is reported independently. */
bool nfca_pcd_exchange(const reader_frame_t *frame,
                       nfca_pcd_response_t *response,
                       uint16_t max_bits);
bool nfca_pcd_exchange_timed(const reader_frame_t *frame,
                             nfca_pcd_response_t *response,
                             uint16_t max_bits,
                             uint32_t sof_timeout_us);
/* Pure fixed-grid decoder. Samples start immediately after the first rise. */
void nfca_pcd_decode(const uint32_t *words,
                     uint16_t sample_count,
                     uint16_t max_bits,
                     nfca_pcd_response_t *response);

/* Same physical grid/decoder; deadline is in fc/16 recovery-clock ticks.
 * Zero means immediate TX. No parity interpretation is added to raw RX. */
bool nfca_pcd_exchange_at(const reader_frame_t *frame,
                          nfca_pcd_response_t *response,
                          uint16_t max_bits,
                          uint32_t deadline_ticks);
bool nfca_pcd_send(const reader_frame_t *frame);
