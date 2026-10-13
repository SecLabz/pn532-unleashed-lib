#pragma once

#include "phy/nfcv/pcd/nfcv_pcd_rx.h"

#include <stdbool.h>
#include <stdint.h>

#define NFCV_PCD_MAX_SYMBOLS 640u
/* Decodes recovered subcarrier levels sampled on the fixed 8/fc grid. */
bool nfcv_pcd_decode_level_samples(const uint32_t *sample_words,
                                   uint16_t sample_count,
                                   nfcv_pcd_rx_result_t *result);

/* Shared physical decoding for bounded passive responses. Exclusive RF owner;
 * internal scratch is reused, no pointers retained. All counts are samples
 * on the fixed 8/fc grid; data capacity includes CRC. */
typedef struct
{
    uint16_t size_bytes;
    uint32_t start_sample;
    uint32_t end_sample;
    bool sof_seen, eof_seen, crc_ok, framing_valid, truncated;
} nfcv_level_result_t;
bool nfcv_decode_levels(const uint32_t *words,
                        uint32_t count,
                        uint8_t *data,
                        uint16_t capacity,
                        nfcv_level_result_t *result);

/* Qualify the first SOF on this fixed grid; no output changes on failure.
 * Caller supplies bounded level samples and a non-null start output. */
bool nfcv_find_sof(const uint32_t *words, uint32_t count, uint32_t *start);
