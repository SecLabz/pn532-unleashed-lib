#include "cards/mifare/reader/mifare_reader.h"

#include "common/crypto1.h"
#include "common/endian.h"
#include "pn532_unleashed/integrity.h"
#include "pn532_unleashed/nfca_frame.h"

#include <string.h>

void mifare_reader_command(reader_frame_t *tx, uint8_t opcode, uint8_t block)
{
    (void)reader_frame_begin(tx, MIFARE_COMMAND_BYTES + NFC_CRC_BYTES);
    tx->byte_parity = true;
    tx->data[0] = opcode;
    tx->data[1] = block;
    nfca_append_crc(tx->data, MIFARE_COMMAND_BYTES);
}

void mifare_reader_encrypt(crypto1_state_t *cipher,
                           reader_frame_t *frame,
                           uint8_t input_size_bytes)
{
    frame->supplied_parity = true;
    for (uint8_t byte = 0u; byte < frame->length; byte++)
    {
        const uint8_t plain = frame->data[byte];
        frame->data[byte] ^=
            crypto1_byte(cipher, byte < input_size_bytes ? plain : 0u, false);
        frame->parity[byte] =
            nfca_odd_parity_bit(plain) ^ crypto1_filter(cipher);
    }
}

static nfc_status_t decrypt(crypto1_state_t *cipher,
                            nfca_pcd_response_t *rx,
                            uint8_t expected_size_bytes)
{
    if (rx->size_bits == NFCA_SHORT_RESPONSE_BITS)
    {
        return NFC_ACCESS_DENIED;
    }
    if (rx->size_bits != expected_size_bytes * 8u)
    {
        return NFC_RF_ERROR;
    }
    for (uint8_t byte = 0u; byte < expected_size_bytes; byte++)
    {
        rx->data[byte] ^= crypto1_byte(cipher, 0u, false);
        if (rx->parity[byte] !=
            (nfca_odd_parity_bit(rx->data[byte]) ^ crypto1_filter(cipher)))
        {
            return NFC_RF_ERROR;
        }
    }
    return NFC_OK;
}

static nfc_status_t authentication_transfer(const reader_frame_t *tx,
                                            nfca_pcd_response_t *rx,
                                            uint32_t deadline_ticks,
                                            uint8_t opcode)
{
    if (deadline_ticks != 0u ||
        (opcode != MIFARE_CMD_AUTH_A && opcode != MIFARE_CMD_AUTH_B))
    {
        return nfca_reader_transfer_at(
            tx, rx, MIFARE_NONCE_BYTES * 8u, deadline_ticks);
    }
    /* Classic authentication parts 1 and 2 allow 1 ms to start a reply
     * (NXP MF1S50YYX/V1, table 21). Type 2's 5 ms wait is unrelated. */
    /* 66 us plus the encoder's 352/fc carrier guard exceeds 1172/fc
     * (86.43 us), even without counting post-EOF capture or CPU work. */
    return nfca_reader_transfer_timed(
        tx, rx, MIFARE_NONCE_BYTES * 8u, 1000u, 66u);
}

nfc_status_t mifare_reader_authenticate(const nfca_tag_t *tag,
                                        uint8_t block,
                                        bool key_b,
                                        const uint8_t key[MIFARE_KEY_BYTES],
                                        crypto1_state_t *cipher,
                                        uint32_t *first_nonce,
                                        uint32_t deadline_ticks,
                                        uint32_t reader_nonce)
{
    return mifare_reader_authenticate_opcode(tag,
                                             block,
                                             key_b ? MIFARE_CMD_AUTH_B
                                                   : MIFARE_CMD_AUTH_A,
                                             key,
                                             cipher,
                                             first_nonce,
                                             deadline_ticks,
                                             reader_nonce);
}

nfc_status_t
mifare_reader_authenticate_opcode(const nfca_tag_t *tag,
                                  uint8_t block,
                                  uint8_t opcode,
                                  const uint8_t key[MIFARE_KEY_BYTES],
                                  crypto1_state_t *cipher,
                                  uint32_t *first_nonce,
                                  uint32_t deadline_ticks,
                                  uint32_t reader_nonce)
{
    /* Entropy/table initialization must precede AUTH: the nonce challenge
     * starts a bounded card deadline, independent of host or transport. */
    crypto1_prepare();
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    mifare_reader_command(&tx, opcode, block);
    nfc_status_t status =
        authentication_transfer(&tx, &rx, deadline_ticks, opcode);
    if (status != NFC_OK)
    {
        return status;
    }
    if (rx.size_bits == NFCA_SHORT_RESPONSE_BITS)
    {
        return NFC_ACCESS_DENIED;
    }
    if (rx.size_bits != MIFARE_NONCE_BYTES * 8u || !rx.parity_valid)
    {
        return NFC_RF_ERROR;
    }
    const uint32_t nonce = read_be32(rx.data);
    if (first_nonce != NULL)
    {
        *first_nonce = nonce;
    }
    const uint32_t uid =
        read_be32(&tag->uid[tag->uid_size_bytes - NFCA_UID_FRAGMENT_BYTES]);
    crypto1_init(cipher, crypto1_key_from_bytes(key));
    (void)crypto1_word(cipher, uid ^ nonce, false);
    (void)reader_frame_begin(&tx, MIFARE_READER_AUTH_BYTES);
    tx.byte_parity = true;
    write_be32(tx.data, reader_nonce);
    write_be32(&tx.data[MIFARE_NONCE_BYTES],
               crypto1_prng_successor(nonce, MIFARE_READER_ANSWER_PRNG_STEPS));
    mifare_reader_encrypt(cipher, &tx, MIFARE_NONCE_BYTES);
    status = authentication_transfer(
        &tx, &rx, deadline_ticks != 0u ? deadline_ticks + 2048u : 0u, opcode);
    if (status != NFC_OK)
    {
        return status;
    }
    status = decrypt(cipher, &rx, MIFARE_NONCE_BYTES);
    if (status != NFC_OK)
    {
        return status;
    }
    return read_be32(rx.data) ==
                   crypto1_prng_successor(nonce, MIFARE_CARD_ANSWER_PRNG_STEPS)
               ? NFC_OK
               : NFC_ACCESS_DENIED;
}

nfc_status_t mifare_reader_block(crypto1_state_t *cipher,
                                 uint8_t block,
                                 uint8_t output[MIFARE_BLOCK_SIZE])
{
    return mifare_reader_block_opcode(cipher, block, MIFARE_CMD_READ, output);
}

nfc_status_t mifare_reader_block_opcode(crypto1_state_t *cipher,
                                        uint8_t block,
                                        uint8_t opcode,
                                        uint8_t output[MIFARE_BLOCK_SIZE])
{
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    mifare_reader_command(&tx, opcode, block);
    mifare_reader_encrypt(cipher, &tx, 0u);
    nfc_status_t status =
        nfca_reader_transfer(&tx, &rx, MIFARE_BLOCK_FRAME_BYTES * 8u);
    if (status == NFC_OK)
    {
        status = decrypt(cipher, &rx, MIFARE_BLOCK_FRAME_BYTES);
    }
    if (status == NFC_OK && !nfca_check_crc(rx.data, MIFARE_BLOCK_FRAME_BYTES))
    {
        status = NFC_RF_ERROR;
    }
    if (status == NFC_OK)
    {
        memcpy(output, rx.data, MIFARE_BLOCK_SIZE);
    }
    return status;
}
