#include "cards/nfca_access.h"

#include "api_internal.h"
#include "cards/mifare/mifare_product.h"
#include "cards/type2/ntag_config.h"
#include "cards/type2/reader/ntag_reader.h"
#include "pn532_unleashed/core.h"
#include "pn532_unleashed/nfca_mifare.h"
#include "protocols/nfca/reader.h"

#include <string.h>

static struct
{
    bool active;
    nfca_tag_t expected;
    card_model_t model;
} context;

const nfca_tag_t *nfca_selected_tag(void)
{
    return context.active ? &context.expected : NULL;
}
card_model_t nfca_selected_model(void)
{
    return context.active ? context.model : 0;
}
void nfca_access_reset(void)
{
    memset(&context, 0, sizeof(context));
    mifare_access_reader_reset();
    ntag_access_reader_reset();
}
nfc_status_t nfca_select(const nfca_tag_t *expected, card_model_t model)
{
    nfc_status_t status = nfca_select_validate(expected, model);
    if (status != NFC_OK)
    {
        return status;
    }
    status = nfc_operation_begin(NFC_STATE_READING_NFCA);
    if (status != NFC_OK)
    {
        return status;
    }
    nfca_access_reset();
    status = nfca_reader_restart(expected);
    if (status != NFC_OK)
    {
        const nfc_status_t cleanup = nfc_stop();
        return cleanup == NFC_OK ? status : cleanup;
    }
    context.expected = *expected;
    context.model = model;
    context.active = true;
    mifare_access_reader_selected();
    return NFC_OK;
}
static nfc_status_t finish(nfc_status_t status)
{
    if (status != NFC_OK)
    {
        (void)nfc_stop();
    }
    return status;
}
nfc_status_t nfca_read_all(const uint8_t *password, nfca_dump_t *dump)
{
    if (dump == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    dump->memory_size_bytes = 0u;
    if (!context.active)
    {
        return NFC_BUSY;
    }
    nfc_status_t status = nfc_operation_require(NFC_STATE_READING_NFCA);
    if (status != NFC_OK)
    {
        return finish(status);
    }
    const ntag_config_t *config = ntag_model_config(context.model);
    if (config == NULL)
    {
        return finish(NFC_UNSUPPORTED);
    }
    if (!config->ev1 && password != NULL)
    {
        return finish(NFC_INVALID_ARGUMENT);
    }
    memset(dump, 0, sizeof(*dump));
    dump->tag = context.expected;
    status = ntag_reader_read(config, password, dump);
    if (status != NFC_OK)
    {
        dump->memory_size_bytes = 0u;
    }
    return finish(status);
}

static nfc_status_t page_access(uint8_t page,
                                const uint8_t *password,
                                const uint8_t *before,
                                const uint8_t *after,
                                uint8_t data[4],
                                bool *writable)
{
    if (!context.active)
    {
        return NFC_BUSY;
    }
    const nfc_status_t required = nfc_operation_require(NFC_STATE_READING_NFCA);
    if (required != NFC_OK)
    {
        return finish(required);
    }
    const ntag_config_t *config = ntag_model_config(context.model);
    if (config == NULL)
    {
        return finish(NFC_UNSUPPORTED);
    }
    const uint8_t end = config->dynamic_lock_page != 0u
                            ? config->dynamic_lock_page
                        : config->ev1 ? config->config_page
                                      : config->page_count;
    if (data == NULL || writable == NULL || page >= config->page_count ||
        (!config->ev1 && password != NULL) ||
        (before != NULL && (page <= NTAG_OTP_PAGE || page >= end)))
    {
        return finish(NFC_INVALID_ARGUMENT);
    }
    uint8_t result[4];
    bool allowed;
    nfc_status_t status =
        ntag_access_reader(page, password, before, after, result, &allowed);
    if (status == NFC_OK)
    {
        memcpy(data, result, 4u);
        *writable = allowed;
    }
    return finish(status);
}
nfc_status_t nfca_read_page(uint8_t page,
                            const uint8_t *password,
                            uint8_t data[4],
                            bool *writable)
{
    return page_access(page, password, NULL, NULL, data, writable);
}
nfc_status_t nfca_write_page(uint8_t page,
                             const uint8_t *password,
                             const uint8_t before[4],
                             const uint8_t after[4])
{
    if (!context.active)
    {
        return NFC_BUSY;
    }
    if (before == NULL || after == NULL)
    {
        return finish(NFC_INVALID_ARGUMENT);
    }
    uint8_t data[4];
    bool writable;
    return page_access(page, password, before, after, data, &writable);
}
static nfc_status_t block_access(uint8_t block,
                                 bool key_b,
                                 const uint8_t key[6],
                                 const uint8_t *before,
                                 const uint8_t *after,
                                 uint8_t data[16],
                                 bool *writable)
{
    if (!context.active)
    {
        return NFC_BUSY;
    }
    const nfc_status_t required = nfc_operation_require(NFC_STATE_READING_NFCA);
    if (required != NFC_OK)
    {
        return finish(required);
    }
    const mifare_product_config_t *config = mifare_model_config(context.model);
    if (config == NULL)
    {
        return finish(NFC_UNSUPPORTED);
    }
    if (key == NULL || data == NULL || writable == NULL ||
        block >= config->block_count ||
        (before != NULL && (block == 0u || mifare_is_sector_trailer(block))))
    {
        return finish(NFC_INVALID_ARGUMENT);
    }
    uint8_t result[16];
    bool allowed;
    nfc_status_t status = mifare_access_reader(
        block, key_b, key, before, after, result, &allowed);
    if (status == NFC_OK)
    {
        memcpy(data, result, 16u);
        *writable = allowed;
    }
    return finish(status);
}
nfc_status_t nfca_mifare_read_block(uint8_t block,
                                    bool key_b,
                                    const uint8_t key[6],
                                    uint8_t data[16],
                                    bool *writable)
{
    return block_access(block, key_b, key, NULL, NULL, data, writable);
}
nfc_status_t nfca_mifare_write_block(uint8_t block,
                                     bool key_b,
                                     const uint8_t key[6],
                                     const uint8_t before[16],
                                     const uint8_t after[16])
{
    if (!context.active)
    {
        return NFC_BUSY;
    }
    if (before == NULL || after == NULL)
    {
        return finish(NFC_INVALID_ARGUMENT);
    }
    uint8_t data[16];
    bool writable;
    return block_access(block, key_b, key, before, after, data, &writable);
}
nfc_status_t nfca_mifare_read_sector(uint8_t sector,
                                     bool key_b,
                                     const uint8_t key[6],
                                     uint8_t *memory,
                                     uint16_t capacity_bytes,
                                     uint16_t *size_bytes)
{
    if (size_bytes == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    *size_bytes = 0u;
    if (!context.active)
    {
        return NFC_BUSY;
    }
    const nfc_status_t required = nfc_operation_require(NFC_STATE_READING_NFCA);
    if (required != NFC_OK)
    {
        return finish(required);
    }
    const mifare_product_config_t *config = mifare_model_config(context.model);
    if (config == NULL)
    {
        return finish(NFC_UNSUPPORTED);
    }
    if (sector >= MIFARE_MAX_SECTORS || key == NULL || memory == NULL)
    {
        return finish(NFC_INVALID_ARGUMENT);
    }
    const uint16_t first = mifare_sector_first_block(sector);
    const uint16_t end = (uint16_t)mifare_sector_trailer(sector) + 1u;
    const uint16_t size = (end - first) * MIFARE_BLOCK_SIZE;
    if (end > config->block_count || capacity_bytes < size)
    {
        return finish(capacity_bytes < size ? NFC_BUFFER_TOO_SMALL
                                            : NFC_INVALID_ARGUMENT);
    }
    for (uint16_t block = first; block < end; block++)
    {
        bool writable;
        const nfc_status_t status =
            nfca_mifare_read_block((uint8_t)block,
                                   key_b,
                                   key,
                                   &memory[(block - first) * MIFARE_BLOCK_SIZE],
                                   &writable);
        if (status != NFC_OK)
        {
            return status;
        }
    }
    *size_bytes = size;
    return NFC_OK;
}
