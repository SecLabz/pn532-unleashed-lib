#pragma once

#include "pn532_unleashed/nfcv_frame.h"

/* Downlink (PCD to VICC) SOF/EOF patterns. */
#define NFCV_1OF4_SOF 0x7bu
#define NFCV_1OF256_SOF 0x7eu
#define NFCV_1OF4_EOF 0xdfu
#define NFCV_1OF256_EOF NFCV_1OF4_EOF

/*
 * At the fixed 128/fc sampler period, one 1-of-256 data byte spans 256
 * positions. Each raw sample byte covers four positions, so 64 raw bytes
 * are needed for one decoded byte.
 */
#define NFCV_1OF4_SYMBOLS_PER_BYTE 4u
#define NFCV_RAW_POSITIONS_PER_BYTE 4u
#define NFCV_1OF256_RAW_BYTES_PER_DATA_BYTE 64u
#define NFCV_IDLE_SAMPLE 0xffu
#define NFCV_REQUEST_MIN_BYTES (2u + NFCV_CRC_SIZE)
#define NFCV_CAPTURE_MIN_SAMPLES 3u

/* Uplink (VICC to PCD) SOF/EOF subcarrier symbol patterns */
#define NFCV_UPLINK_SOF 0x1du
#define NFCV_UPLINK_EOF 0xb8u

/*
 * Decodes one 1-out-of-4 symbol into two data bits.
 */
bool nfcv_decode_1of4_sample(uint8_t sample, uint8_t *two_bits);

/*
 * Returns true when sample is the NFC-V start-of-frame pattern.
 */
bool nfcv_sample_is_sof(uint8_t sample);

/*
 * Returns true when sample is the NFC-V end-of-frame pattern.
 */
bool nfcv_sample_is_eof(uint8_t sample);
