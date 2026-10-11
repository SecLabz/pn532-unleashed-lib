#include "cards/mifare/reader/mifare_reader.h"
#include "pn532_unleashed/integrity.h"

#include <string.h>

static nfc_status_t encrypted_ack(crypto1_state_t *cipher, reader_frame_t *tx)
{
    nfca_pcd_response_t rx;
    mifare_reader_encrypt(cipher, tx, 0u);
    const nfc_status_t status = nfca_reader_transfer(tx, &rx, 4u);
    if (status != NFC_OK)
    {
        return status;
    }
    if (rx.size_bits != 4u || !rx.framing_valid || rx.collision)
    {
        return NFC_INVALID_RESPONSE;
    }
    for (uint8_t bit = 0u; bit < 4u; bit++)
    {
        rx.data[0] ^= crypto1_bit(cipher, 0u, false) << bit;
    }
    return (rx.data[0] & 0x0fu) == MIFARE_ACK ? NFC_OK : NFC_ACCESS_DENIED;
}

nfc_status_t mifare_reader_write_opcode(crypto1_state_t *cipher,
                                        uint8_t block,
                                        uint8_t opcode,
                                        const uint8_t data[16])
{
    if (opcode != MIFARE_CMD_WRITE && opcode != 0xa8u)
    {
        return NFC_INVALID_ARGUMENT;
    }
    reader_frame_t tx;
    mifare_reader_command(&tx, opcode, block);
    nfc_status_t status = encrypted_ack(cipher, &tx);
    if (status != NFC_OK)
    {
        return status;
    }
    (void)reader_frame_begin(&tx, 18u);
    memcpy(tx.data, data, 16u);
    nfca_append_crc(tx.data, 16u);
    tx.byte_parity = true;
    status = encrypted_ack(cipher, &tx);
    return status == NFC_OK ? NFC_OK : NFC_WRITE_OUTCOME_UNKNOWN;
}
