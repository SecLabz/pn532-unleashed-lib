#pragma once

#include <stdbool.h>
#include <stdint.h>

#define READER_RX_SAMPLES_PER_WORD 32u
#define READER_RX_NFCA_SAMPLE_FC 1u
#define READER_RX_NFCV_SAMPLE_FC 8u

typedef struct
{
    uint16_t word_count;
    uint32_t first_edge_us;
    uint32_t capture_us;
    bool truncated;
    bool overflowed;
} reader_subcarrier_level_rx_stats_t;

/* The selected grid remains fixed throughout capture. NFC-V uses 8/fc;
 * NFC-A uses 1/fc so narrow subcarrier pulses retain their first edge. */
bool reader_subcarrier_level_rx_init(uint8_t sample_fc);

/* Stops the shared RX state machine and releases the level RX program. */
void reader_subcarrier_level_rx_release(void);

void reader_subcarrier_level_rx_capture(
    uint32_t *sample_words,
    uint16_t word_capacity,
    uint32_t blank_us,
    uint32_t sof_timeout_us,
    uint32_t window_us,
    reader_subcarrier_level_rx_stats_t *stats);
