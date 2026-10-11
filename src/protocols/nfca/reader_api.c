#include "api_internal.h"
#include "pn532_unleashed/nfca_reader.h"
#include "protocols/nfca/reader.h"

#include <string.h>

_Static_assert(NFCA_READER_TX_MAX_BYTES == READER_FRAME_MAX_BYTES,
               "Public NFC-A TX must match physical capacity");
_Static_assert(NFCA_READER_RX_MAX_BYTES == NFCA_PCD_RX_MAX_BYTES,
               "Public NFC-A RX must match physical capacity");

nfc_status_t nfca_reader_start(void)
{
    const nfc_status_t status = nfc_session_begin(NFC_STATE_READING_NFCA);
    if (status != NFC_OK)
    {
        return status;
    }
    if (nfca_reader_open() != NFC_OK)
    {
        (void)nfc_stop();
        nfc_session_error();
        return NFC_RF_ERROR;
    }
    return NFC_OK;
}

nfc_status_t nfca_reader_select(nfca_tag_t *tag)
{
    if (tag == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t status = nfc_session_require(NFC_STATE_READING_NFCA);
    if (status != NFC_OK)
    {
        return status;
    }
    return nfc_session_result(nfca_reader_select_tag(tag));
}

nfc_status_t nfca_reader_exchange_frame(const nfca_tx_frame_t *tx,
                                        nfca_rx_frame_t *rx,
                                        const reader_options_t *options)
{
    if (rx == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    rx->size_bits = 0u;
    rx->parity_valid = false;
    uint32_t timeout_us;
    bool expect_response;
    if (tx == NULL || tx->data == NULL || tx->size_bits == 0u ||
        tx->size_bits > NFCA_READER_TX_MAX_BYTES * 8u)
    {
        return NFC_INVALID_ARGUMENT;
    }
    if ((tx->parity != NFCA_PARITY_AUTO && tx->parity != NFCA_PARITY_PROVIDED &&
         tx->parity != NFCA_PARITY_NONE) ||
        (tx->parity == NFCA_PARITY_PROVIDED && tx->size_bits >= 8u &&
         tx->parity_bits == NULL))
    {
        return NFC_INVALID_ARGUMENT;
    }
    if (!reader_options_resolve(options,
                                NFCA_READER_SOF_TIMEOUT_US,
                                &timeout_us,
                                &expect_response) ||
        (expect_response && (rx->data == NULL || rx->capacity_bytes == 0u)))
    {
        return NFC_INVALID_ARGUMENT;
    }
    reader_frame_t frame;
    (void)reader_frame_begin(&frame, (tx->size_bits + 7u) / 8u);
    memcpy(frame.data, tx->data, frame.length);
    frame.last_byte_bits = (uint8_t)(tx->size_bits % 8u);
    frame.byte_parity = tx->parity != NFCA_PARITY_NONE;
    frame.supplied_parity = tx->parity == NFCA_PARITY_PROVIDED;
    if (frame.supplied_parity)
    {
        for (uint16_t byte = 0u; byte < tx->size_bits / 8u; byte++)
        {
            if (tx->parity_bits[byte] > 1u)
            {
                return NFC_INVALID_ARGUMENT;
            }
            frame.parity[byte] = tx->parity_bits[byte];
        }
    }
    const nfc_status_t ready = nfc_session_require(NFC_STATE_READING_NFCA);
    if (ready != NFC_OK)
    {
        return ready;
    }
    nfca_pcd_response_t response;
    const nfc_status_t status = nfca_reader_transfer_frame(
        &frame, &response, timeout_us, expect_response);
    if (status != NFC_OK || !expect_response)
    {
        return nfc_session_result(status);
    }
    const size_t size_bytes = (response.size_bits + 7u) / 8u;
    const size_t parity_bytes = response.size_bits / 8u;
    if (size_bytes > rx->capacity_bytes ||
        (rx->parity_bits != NULL && parity_bytes > rx->parity_capacity_bytes))
    {
        return NFC_BUFFER_TOO_SMALL;
    }
    memcpy(rx->data, response.data, size_bytes);
    if (rx->parity_bits != NULL)
    {
        memcpy(rx->parity_bits, response.parity, parity_bytes);
    }
    rx->size_bits = response.size_bits;
    rx->parity_valid = response.parity_valid;
    return NFC_OK;
}

nfc_status_t nfca_reader_exchange(const uint8_t *command,
                                  size_t command_size_bytes,
                                  nfca_reader_reply_t *reply,
                                  const reader_options_t *options)
{
    if (reply == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    reply->size_bytes = 0u;
    reply->is_short4 = false;
    reply->short4 = 0u;
    if (command == NULL || command_size_bytes == 0u ||
        command_size_bytes > NFCA_READER_TX_MAX_BYTES - NFC_CRC_BYTES)
    {
        return NFC_INVALID_ARGUMENT;
    }
    uint32_t timeout_us;
    bool expect_response;
    if (!reader_options_resolve(options,
                                NFCA_READER_SOF_TIMEOUT_US,
                                &timeout_us,
                                &expect_response) ||
        (expect_response &&
         (reply->data == NULL || reply->capacity_bytes == 0u)))
    {
        return NFC_INVALID_ARGUMENT;
    }
    uint8_t bytes[NFCA_READER_TX_MAX_BYTES];
    memcpy(bytes, command, command_size_bytes);
    nfca_append_crc(bytes, command_size_bytes);
    const nfca_tx_frame_t tx = {
        .data = bytes,
        .size_bits = (uint16_t)((command_size_bytes + NFC_CRC_BYTES) * 8u),
        .parity = NFCA_PARITY_AUTO,
    };
    uint8_t data[NFCA_READER_RX_MAX_BYTES];
    nfca_rx_frame_t rx = {.data = data, .capacity_bytes = sizeof(data)};
    const nfc_status_t status = nfca_reader_exchange_frame(&tx, &rx, options);
    if (status != NFC_OK || !expect_response)
    {
        return status;
    }
    if (rx.size_bits == NFCA_SHORT_RESPONSE_BITS)
    {
        reply->is_short4 = true;
        reply->short4 = data[0] & NFCA_SHORT_RESPONSE_MASK;
        return NFC_OK;
    }
    const size_t size_bytes = rx.size_bits / 8u;
    if (rx.size_bits % 8u != 0u || !rx.parity_valid ||
        size_bytes <= NFC_CRC_BYTES || !nfca_check_crc(data, size_bytes))
    {
        return nfc_session_result(NFC_INVALID_RESPONSE);
    }
    const size_t payload_bytes = size_bytes - NFC_CRC_BYTES;
    if (payload_bytes > reply->capacity_bytes)
    {
        return NFC_BUFFER_TOO_SMALL;
    }
    memcpy(reply->data, data, payload_bytes);
    reply->size_bytes = payload_bytes;
    return NFC_OK;
}
