/* Generation-specific UID mutation paths. */
#include "api_internal.h"
#include "cards/mifare/reader/mifare_reader.h"
#include "pn532_unleashed/integrity.h"
#include "pn532_unleashed/nfca_frame.h"
#include "pn532_unleashed/nfca_mifare_uid.h"
#include "pn532_unleashed/reader.h"

#include "pico/rand.h"
#include "pico/stdlib.h"

#include <string.h>

#define UID_TIMEOUT_US 2000000u

static nfc_status_t
exchange(const uint8_t *data, uint8_t size, nfca_pcd_response_t *rx)
{
    reader_frame_t tx;
    if (!reader_frame_begin(&tx, size + NFC_CRC_BYTES))
    {
        return NFC_INVALID_ARGUMENT;
    }
    memcpy(tx.data, data, size);
    nfca_append_crc(tx.data, size);
    tx.byte_parity = true;
    return nfca_reader_transfer_frame(&tx, rx, UID_TIMEOUT_US, true);
}

static bool valid(const nfca_pcd_response_t *rx, uint8_t size)
{
    return rx->framing_valid && !rx->collision && rx->parity_valid &&
           rx->size_bits == size * 8u && nfca_check_crc(rx->data, size);
}

static nfc_status_t ack(const nfca_pcd_response_t *rx)
{
    return rx->framing_valid && !rx->collision && rx->size_bits == 4u &&
                   (rx->data[0] & 0x0fu) == MIFARE_ACK
               ? NFC_OK
               : NFC_ACCESS_DENIED;
}

static nfc_status_t unlock(uint8_t first, bool second)
{
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    (void)reader_frame_begin(&tx, 1u);
    tx.data[0] = first;
    tx.last_byte_bits = 7u;
    nfc_status_t status = nfca_reader_transfer_frame(&tx, &rx, 5000u, true);
    if (status != NFC_OK || ack(&rx) != NFC_OK)
    {
        return status == NFC_OK ? NFC_ACCESS_DENIED : status;
    }
    if (!second)
    {
        return NFC_OK;
    }
    tx.data[0] = first == 0x20u ? 0x23u : 0x43u;
    tx.last_byte_bits = 0u;
    tx.byte_parity = true;
    status = nfca_reader_transfer_frame(&tx, &rx, 5000u, true);
    return status == NFC_OK ? ack(&rx) : status;
}

/* Preserve both contiguous and split seven-byte layouts. */
static void build_block(const uint8_t old[16],
                        const uint8_t *uid,
                        uint8_t size,
                        bool direct,
                        uint8_t block[16])
{
    memcpy(block, old, 16u);
    if (size == 4u || direct || old[0] != 0x88u ||
        old[4] != (uint8_t)(old[1] ^ old[2] ^ old[3]) ||
        old[9] != (uint8_t)(old[5] ^ old[6] ^ old[7] ^ old[8]))
    {
        memcpy(block, uid, size);
        block[size] = 0u;
        for (uint8_t i = 0u; i < size; i++)
        {
            block[size] ^= uid[i];
        }
        return;
    }
    memcpy(&block[1], uid, 3u);
    block[4] = uid[0] ^ uid[1] ^ uid[2];
    memcpy(&block[5], &uid[3], 4u);
    block[9] = uid[3] ^ uid[4] ^ uid[5] ^ uid[6];
}

static nfc_status_t direct(const mifare_uid_request_t *r)
{
    const uint8_t first = r->method == UID_GDM_20 ? 0x20u : 0x40u;
    const bool second = r->method != UID_GEN1B;
    nfc_status_t status = nfca_reader_restart(&r->expected);
    if (status != NFC_OK)
    {
        return status;
    }
    /* Reset into the backdoor only after checking the selected identity. */
    if (!nfca_reader_stop())
    {
        return NFC_RF_ERROR;
    }
    sleep_us(5000u);
    status = nfca_reader_open();
    if (status == NFC_OK)
    {
        status = unlock(first, second);
    }
    if (status != NFC_OK)
    {
        return status;
    }
    nfca_pcd_response_t rx;
    status = exchange((const uint8_t[]){MIFARE_CMD_READ, 0u}, 2u, &rx);
    if (status != NFC_OK || !valid(&rx, 18u))
    {
        return status == NFC_OK ? NFC_INVALID_RESPONSE : status;
    }
    uint8_t block[16];
    build_block(rx.data, r->uid, r->expected.uid_size_bytes, true, block);
    /* Keep this selected backdoor session through read and write. */
    status = exchange((const uint8_t[]){MIFARE_CMD_WRITE, 0u}, 2u, &rx);
    if (status != NFC_OK || ack(&rx) != NFC_OK)
    {
        return status == NFC_OK ? NFC_ACCESS_DENIED : status;
    }
    status = exchange(block, sizeof(block), &rx);
    return status == NFC_OK ? ack(&rx) : status;
}

static nfc_status_t authenticated(const mifare_uid_request_t *r)
{
    const bool gdm = r->method == UID_GDM_AUTH;
    const uint8_t auth = gdm        ? 0x80u
                         : r->key_b ? MIFARE_CMD_AUTH_B
                                    : MIFARE_CMD_AUTH_A;
    crypto1_state_t cipher = {0};
    nfc_status_t status = nfca_reader_restart(&r->expected);
    uint8_t old[16];
    if (status == NFC_OK)
    {
        status = mifare_reader_authenticate_opcode(
            &r->expected, 0u, auth, r->key, &cipher, NULL, 0u, get_rand_32());
    }
    if (status == NFC_OK)
    {
        status = mifare_reader_block_opcode(
            &cipher, 0u, gdm ? 0x38u : MIFARE_CMD_READ, old);
    }
    crypto1_clear(&cipher);
    if (status != NFC_OK)
    {
        return status;
    }
    uint8_t block[16];
    build_block(old, r->uid, r->expected.uid_size_bytes, false, block);
    status = nfca_reader_restart(&r->expected);
    if (status == NFC_OK)
    {
        status = mifare_reader_authenticate_opcode(
            &r->expected, 0u, auth, r->key, &cipher, NULL, 0u, get_rand_32());
    }
    if (status == NFC_OK)
    {
        status = mifare_reader_write_opcode(
            &cipher, 0u, gdm ? 0xa8u : MIFARE_CMD_WRITE, block);
    }
    crypto1_clear(&cipher);
    return status;
}

static nfc_status_t special(const mifare_uid_request_t *r)
{
    nfc_status_t status = nfca_reader_restart(&r->expected);
    if (status != NFC_OK)
    {
        return status;
    }
    nfca_pcd_response_t rx;
    uint8_t command[23] = {0};
    uint8_t size;
    if (r->method == UID_GEN3)
    {
        memcpy(command, (const uint8_t[]){0x90u, 0xfbu, 0xccu, 0xccu}, 4u);
        command[4] = r->expected.uid_size_bytes;
        memcpy(&command[5], r->uid, command[4]);
        size = 5u + command[4];
    }
    else if (r->method == UID_SUPER_GEN1)
    {
        memcpy(command,
               (const uint8_t[]){0x0au, 0u, 0u, 0xa6u, 0xa0u, 0u, 5u},
               7u);
        memcpy(&command[7], r->uid, 4u);
        size = 12u;
    }
    else
    {
        command[0] = 0xcfu;
        if (r->password != NULL)
        {
            memcpy(&command[1], r->password, 4u);
        }
        command[5] = 0xceu;
        status = exchange(command, 7u, &rx);
        if (status != NFC_OK || !valid(&rx, 18u))
        {
            return status == NFC_OK ? NFC_INVALID_RESPONSE : status;
        }
        build_block(
            rx.data, r->uid, r->expected.uid_size_bytes, false, &command[7]);
        status = nfca_reader_restart(&r->expected);
        if (status != NFC_OK)
        {
            return status;
        }
        command[5] = 0xcdu;
        size = 23u;
    }
    status = exchange(command, size, &rx);
    if (status != NFC_OK)
    {
        return status;
    }
    const bool super = r->method == UID_SUPER_GEN1;
    const uint8_t offset = super ? 2u : 0u;
    return valid(&rx, super ? 6u : 4u) &&
                   (!super || (rx.data[0] & 0xfeu) == 0x0au) &&
                   rx.data[offset] == 0x90u && rx.data[offset + 1u] == 0u
               ? NFC_OK
               : NFC_INVALID_RESPONSE;
}

nfc_status_t nfca_mifare_set_uid(const mifare_uid_request_t *r)
{
    if (r == NULL ||
        (r->expected.uid_size_bytes != 4u &&
         r->expected.uid_size_bytes != 7u) ||
        r->method < UID_GEN1A || r->method > UID_SUPER_GEN1)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const uint8_t sak = r->expected.sak & (uint8_t)~0x20u;
    if (sak != 8u && sak != 9u && sak != 0x18u)
    {
        return NFC_UNSUPPORTED;
    }
    const bool auth = r->method == UID_GEN2 || r->method == UID_FUID ||
                      r->method == UID_GDM_AUTH;
    if ((auth && r->key == NULL) || (!auth && (r->key != NULL || r->key_b)) ||
        (r->method != UID_GEN4_GTU && r->password != NULL) ||
        (r->method == UID_GDM_AUTH && r->key_b) ||
        (r->method == UID_SUPER_GEN1 && r->expected.uid_size_bytes != 4u))
    {
        return NFC_INVALID_ARGUMENT;
    }
    if (r->method == UID_FUID &&
        (r->expected.uid_size_bytes != 4u ||
         memcmp(r->expected.uid,
                (const uint8_t[]){0xaau, 0x55u, 0xc3u, 0x96u},
                4u) != 0))
    {
        return NFC_UNSUPPORTED;
    }
    nfc_status_t status = nfc_session_begin(NFC_STATE_READING_NFCA);
    if (status != NFC_OK)
    {
        return status;
    }
    status = auth ? authenticated(r)
             : r->method == UID_GEN3 || r->method == UID_GEN4_GTU ||
                     r->method == UID_SUPER_GEN1
                 ? special(r)
                 : direct(r);
    if (status == NFC_OK)
    {
        nfca_tag_t expected = r->expected;
        memcpy(expected.uid, r->uid, expected.uid_size_bytes);
        status = nfca_reader_restart(&expected);
    }
    const nfc_status_t stopped = nfc_stop();
    return stopped == NFC_OK ? status : stopped;
}

nfc_status_t nfca_mifare_uid_probe(const mifare_uid_request_t *r)
{
    if (r == NULL || r->method != UID_GEN4_GTU || r->key != NULL || r->key_b ||
        (r->expected.uid_size_bytes != 4u && r->expected.uid_size_bytes != 7u))
    {
        return NFC_INVALID_ARGUMENT;
    }
    nfc_status_t status = nfc_session_begin(NFC_STATE_READING_NFCA);
    if (status != NFC_OK)
    {
        return status;
    }
    status = nfca_reader_restart(&r->expected);
    if (status == NFC_OK)
    {
        uint8_t command[6] = {0xcfu, 0u, 0u, 0u, 0u, 0xc6u};
        if (r->password != NULL)
        {
            memcpy(&command[1], r->password, 4u);
        }
        nfca_pcd_response_t rx;
        status = exchange(command, sizeof(command), &rx);
        if (status == NFC_OK && !valid(&rx, 32u) && !valid(&rx, 34u))
        {
            status = NFC_INVALID_RESPONSE;
        }
        if (status == NFC_OK)
        {
            status = nfca_reader_restart(&r->expected);
        }
    }
    const nfc_status_t stopped = nfc_stop();
    return stopped == NFC_OK ? status : stopped;
}
