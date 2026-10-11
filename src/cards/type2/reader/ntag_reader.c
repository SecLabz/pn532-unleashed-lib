#include "cards/type2/reader/ntag_reader.h"

#include "board/activity_led.h"
#include "cards/nfca_access.h"
#include "pn532_unleashed/integrity.h"
#include "pn532_unleashed/nfca_frame.h"

#include <string.h>

/* CRC/parity apply only to complete plaintext replies. A physical four-bit
 * NAK is a refusal, not a short version of the requested data. */
static nfc_status_t exchange(const uint8_t *command,
                             uint8_t command_size_bytes,
                             uint8_t *output,
                             uint8_t output_size_bytes)
{
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    (void)reader_frame_begin(&tx, command_size_bytes + NFC_CRC_BYTES);
    tx.byte_parity = true;
    memcpy(tx.data, command, command_size_bytes);
    nfca_append_crc(tx.data, command_size_bytes);
    const nfc_status_t status = nfca_reader_transfer(
        &tx, &rx, (output_size_bytes + NFC_CRC_BYTES) * 8u);
    if (status != NFC_OK)
    {
        return status;
    }
    if (rx.size_bits == NFCA_SHORT_RESPONSE_BITS)
    {
        return NFC_ACCESS_DENIED;
    }
    if (rx.size_bits != (output_size_bytes + NFC_CRC_BYTES) * 8u ||
        !rx.parity_valid ||
        !nfca_check_crc(rx.data, output_size_bytes + NFC_CRC_BYTES))
    {
        return NFC_RF_ERROR;
    }
    memcpy(output, rx.data, output_size_bytes);
    board_activity_led_task();
    return NFC_OK;
}

static nfc_status_t read_memory(const ntag_config_t *config, nfca_dump_t *dump)
{
    nfc_status_t status;
    uint8_t pages[NTAG_READ_PAGES * NTAG_PAGE_BYTES];
    for (uint16_t page = 0u; page < config->page_count; page += NTAG_READ_PAGES)
    {
        /* READ wraps near the end of some products. Start the final request
         * four pages before the end, then copy only its remaining pages. */
        const uint8_t first = page + NTAG_READ_PAGES <= config->page_count
                                  ? page
                                  : config->page_count - NTAG_READ_PAGES;
        status = exchange((const uint8_t[]){NTAG_CMD_READ, first},
                          NTAG_INDEXED_COMMAND_BYTES,
                          pages,
                          sizeof(pages));
        if (status != NFC_OK)
        {
            return status;
        }
        const uint16_t remaining = config->page_count - page;
        const uint16_t count =
            remaining < NTAG_READ_PAGES ? remaining : NTAG_READ_PAGES;
        memcpy(&dump->memory[page * NTAG_PAGE_BYTES],
               &pages[(page - first) * NTAG_PAGE_BYTES],
               count * NTAG_PAGE_BYTES);
    }
    dump->memory_size_bytes = config->page_count * NTAG_PAGE_BYTES;
    return NFC_OK;
}

static nfc_status_t read_metadata(const ntag_config_t *config,
                                  nfca_dump_t *dump)
{
    nfc_status_t status;
    status = exchange((const uint8_t[]){NTAG_CMD_READ_SIG, 0u},
                      NTAG_INDEXED_COMMAND_BYTES,
                      dump->signature,
                      sizeof(dump->signature));
    if (status != NFC_OK)
    {
        return status;
    }
    dump->metadata_valid |= NFCA_METADATA_SIGNATURE;
    for (uint8_t index = 0u; index < NTAG_COUNTER_COUNT; index++)
    {
        /* NTAG21x exposes only counter 2, and only when enabled. The
         * first READ may already have incremented it on the real tag. */
        if (config->is_ntag21x &&
            (index != NTAG21X_COUNTER_INDEX ||
             (dump->memory[(config->config_page + NTAG_CONFIG_ACCESS_PAGE) *
                           NTAG_PAGE_BYTES] &
              NTAG_ACCESS_NFC_CNT_EN) == 0u))
        {
            continue;
        }
        status = exchange((const uint8_t[]){NTAG_CMD_READ_CNT, index},
                          NTAG_INDEXED_COMMAND_BYTES,
                          dump->counters_le[index],
                          sizeof(dump->counters_le[index]));
        if (status != NFC_OK && status != NFC_ACCESS_DENIED)
        {
            return status;
        }
        if (status == NFC_OK)
        {
            dump->counter_valid |= (uint8_t)(1u << index);
        }
        if (config->is_ntag21x)
        {
            continue;
        }
        status = exchange((const uint8_t[]){NTAG_CMD_CHECK_TEARING, index},
                          NTAG_INDEXED_COMMAND_BYTES,
                          &dump->tearing[index],
                          1u);
        if (status != NFC_OK && status != NFC_ACCESS_DENIED)
        {
            return status;
        }
        if (status == NFC_OK)
        {
            dump->tearing_valid |= (uint8_t)(1u << index);
        }
    }

    return NFC_OK;
}

nfc_status_t ntag_reader_read(const ntag_config_t *config,
                              const uint8_t *password,
                              nfca_dump_t *dump)
{
    if (dump->tag.uid_size_bytes != NTAG_UID_BYTES || dump->tag.sak != 0u)
    {
        return NFC_UNSUPPORTED;
    }
    nfc_status_t status = ntag_access_reader_prepare(password, dump);
    if (status != NFC_OK)
    {
        return status;
    }
    status = read_memory(config, dump);
    if (status != NFC_OK || !config->ev1)
    {
        return status;
    }
    return read_metadata(config, dump);
}
