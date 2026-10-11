#pragma once

#include "pn532_unleashed/nfca.h"
#include "pn532_unleashed/reader.h"

#include <stdbool.h>

#define NFCA_READER_TX_MAX_BYTES 32u
#define NFCA_READER_RX_MAX_BYTES 34u
#define NFCA_READER_SOF_TIMEOUT_US 5000u

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        NFCA_PARITY_AUTO = 0,
        NFCA_PARITY_PROVIDED,
        NFCA_PARITY_NONE,
    } nfca_parity_t;

    /* Exact LSB-first bits, including any CRC. 1..256 bits; parity is added
     * after complete bytes only. PROVIDED borrows one 0/1 byte per complete
     * data byte, never a packed bitset. NONE supports short/partial requests.
     * Inputs are borrowed for the call and may alias RX storage. */
    typedef struct
    {
        const uint8_t *data;
        uint16_t size_bits;
        nfca_parity_t parity;
        const uint8_t *parity_bits;
    } nfca_tx_frame_t;

    /* Caller-owned buffers. RX keeps CRC and exact bit count. Optional parity
     * output has one 0/1 byte per complete data byte. No pointers are retained.
     * parity_valid observes plaintext odd parity, not encrypted parity.
     * Complete physical frames may have invalid parity/CRC; raw callers own
     * those checks. Decoder always expects the NFC-A tag parity positions.
     * On failure size_bits is zero and buffers are unchanged. NONE also resets
     * size_bits to zero. A small output is not permission to retry a command.
     */
    typedef struct
    {
        uint8_t *data;
        size_t capacity_bytes;
        uint8_t *parity_bits;
        size_t parity_capacity_bytes;
        uint16_t size_bits;
        bool parity_valid;
    } nfca_rx_frame_t;

    typedef struct
    {
        uint8_t *data;
        size_t capacity_bytes;
        size_t size_bytes;
        bool is_short4;
        uint8_t short4;
    } nfca_reader_reply_t;

    /* One WUPA activation (4/7/10-byte UID), no collision-tree walk. Requires
     * this public reader session. Output unchanged on failure. NO_TAG keeps
     * the session open; malformed selection enters ERROR. */
    nfc_status_t nfca_reader_select(nfca_tag_t *tag);

    nfc_status_t nfca_reader_exchange_frame(const nfca_tx_frame_t *tx,
                                            nfca_rx_frame_t *rx,
                                            const reader_options_t *options);

    /* Plain byte command, 1..30 bytes excluding CRC. Add CRC-A and odd parity;
     * validate/strip RX CRC and parity, or return a four-bit ACK/NACK. A byte
     * reply has at most 32 payload bytes. reply control fields reset on failure
     * and payload is unchanged. Buffers are borrowed only for this call. */
    nfc_status_t nfca_reader_exchange(const uint8_t *command,
                                      size_t command_size_bytes,
                                      nfca_reader_reply_t *reply,
                                      const reader_options_t *options);

#ifdef __cplusplus
}
#endif
