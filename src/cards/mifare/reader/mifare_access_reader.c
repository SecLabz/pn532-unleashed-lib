#include "api_internal.h"
#include "cards/mifare/mifare_product.h"
#include "cards/mifare/reader/mifare_reader.h"
#include "cards/nfca_access.h"
#include "pn532_unleashed/core.h"
#include "pn532_unleashed/nfca_mifare.h"

#include "pico/rand.h"
#include "pico/stdlib.h"

#include <string.h>

static struct
{
    crypto1_state_t cipher;
    uint8_t trailer[16];
    uint8_t credential[6];
    uint8_t credential_kind;
    uint8_t sector;
    bool authenticated;
    bool needs_reselection;
} context;

void mifare_access_reader_reset(void)
{
    crypto1_clear(&context.cipher);
    memset(&context, 0, sizeof(context));
    context.needs_reselection = true;
}

void mifare_access_reader_selected(void)
{
    context.needs_reselection = false;
}

static nfc_status_t
authenticate(uint8_t block, bool key_b, const uint8_t key[6])
{
    const uint8_t sector = mifare_block_sector(block);
    if (context.authenticated && context.sector == sector &&
        context.credential_kind == (uint8_t)key_b &&
        memcmp(context.credential, key, 6u) == 0)
    {
        return NFC_OK;
    }
    if (context.authenticated || context.needs_reselection)
    {
        /* The shared authenticator starts with plaintext AUTH. Reset the
         * selected card's cipher at sector/key boundaries, keeping RF on. */
        const nfc_status_t status = nfca_reader_reselect(nfca_selected_tag());
        if (status != NFC_OK)
        {
            return status;
        }
    }
    mifare_access_reader_reset();
    nfc_status_t status = mifare_reader_authenticate(nfca_selected_tag(),
                                                     block,
                                                     key_b,
                                                     key,
                                                     &context.cipher,
                                                     NULL,
                                                     0u,
                                                     get_rand_32());
    if (status != NFC_OK)
    {
        return status;
    }
    status = mifare_reader_block(
        &context.cipher, mifare_sector_trailer(sector), context.trailer);
    if (status != NFC_OK)
    {
        return status;
    }
    if (!mifare_trailer_access_bits_valid(context.trailer))
    {
        return NFC_INVALID_RESPONSE;
    }
    context.sector = sector;
    context.credential_kind = (uint8_t)key_b;
    memcpy(context.credential, key, 6u);
    context.authenticated = true;
    return NFC_OK;
}

nfc_status_t mifare_access_reader(uint8_t block,
                                  bool key_b,
                                  const uint8_t key[6],
                                  const uint8_t *before,
                                  const uint8_t *after,
                                  uint8_t data[16],
                                  bool *writable)
{
    nfc_status_t status = authenticate(block, key_b, key);
    if (status != NFC_OK)
    {
        return status;
    }
    const uint8_t key_kind = (uint8_t)key_b;
    if (!mifare_is_sector_trailer(block) &&
        !mifare_data_permission(
            context.trailer, block, key_kind, MIFARE_DATA_READ))
    {
        return NFC_ACCESS_DENIED;
    }
    *writable =
        block != 0u && mifare_data_permission(
                           context.trailer, block, key_kind, MIFARE_DATA_WRITE);
    status = mifare_reader_block(&context.cipher, block, data);
    if (status != NFC_OK || before == NULL)
    {
        return status;
    }
    if (!*writable)
    {
        return NFC_ACCESS_DENIED;
    }
    if (memcmp(data, before, 16u) != 0)
    {
        return NFC_CONTENT_CHANGED;
    }
    status = mifare_reader_write_opcode(
        &context.cipher, block, MIFARE_CMD_WRITE, after);
    if (status != NFC_OK)
    {
        return status;
    }
    status = mifare_reader_block(&context.cipher, block, data);
    return status == NFC_OK && memcmp(data, after, 16u) == 0
               ? NFC_OK
               : NFC_WRITE_OUTCOME_UNKNOWN;
}

nfc_status_t
nfca_mifare_check_key(uint8_t block, bool key_b, const uint8_t key[6])
{
    if (nfca_selected_tag() == NULL)
    {
        return NFC_BUSY;
    }
    const mifare_product_config_t *config =
        mifare_model_config(nfca_selected_model());
    if (config == NULL)
    {
        return NFC_UNSUPPORTED;
    }
    if (block >= config->block_count || key == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t required = nfc_operation_require(NFC_STATE_READING_NFCA);
    if (required != NFC_OK)
    {
        return required;
    }
    const bool selected = !context.authenticated && !context.needs_reselection;
    mifare_access_reader_reset();
    nfc_status_t status = NFC_OK;
    if (!selected)
    {
        status = nfca_reader_reselect_known(nfca_selected_tag(), 66u);
        if (status != NFC_OK)
        {
            return status;
        }
    }
    crypto1_state_t cipher = {0};
    status = mifare_reader_authenticate(nfca_selected_tag(),
                                        block,
                                        key_b,
                                        key,
                                        &cipher,
                                        NULL,
                                        0u,
                                        get_rand_32());
    const bool wrong_key = status == NFC_ACCESS_DENIED || status == NFC_NO_TAG;
    if (status == NFC_OK)
    {
        reader_frame_t halt;
        mifare_reader_command(&halt, NFCA_CMD_HALT, 0u);
        mifare_reader_encrypt(&cipher, &halt, 0u);
        status = nfca_reader_transfer_frame(&halt, NULL, 0u, false);
    }
    crypto1_clear(&cipher);
    if (!wrong_key && status != NFC_OK)
    {
        return status;
    }
    /* A valid authentication permits encrypted HALT, avoiding the ignored
     * first WUPA while the card cipher is active. Reset after a wrong
     * candidate before ordinary reads resume; the cached reader cipher was
     * discarded above. */
    const nfc_status_t selection =
        nfca_reader_reselect_known(nfca_selected_tag(), 66u);
    if (selection != NFC_OK)
    {
        return selection;
    }
    mifare_access_reader_selected();
    return wrong_key ? NFC_ACCESS_DENIED : status;
}

static nfc_status_t verify_trailer(uint8_t block, const uint8_t after[16])
{
    nfc_status_t status = nfca_mifare_check_key(block, false, after);
    if (status != NFC_OK)
    {
        return status;
    }
    status = authenticate(block, false, after);
    if (status != NFC_OK || memcmp(&context.trailer[6], &after[6], 4u) != 0)
    {
        return status == NFC_OK ? NFC_INVALID_RESPONSE : status;
    }
    const bool readable_b =
        (mifare_trailer_key_permissions(after, MIFARE_KEY_A) &
         MIFARE_TRAILER_READ_KEY_B) != 0u;
    if (readable_b)
    {
        return memcmp(&context.trailer[10], &after[10], 6u) == 0
                   ? NFC_OK
                   : NFC_INVALID_RESPONSE;
    }
    return nfca_mifare_check_key(block, true, &after[10]);
}

static nfc_status_t trailer_operation(uint8_t sector,
                                      bool key_b,
                                      const uint8_t key[6],
                                      const uint8_t before[16],
                                      const uint8_t after[16],
                                      bool write)
{
    if (nfca_selected_tag() == NULL)
    {
        return NFC_BUSY;
    }
    const mifare_product_config_t *config =
        mifare_model_config(nfca_selected_model());
    nfc_status_t status = nfc_operation_require(NFC_STATE_READING_NFCA);
    if (status == NFC_OK &&
        (config == NULL || sector >= MIFARE_MAX_SECTORS ||
         mifare_sector_trailer(sector) >= config->block_count || key == NULL ||
         before == NULL || after == NULL ||
         !mifare_trailer_access_bits_valid(after)))
    {
        status = NFC_INVALID_ARGUMENT;
    }
    bool write_started = false;
    if (status == NFC_OK)
    {
        /* Never trust a cached trailer across a host planning/confirmation gap.
         */
        mifare_access_reader_reset();
        status = nfca_reader_reselect_known(nfca_selected_tag(), 66u);
    }
    const uint8_t block =
        sector < MIFARE_MAX_SECTORS ? mifare_sector_trailer(sector) : 0u;
    if (status == NFC_OK)
    {
        mifare_access_reader_selected();
        status = authenticate(block, key_b, key);
    }
    if (status == NFC_OK && memcmp(context.trailer, before, 16u) != 0)
    {
        status = NFC_CONTENT_CHANGED;
    }
    if (status == NFC_OK)
    {
        const uint8_t permissions =
            mifare_trailer_key_permissions(context.trailer, (uint8_t)key_b);
        if (memcmp(&before[6], &after[6], 4u) != 0 &&
            (permissions & MIFARE_TRAILER_WRITE_ACCESS) == 0u)
        {
            status = NFC_ACCESS_DENIED;
        }
        /* A denied key field may only be preserved, never silently changed.
         * Authenticate the proposed key to prove it already matches the card.
         */
        if (status == NFC_OK &&
            (permissions & MIFARE_TRAILER_WRITE_KEY_A) == 0u)
        {
            status = nfca_mifare_check_key(block, false, after);
        }
        if (status == NFC_OK &&
            (permissions & MIFARE_TRAILER_WRITE_KEY_B) == 0u)
        {
            const bool readable =
                (mifare_trailer_key_permissions(before, MIFARE_KEY_A) &
                 MIFARE_TRAILER_READ_KEY_B) != 0u;
            status = readable ? (memcmp(&before[10], &after[10], 6u) == 0
                                     ? NFC_OK
                                     : NFC_ACCESS_DENIED)
                              : nfca_mifare_check_key(block, true, &after[10]);
        }
        if (status == NFC_OK)
        {
            status = authenticate(block, key_b, key);
            if (status == NFC_OK && memcmp(context.trailer, before, 16u) != 0)
            {
                status = NFC_CONTENT_CHANGED;
            }
        }
    }
    if (status == NFC_OK && write)
    {
        /* After a successful WRITE command, any failed check is uncertain. */
        status = mifare_reader_write_opcode(
            &context.cipher, block, MIFARE_CMD_WRITE, after);
        write_started = status == NFC_OK || status == NFC_WRITE_OUTCOME_UNKNOWN;
        mifare_access_reader_reset();
        if (status == NFC_OK)
        {
            status = verify_trailer(block, after);
        }
        if (write_started && status != NFC_OK)
        {
            status = NFC_WRITE_OUTCOME_UNKNOWN;
        }
    }
    const nfc_status_t cleanup = nfc_stop();
    if (cleanup != NFC_OK)
    {
        return write_started ? NFC_WRITE_OUTCOME_UNKNOWN : cleanup;
    }
    return status;
}

nfc_status_t nfca_mifare_trailer_check(uint8_t sector,
                                       bool key_b,
                                       const uint8_t key[6],
                                       const uint8_t before[16],
                                       const uint8_t after[16])
{
    return trailer_operation(sector, key_b, key, before, after, false);
}

nfc_status_t nfca_mifare_trailer_write(uint8_t sector,
                                       bool key_b,
                                       const uint8_t key[6],
                                       const uint8_t before[16],
                                       const uint8_t after[16])
{
    return trailer_operation(sector, key_b, key, before, after, true);
}
