#pragma once

#include "phy/nfcv/pcd/nfcv_pcd_rx.h"

#include <stdbool.h>
#include <stdint.h>

#define NFCV_PCD_MAX_SYMBOLS 640u
/* Decodes recovered subcarrier levels sampled on the fixed 8/fc grid. */
bool nfcv_pcd_decode_level_samples(const uint32_t *sample_words,
                                   uint16_t sample_count,
                                   nfcv_pcd_rx_result_t *result);
