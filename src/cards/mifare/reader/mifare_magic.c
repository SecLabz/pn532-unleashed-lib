#include "api_internal.h"
#include "cards/mifare/reader/mifare_reader.h"
#include "pn532_unleashed/integrity.h"
#include "pn532_unleashed/nfca_frame.h"
#include "pn532_unleashed/nfca_mifare_magic.h"
#include "pn532_unleashed/reader.h"

#include "pico/rand.h"
#include "pico/stdlib.h"

#include <string.h>

#define MAGIC_TIMEOUT_US 5000u
#define MAGIC_FIELD_OFF_US 5000u

static nfc_status_t restart(const nfca_tag_t *expected, bool select)
{
    if (!nfca_reader_stop())
    {
        return NFC_RF_ERROR;
    }
    sleep_us(MAGIC_FIELD_OFF_US);
    nfc_status_t status = nfca_reader_open();
    if (status != NFC_OK || !select)
    {
        return status;
    }
    nfca_tag_t tag;
    status = nfca_reader_select_tag(&tag);
    if (status != NFC_OK)
    {
        return status;
    }
    return tag.uid_size_bytes == expected->uid_size_bytes &&
                   memcmp(tag.uid, expected->uid, tag.uid_size_bytes) == 0 &&
                   memcmp(tag.atqa, expected->atqa, sizeof(tag.atqa)) == 0 &&
                   tag.sak == expected->sak
               ? NFC_OK
               : NFC_INVALID_RESPONSE;
}

static nfc_status_t
exchange(const uint8_t *command, uint8_t size_bytes, nfca_pcd_response_t *rx)
{
    reader_frame_t tx;
    (void)reader_frame_begin(&tx, size_bytes + NFC_CRC_BYTES);
    memcpy(tx.data, command, size_bytes);
    nfca_append_crc(tx.data, size_bytes);
    tx.byte_parity = true;
    return nfca_reader_transfer_frame(&tx, rx, MAGIC_TIMEOUT_US, true);
}

static bool bytes_valid(const nfca_pcd_response_t *rx,
                        uint8_t minimum_bytes,
                        uint8_t exact_bytes)
{
    return rx->framing_valid && !rx->collision && rx->parity_valid &&
           rx->size_bits % 8u == 0u && rx->size_bits >= minimum_bytes * 8u &&
           (exact_bytes == 0u || rx->size_bits == exact_bytes * 8u);
}

static bool missing(nfc_status_t status)
{
    /* A fresh field/selection separates probes. Hardware faults are fatal;
     * silence or malformed unsupported-command replies add no signature. */
    return status == NFC_NO_RESPONSE || status == NFC_INVALID_RESPONSE;
}

static nfc_status_t wakeup(uint8_t command, bool short_frame, bool *ack)
{
    reader_frame_t tx;
    (void)reader_frame_begin(&tx, 1u);
    tx.data[0] = command;
    tx.last_byte_bits = short_frame ? 7u : 0u;
    tx.byte_parity = !short_frame;
    nfca_pcd_response_t rx;
    const nfc_status_t status =
        nfca_reader_transfer_frame(&tx, &rx, MAGIC_TIMEOUT_US, true);
    *ack = status == NFC_OK && rx.size_bits == 4u &&
           (rx.data[0] & 0x0fu) == MIFARE_ACK;
    return missing(status) ? NFC_OK : status;
}

static nfc_status_t restart_halted(const nfca_tag_t *expected)
{
    const nfc_status_t status = restart(expected, true);
    if (status != NFC_OK)
    {
        return status;
    }
    reader_frame_t tx;
    nfca_pcd_response_t unused;
    mifare_reader_command(&tx, NFCA_CMD_HALT, 0u);
    /* HALT has no reply. Selection already established this card's identity. */
    return nfca_reader_transfer_frame(&tx, &unused, MAGIC_TIMEOUT_US, false);
}

static nfc_status_t gen1(const nfca_tag_t *expected, uint16_t *flags)
{
    nfc_status_t status = restart_halted(expected);
    bool ack = false;
    if (status == NFC_OK)
    {
        status = wakeup(0x40u, true, &ack);
    }
    if (status != NFC_OK || !ack)
    {
        return status;
    }
    status = wakeup(0x43u, false, &ack);
    if (status != NFC_OK)
    {
        return status;
    }
    *flags |= ack ? MAGIC_GEN1A : MAGIC_GEN1B;
    if (!ack)
    {
        /* Gen1B is already unlocked by 40; do not continue after a failed 43.
         */
        status = restart_halted(expected);
        if (status == NFC_OK)
        {
            status = wakeup(0x40u, true, &ack);
        }
        if (status != NFC_OK || !ack)
        {
            return status;
        }
    }
    const uint8_t config[] = {0xe0u, 0x00u};
    nfca_pcd_response_t rx;
    status = exchange(config, sizeof(config), &rx);
    if (status != NFC_OK || !bytes_valid(&rx, 18u, 18u) ||
        !nfca_check_crc(rx.data, 18u))
    {
        return missing(status) ? NFC_OK : status;
    }
    const uint8_t hidden[] = {0x38u, 0x00u};
    status = exchange(hidden, sizeof(hidden), &rx);
    if (status != NFC_OK && !missing(status))
    {
        return status;
    }
    *flags |= status == NFC_OK && bytes_valid(&rx, 18u, 18u) &&
                      nfca_check_crc(rx.data, 18u)
                  ? MAGIC_GDM_40
                  : MAGIC_GDM_ZUID;
    return NFC_OK;
}

static nfc_status_t gen2_ats(mifare_magic_result_t *result)
{
    static const uint8_t ats4[] = {
        0x09u, 0x78u, 0x00u, 0x91u, 0x02u, 0xdau, 0xbcu, 0x19u, 0x10u};
    static const uint8_t ats7[] = {0x0du,
                                   0x78u,
                                   0x00u,
                                   0x71u,
                                   0x02u,
                                   0x88u,
                                   0x49u,
                                   0xa1u,
                                   0x30u,
                                   0x20u,
                                   0x15u,
                                   0x06u,
                                   0x08u};
    const uint8_t rats[] = {0xe0u, 0x20u};
    nfca_pcd_response_t rx;
    const nfc_status_t status = exchange(rats, sizeof(rats), &rx);
    if (status != NFC_OK)
    {
        return missing(status) ? NFC_OK : status;
    }
    const size_t size_bytes = rx.size_bits / 8u;
    if (!bytes_valid(&rx, 3u, 0u) || !nfca_check_crc(rx.data, size_bytes))
    {
        return NFC_OK;
    }
    const size_t ats_bytes = size_bytes - NFC_CRC_BYTES;
    if ((ats_bytes == sizeof(ats4) && memcmp(rx.data, ats4, ats_bytes) == 0) ||
        (ats_bytes == sizeof(ats7) && memcmp(rx.data, ats7, ats_bytes) == 0))
    {
        result->flags |= MAGIC_GEN2;
        result->gen2 = MAGIC_GEN2_ATS;
    }
    return NFC_OK;
}

static nfc_status_t gen2_write(const nfca_tag_t *tag,
                               const uint8_t key[MIFARE_KEY_BYTES],
                               bool key_b,
                               mifare_magic_result_t *result)
{
    crypto1_state_t cipher = {0};
    nfc_status_t status = mifare_reader_authenticate(
        tag, 0u, key_b, key, &cipher, NULL, 0u, get_rand_32());
    result->gen2 = MAGIC_GEN2_AUTH_DENIED;
    if (status == NFC_OK)
    {
        result->gen2 = MAGIC_GEN2_WRITE_INCONCLUSIVE;
        reader_frame_t tx;
        nfca_pcd_response_t rx;
        mifare_reader_command(&tx, MIFARE_CMD_WRITE, 0u);
        mifare_reader_encrypt(&cipher, &tx, 0u);
        status = nfca_reader_transfer(&tx, &rx, 4u);
        /* Never send write data or another RF command after this reply. */
        const bool stopped = nfca_reader_stop();
        if (!stopped)
        {
            crypto1_clear(&cipher);
            return NFC_RF_ERROR;
        }
        if (status == NFC_OK && rx.size_bits == 4u && !rx.collision)
        {
            uint8_t ack = rx.data[0] & 0x0fu;
            for (uint8_t bit = 0u; bit < 4u; bit++)
            {
                ack ^= crypto1_bit(&cipher, 0u, false) << bit;
            }
            if (ack == MIFARE_ACK)
            {
                result->gen2 = MAGIC_GEN2_WRITE_ACK;
                result->flags |= MAGIC_GEN2;
            }
            else if (ack == 0u || ack == 1u || ack == 4u || ack == 5u)
            {
                result->gen2 = MAGIC_GEN2_WRITE_DENIED;
            }
        }
        else if (status == NFC_OK)
        {
            status = NFC_INVALID_RESPONSE;
        }
    }
    crypto1_clear(&cipher);
    return status == NFC_ACCESS_DENIED || status == NFC_NO_TAG ||
                   missing(status)
               ? NFC_OK
               : status;
}

static nfc_status_t probe(uint8_t kind, uint16_t *flags)
{
    static const uint8_t gtu[] = {0xcfu, 0u, 0u, 0u, 0u, 0xc6u};
    static const uint8_t read[] = {MIFARE_CMD_READ, 0u};
    static const uint8_t auth[] = {0x80u, 0u};
    static const uint8_t super[] = {0x0au, 0u, 0u, 0xa6u, 0xb0u, 0u, 0x10u};
    const uint8_t *commands[] = {gtu, read, auth, super};
    const uint8_t sizes[] = {
        sizeof(gtu), sizeof(read), sizeof(auth), sizeof(super)};
    const uint16_t bits[] = {
        MAGIC_GEN4_GTU, MAGIC_GEN3, MAGIC_GDM_AUTH, MAGIC_SUPER_GEN1};
    nfca_pcd_response_t rx;
    const nfc_status_t status = exchange(commands[kind], sizes[kind], &rx);
    if (status != NFC_OK)
    {
        return missing(status) ? NFC_OK : status;
    }
    bool valid = false;
    switch (kind)
    {
    case 0:
        valid = (bytes_valid(&rx, 32u, 32u) && nfca_check_crc(rx.data, 32u)) ||
                (bytes_valid(&rx, 34u, 34u) && nfca_check_crc(rx.data, 34u));
        break;
    case 1:
        valid = bytes_valid(&rx, 18u, 18u) && nfca_check_crc(rx.data, 18u);
        break;
    case 2:
        valid = bytes_valid(&rx, 4u, 4u);
        break;
    case 3:
        valid = bytes_valid(&rx, 22u, 22u) && nfca_check_crc(rx.data, 22u);
        break;
    }
    if (valid)
    {
        *flags |= bits[kind];
    }
    return NFC_OK;
}

static nfc_status_t detect(const nfca_tag_t *expected,
                           const uint8_t *key,
                           bool key_b,
                           mifare_magic_result_t *result)
{
    static const uint8_t unfused[] = {0xaau, 0x55u, 0xc3u, 0x96u};
    nfc_status_t status = restart(expected, true);
    if (status != NFC_OK)
    {
        return status;
    }
    if (memcmp(&expected->uid[expected->uid_size_bytes - 4u], unfused, 4u) == 0)
    {
        result->flags |= MAGIC_UNFUSED;
    }
    status = gen1(expected, &result->flags);
    if (status == NFC_OK)
    {
        status = restart(expected, true);
    }
    if (status == NFC_OK)
    {
        status = gen2_ats(result);
    }
    if (status == NFC_OK && key != NULL && result->gen2 != MAGIC_GEN2_ATS)
    {
        status = restart(expected, true);
        if (status == NFC_OK)
        {
            status = gen2_write(expected, key, key_b, result);
        }
    }
    for (uint8_t kind = 0u; status == NFC_OK && kind < 4u; kind++)
    {
        status = restart(expected, true);
        if (status == NFC_OK)
        {
            status = probe(kind, &result->flags);
        }
    }
    if (status != NFC_OK)
    {
        return status;
    }
    if ((result->flags & MAGIC_SUPER_GEN1) != 0u &&
        (result->flags & MAGIC_GEN3) != 0u)
    {
        result->flags &= (uint16_t)~MAGIC_SUPER_GEN1;
        result->flags |= MAGIC_SUPER_GEN2;
    }
    status = restart(expected, true);
    if (status != NFC_OK)
    {
        return status;
    }
    static const uint8_t ql88_key[] = {
        0x70u, 0x7bu, 0x11u, 0xfcu, 0x14u, 0x81u};
    crypto1_state_t cipher = {0};
    status = mifare_reader_authenticate(
        expected, 68u, true, ql88_key, &cipher, NULL, 0u, get_rand_32());
    crypto1_clear(&cipher);
    if (status == NFC_OK)
    {
        result->flags |= MAGIC_QL88;
    }
    else if (status != NFC_ACCESS_DENIED && status != NFC_NO_TAG &&
             !missing(status))
    {
        return status;
    }
    status = restart_halted(expected);
    bool ack = false;
    if (status == NFC_OK)
    {
        status = wakeup(0x20u, true, &ack);
    }
    if (status == NFC_OK && ack)
    {
        status = wakeup(0x23u, false, &ack);
        if (status == NFC_OK && ack)
        {
            const uint8_t config[] = {0xe0u, 0x00u};
            nfca_pcd_response_t rx;
            status = exchange(config, sizeof(config), &rx);
            if (status == NFC_OK && bytes_valid(&rx, 18u, 18u) &&
                nfca_check_crc(rx.data, 18u))
            {
                result->flags |= MAGIC_GDM_20;
            }
        }
    }
    if (status != NFC_OK && !missing(status))
    {
        return status;
    }
    return restart(expected, true);
}

nfc_status_t nfca_mifare_magic(const nfca_tag_t *expected,
                               const uint8_t key[MIFARE_KEY_BYTES],
                               bool key_b,
                               mifare_magic_result_t *result)
{
    if (expected == NULL || result == NULL ||
        (expected->uid_size_bytes != 4u && expected->uid_size_bytes != 7u &&
         expected->uid_size_bytes != 10u) ||
        (key == NULL && key_b))
    {
        return NFC_INVALID_ARGUMENT;
    }
    const uint8_t classic_sak = expected->sak & (uint8_t)~0x20u;
    if (classic_sak != 0x08u && classic_sak != 0x09u && classic_sak != 0x18u)
    {
        return NFC_UNSUPPORTED;
    }
    nfc_status_t status = nfc_session_begin(NFC_STATE_READING_NFCA);
    if (status != NFC_OK)
    {
        return status;
    }
    mifare_magic_result_t staged = {0};
    status = detect(expected, key, key_b, &staged);
    const nfc_status_t stopped = nfc_stop();
    if (stopped != NFC_OK)
    {
        return stopped;
    }
    if (status == NFC_OK)
    {
        *result = staged;
    }
    if (status == NFC_RF_ERROR)
    {
        nfc_session_error();
    }
    return status;
}
