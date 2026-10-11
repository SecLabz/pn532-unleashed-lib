#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NFCV_PCD_RX_MAX_BYTES 32u

typedef struct
{
    uint8_t bytes[NFCV_PCD_RX_MAX_BYTES];
    uint8_t size_bytes;
    bool sof_seen;
    bool eof_seen;
    bool crc_ok;
    bool framing_valid;
    bool truncated;
} nfcv_pcd_rx_result_t;

/*
 * Initializes the NFC-V reader response capture path.
 */
bool nfcv_pcd_rx_init(void);

/*
 * Captures and decodes one physical NFC-V response.
 * Returns true when a CRC-valid frame was decoded.
 */
bool nfcv_pcd_rx_capture(nfcv_pcd_rx_result_t *result);
/* Physical completion only; CRC validity is reported independently. */
bool nfcv_pcd_rx_capture_timed(nfcv_pcd_rx_result_t *result,
                               uint32_t sof_timeout_us);
