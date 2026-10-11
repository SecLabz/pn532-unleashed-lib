#include "cards/nfca_access.h"
#include "cards/type2/ntag_config.h"
#include "pn532_unleashed/integrity.h"
#include "protocols/nfca/reader.h"

#include <string.h>

#define TYPE2_ACCESS_RESPONSE_TIMEOUT_US 20000u
#define TYPE2_ACCESS_READ_BYTES (NTAG_READ_PAGES * NTAG_PAGE_BYTES)

static struct
{
    bool prepared;
    bool permissions_known;
    uint8_t credential_kind;
    uint8_t password[NTAG_PASSWORD_BYTES];
    uint8_t pack[NTAG_PACK_BYTES];
    uint16_t static_locks;
    uint16_t dynamic_locks;
    uint8_t auth_start;
} context;

void ntag_access_reader_reset(void)
{
    memset(&context, 0, sizeof(context));
}

static nfc_status_t exchange(const uint8_t *command,
                             uint8_t command_size_bytes,
                             uint8_t *output,
                             uint8_t reply_size_bytes)
{
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    (void)reader_frame_begin(&tx, command_size_bytes + NFC_CRC_BYTES);
    tx.byte_parity = true;
    memcpy(tx.data, command, command_size_bytes);
    nfca_append_crc(tx.data, command_size_bytes);
    nfc_status_t status = nfca_reader_transfer_frame(
        &tx, &rx, TYPE2_ACCESS_RESPONSE_TIMEOUT_US, true);
    if (status != NFC_OK)
    {
        return status;
    }
    if (reply_size_bytes == 0u)
    {
        return rx.framing_valid && !rx.collision &&
                       rx.size_bits == NFCA_SHORT_RESPONSE_BITS &&
                       (rx.data[0] & NFCA_SHORT_RESPONSE_MASK) == NFCA_ACK
                   ? NFC_OK
                   : NFC_ACCESS_DENIED;
    }
    if (rx.size_bits == NFCA_SHORT_RESPONSE_BITS)
    {
        return NFC_ACCESS_DENIED;
    }
    if (!rx.framing_valid || rx.collision || !rx.parity_valid ||
        rx.size_bits != (reply_size_bytes + NFC_CRC_BYTES) * 8u ||
        !nfca_check_crc(rx.data, reply_size_bytes + NFC_CRC_BYTES))
    {
        return NFC_INVALID_RESPONSE;
    }
    memcpy(output, rx.data, reply_size_bytes);
    return NFC_OK;
}

static nfc_status_t read_pages(uint8_t page,
                               uint8_t output[TYPE2_ACCESS_READ_BYTES])
{
    return exchange((const uint8_t[]){NTAG_CMD_READ, page},
                    NTAG_INDEXED_COMMAND_BYTES,
                    output,
                    TYPE2_ACCESS_READ_BYTES);
}

static nfc_status_t permissions(const ntag_config_t *config)
{
    uint8_t pages[TYPE2_ACCESS_READ_BYTES];
    nfc_status_t status = read_pages(0u, pages);
    if (status != NFC_OK)
    {
        return status;
    }
    context.static_locks =
        (uint16_t)pages[NTAG_STATIC_LOCK_PAGE * NTAG_PAGE_BYTES +
                        NTAG_STATIC_LOCK_BYTE] |
        ((uint16_t)pages[NTAG_STATIC_LOCK_PAGE * NTAG_PAGE_BYTES +
                         NTAG_STATIC_LOCK_BYTE + 1u]
         << 8u);
    if (config->dynamic_lock_page != 0u)
    {
        status = read_pages(config->dynamic_lock_page, pages);
        if (status != NFC_OK)
        {
            return status;
        }
        context.dynamic_locks = (uint16_t)pages[0] | ((uint16_t)pages[1] << 8u);
    }
    if (config->ev1)
    {
        status = read_pages(config->config_page, pages);
        if (status != NFC_OK)
        {
            return status;
        }
        context.auth_start = pages[NTAG_CONFIG_AUTH0_BYTE];
    }
    return NFC_OK;
}

static nfc_status_t prepare(const uint8_t *password)
{
    const ntag_config_t *config = ntag_model_config(nfca_selected_model());
    if (context.prepared)
    {
        if (context.credential_kind == (password != NULL) &&
            (password == NULL ||
             memcmp(context.password, password, NTAG_PASSWORD_BYTES) == 0))
        {
            return NFC_OK;
        }
        const nfc_status_t status = nfca_reader_reselect(nfca_selected_tag());
        if (status != NFC_OK)
        {
            return status;
        }
        ntag_access_reader_reset();
    }
    uint8_t version[NTAG_VERSION_BYTES];
    nfc_status_t status = exchange((const uint8_t[]){NTAG_CMD_GET_VERSION},
                                   NTAG_OPCODE_BYTES,
                                   version,
                                   sizeof(version));
    if (config->ev1)
    {
        if (status != NFC_OK)
        {
            return status;
        }
        if (memcmp(version, config->version, sizeof(version)) != 0)
        {
            return NFC_UNSUPPORTED;
        }
    }
    else
    {
        /* A versioned product must not be treated as a plain Ultralight. */
        if (status != NFC_NO_RESPONSE && status != NFC_ACCESS_DENIED)
        {
            return status == NFC_OK ? NFC_UNSUPPORTED : status;
        }
        status = nfca_reader_reselect(nfca_selected_tag());
        if (status != NFC_OK)
        {
            return status;
        }
    }
    if (password != NULL)
    {
        uint8_t command[NTAG_OPCODE_BYTES + NTAG_PASSWORD_BYTES] = {
            NTAG_CMD_PWD_AUTH};
        memcpy(&command[NTAG_OPCODE_BYTES], password, NTAG_PASSWORD_BYTES);
        status = exchange(
            command, sizeof(command), context.pack, sizeof(context.pack));
        if (status != NFC_OK)
        {
            return status;
        }
    }
    status = permissions(config);
    if (status != NFC_OK && status != NFC_ACCESS_DENIED)
    {
        return status;
    }
    context.permissions_known = status == NFC_OK;
    context.credential_kind = (password != NULL);
    if (password != NULL)
    {
        memcpy(context.password, password, NTAG_PASSWORD_BYTES);
    }
    context.prepared = true;
    return NFC_OK;
}

nfc_status_t ntag_access_reader_prepare(const uint8_t *password,
                                        nfca_dump_t *dump)
{
    const nfc_status_t status = prepare(password);
    if (status != NFC_OK)
    {
        return status;
    }
    const ntag_config_t *config = ntag_model_config(nfca_selected_model());
    if (config->ev1)
    {
        memcpy(dump->version, config->version, sizeof(dump->version));
        dump->metadata_valid |= NFCA_METADATA_VERSION;
    }
    if (password != NULL)
    {
        memcpy(dump->pack, context.pack, sizeof(dump->pack));
        dump->metadata_valid |= NFCA_METADATA_PACK;
    }
    return NFC_OK;
}

static nfc_status_t read_page(uint8_t page, uint8_t data[4])
{
    const ntag_config_t *config = ntag_model_config(nfca_selected_model());
    uint8_t pages[TYPE2_ACCESS_READ_BYTES];
    const uint8_t first = page + NTAG_READ_PAGES <= config->page_count
                              ? page
                              : config->page_count - NTAG_READ_PAGES;
    const nfc_status_t status = read_pages(first, pages);
    if (status == NFC_OK)
    {
        memcpy(data, &pages[(page - first) * NTAG_PAGE_BYTES], NTAG_PAGE_BYTES);
    }
    return status;
}

nfc_status_t ntag_access_reader(uint8_t page,
                                const uint8_t *password,
                                const uint8_t *before,
                                const uint8_t *after,
                                uint8_t data[4],
                                bool *writable)
{
    nfc_status_t status = prepare(password);
    if (status != NFC_OK)
    {
        return status;
    }
    const ntag_config_t *config = ntag_model_config(nfca_selected_model());
    *writable = page > NTAG_STATIC_LOCK_LAST_PAGE ||
                (context.static_locks & (1u << page)) == 0u;
    if (config->dynamic_lock_page != 0u && page >= NTAG_DYNAMIC_LOCK_FIRST_PAGE)
    {
        const uint8_t bit = (page - NTAG_DYNAMIC_LOCK_FIRST_PAGE) /
                            config->dynamic_lock_granularity;
        *writable = *writable && (context.dynamic_locks & (1u << bit)) == 0u;
    }
    if (config->ev1)
    {
        *writable =
            *writable && (password != NULL || page < context.auth_start);
    }
    const uint8_t end = config->dynamic_lock_page != 0u
                            ? config->dynamic_lock_page
                        : config->ev1 ? config->config_page
                                      : config->page_count;
    *writable = *writable && context.permissions_known &&
                page > NTAG_OTP_PAGE && page < end;
    status = read_page(page, data);
    if (status != NFC_OK || before == NULL)
    {
        return status;
    }
    if (!*writable)
    {
        return NFC_ACCESS_DENIED;
    }
    if (memcmp(data, before, NTAG_PAGE_BYTES) != 0)
    {
        return NFC_CONTENT_CHANGED;
    }
    uint8_t command[NTAG_WRITE_COMMAND_BYTES] = {NTAG_CMD_WRITE, page};
    memcpy(&command[NTAG_INDEXED_COMMAND_BYTES], after, NTAG_PAGE_BYTES);
    status = exchange(command, sizeof(command), NULL, 0u);
    if (status != NFC_OK)
    {
        return NFC_WRITE_OUTCOME_UNKNOWN;
    }
    status = read_page(page, data);
    return status == NFC_OK && memcmp(data, after, NTAG_PAGE_BYTES) == 0
               ? NFC_OK
               : NFC_WRITE_OUTCOME_UNKNOWN;
}
