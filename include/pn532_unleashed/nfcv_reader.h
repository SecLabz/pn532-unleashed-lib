#pragma once

#include "pn532_unleashed/nfcv_frame.h"
#include "pn532_unleashed/reader.h"

#include <stdbool.h>

#define NFCV_READER_TX_MAX_BYTES 32u
/* Conservative physical limit within the existing 8 ms capture window,
 * including response delay, framing and two CRC bytes. A larger caller buffer
 * does not extend capture. Long reads must be split into bounded commands. */
#define NFCV_READER_RX_MAX_BYTES 24u
#define NFCV_READER_SOF_TIMEOUT_US 8000u

#ifdef __cplusplus
extern "C"
{
#endif

    /* Inventory observations only. Display-order UID; no model or system
     * information is inferred. This is independent of SLIX dump geometry. */
    typedef struct
    {
        uint8_t uid[NFCV_UID_SIZE];
        uint8_t dsfid;
    } nfcv_identity_t;

    typedef struct
    {
        const uint8_t *data;
        size_t size_bytes;
    } nfcv_tx_frame_t;

    /* Raw bytes include flags and CRC. crc_valid is an observation; NFC_OK
     * means a complete physical frame even if CRC is invalid. Caller buffers
     * are borrowed for the call, may alias TX and are unchanged on failure;
     * size_bytes/crc_valid reset on failure and with NONE. */
    typedef struct
    {
        uint8_t *data;
        size_t capacity_bytes;
        size_t size_bytes;
        bool crc_valid;
    } nfcv_rx_frame_t;

    typedef struct
    {
        uint8_t *data;
        size_t capacity_bytes;
        size_t size_bytes;
    } nfcv_reader_reply_t;

    /* One-slot inventory, high-rate single-subcarrier response, no AFI/mask.
     * Output unchanged on failure. NO_TAG keeps this public session open.
     * No automatic GET_SYSTEM_INFO or vendor/geometry restriction. */
    nfc_status_t nfcv_reader_inventory(nfcv_identity_t *tag);

    /* Exact command including flags and CRC. Physical TX is 1-of-4; RX is
     * high-rate single-subcarrier. Unsupported flags return UNSUPPORTED before
     * TX. This API does not implement delayed-EOF exchanges or reader writes.
     * Card commands/addressing and raw CRC checks belong to the caller. */
    nfc_status_t nfcv_reader_exchange_frame(const nfcv_tx_frame_t *tx,
                                            nfcv_rx_frame_t *rx,
                                            const reader_options_t *options);

    /* Command includes flags/opcode/parameters but excludes CRC (2..30 bytes).
     * Adds CRC-V, validates/strips RX CRC, retains response flags including
     * card errors. At most 22 payload bytes. Control fields reset on failure
     * and payload is unchanged. No field toggle or retry between commands. */
    nfc_status_t nfcv_reader_exchange(const uint8_t *command,
                                      size_t command_size_bytes,
                                      nfcv_reader_reply_t *reply,
                                      const reader_options_t *options);

#ifdef __cplusplus
}
#endif
