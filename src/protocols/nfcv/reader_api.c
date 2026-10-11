#include "api_internal.h"
#include "pn532_unleashed/nfcv_reader.h"
#include "protocols/nfcv/reader.h"

#include <string.h>

_Static_assert(NFCV_READER_TX_MAX_BYTES == READER_FRAME_MAX_BYTES,
               "Public NFC-V TX must match physical capacity");
_Static_assert(NFCV_READER_RX_MAX_BYTES <= NFCV_PCD_RX_MAX_BYTES,
               "Public NFC-V RX must fit physical capacity");

nfc_status_t nfcv_reader_start(void)
{
    const nfc_status_t status = nfc_session_begin(NFC_STATE_READING_NFCV);
    if (status != NFC_OK)
    {
        return status;
    }
    if (nfcv_reader_open(true) != NFC_OK)
    {
        (void)nfc_stop();
        nfc_session_error();
        return NFC_RF_ERROR;
    }
    return NFC_OK;
}

/* Discovery needs a distinct silence result without expiring the session. */
static nfc_status_t exchange(const nfcv_tx_frame_t *tx,
                             nfcv_rx_frame_t *rx,
                             const reader_options_t *options,
                             bool discovery)
{
    if (rx == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    rx->size_bytes = 0u;
    rx->crc_valid = false;
    uint32_t timeout_us;
    bool expect_response;
    if (tx == NULL || tx->data == NULL || tx->size_bytes < 2u ||
        tx->size_bytes > NFCV_READER_TX_MAX_BYTES ||
        !reader_options_resolve(options,
                                NFCV_READER_SOF_TIMEOUT_US,
                                &timeout_us,
                                &expect_response) ||
        (expect_response && (rx->data == NULL || rx->capacity_bytes == 0u)))
    {
        return NFC_INVALID_ARGUMENT;
    }
    const uint8_t flags = tx->data[0];
    if ((flags & NFCV_REQ_DATARATE_HIGH) == 0u ||
        (flags & (NFCV_REQ_SUBCARRIER_TWO | NFCV_REQ_PROTOCOL_EXT)) != 0u)
    {
        return NFC_UNSUPPORTED;
    }
    reader_frame_t frame;
    (void)reader_frame_begin(&frame, tx->size_bytes);
    memcpy(frame.data, tx->data, tx->size_bytes);
    const nfc_status_t ready = nfc_session_require(NFC_STATE_READING_NFCV);
    if (ready != NFC_OK)
    {
        return ready;
    }
    nfcv_pcd_rx_result_t response;
    nfc_status_t status = nfcv_reader_transfer_frame(
        &frame, &response, timeout_us, expect_response);
    if (status == NFC_NO_RESPONSE && discovery)
    {
        status = NFC_NO_TAG;
    }
    if (status != NFC_OK || !expect_response)
    {
        return nfc_session_result(status);
    }
    if (response.size_bytes > NFCV_READER_RX_MAX_BYTES ||
        response.size_bytes > rx->capacity_bytes)
    {
        return NFC_BUFFER_TOO_SMALL;
    }
    memcpy(rx->data, response.bytes, response.size_bytes);
    rx->size_bytes = response.size_bytes;
    rx->crc_valid = response.crc_ok;
    return NFC_OK;
}

nfc_status_t nfcv_reader_exchange_frame(const nfcv_tx_frame_t *tx,
                                        nfcv_rx_frame_t *rx,
                                        const reader_options_t *options)
{
    return exchange(tx, rx, options, false);
}

static nfc_status_t exchange_bytes(const uint8_t *command,
                                   size_t command_size_bytes,
                                   nfcv_reader_reply_t *reply,
                                   const reader_options_t *options,
                                   bool discovery)
{
    if (reply == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    reply->size_bytes = 0u;
    if (command == NULL || command_size_bytes < 2u ||
        command_size_bytes > NFCV_READER_TX_MAX_BYTES - NFC_CRC_BYTES)
    {
        return NFC_INVALID_ARGUMENT;
    }
    uint32_t timeout_us;
    bool expect_response;
    if (!reader_options_resolve(options,
                                NFCV_READER_SOF_TIMEOUT_US,
                                &timeout_us,
                                &expect_response) ||
        (expect_response &&
         (reply->data == NULL || reply->capacity_bytes == 0u)))
    {
        return NFC_INVALID_ARGUMENT;
    }
    uint8_t bytes[NFCV_READER_TX_MAX_BYTES];
    memcpy(bytes, command, command_size_bytes);
    nfcv_append_crc(bytes, command_size_bytes);
    const nfcv_tx_frame_t tx = {
        .data = bytes,
        .size_bytes = command_size_bytes + NFC_CRC_BYTES,
    };
    uint8_t data[NFCV_READER_RX_MAX_BYTES];
    nfcv_rx_frame_t rx = {.data = data, .capacity_bytes = sizeof(data)};
    const nfc_status_t status = exchange(&tx, &rx, options, discovery);
    if (status != NFC_OK || !expect_response)
    {
        return status;
    }
    if (rx.size_bytes <= NFC_CRC_BYTES || !rx.crc_valid)
    {
        return nfc_session_result(NFC_INVALID_RESPONSE);
    }
    const size_t payload_bytes = rx.size_bytes - NFC_CRC_BYTES;
    if (payload_bytes > reply->capacity_bytes)
    {
        return NFC_BUFFER_TOO_SMALL;
    }
    memcpy(reply->data, data, payload_bytes);
    reply->size_bytes = payload_bytes;
    return NFC_OK;
}

nfc_status_t nfcv_reader_exchange(const uint8_t *command,
                                  size_t command_size_bytes,
                                  nfcv_reader_reply_t *reply,
                                  const reader_options_t *options)
{
    return exchange_bytes(command, command_size_bytes, reply, options, false);
}

nfc_status_t nfcv_reader_inventory(nfcv_identity_t *tag)
{
    if (tag == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const uint8_t command[] = {
        NFCV_REQ_DATARATE_HIGH | NFCV_REQ_INVENTORY | NFCV_REQ_SLOT1,
        NFCV_CMD_INVENTORY,
        0u,
    };
    uint8_t data[2u + NFCV_UID_SIZE];
    nfcv_reader_reply_t reply = {
        .data = data,
        .capacity_bytes = sizeof(data),
    };
    const nfc_status_t status =
        exchange_bytes(command, sizeof(command), &reply, NULL, true);
    if (status != NFC_OK)
    {
        return status;
    }
    if (reply.size_bytes != sizeof(data) || data[0] != NFCV_RES_OK)
    {
        return nfc_session_result(NFC_INVALID_RESPONSE);
    }
    nfcv_identity_t identity = {.dsfid = data[1]};
    nfcv_uid_to_wire(identity.uid, &data[2]);
    *tag = identity;
    return NFC_OK;
}
