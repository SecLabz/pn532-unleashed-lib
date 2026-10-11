#include "protocols/nfcv/reader.h"

#include "board/activity_led.h"
#include "phy/nfcv/pcd/nfcv_pcd_rx.h"
#include "phy/nfcv/pcd/nfcv_pcd_tx.h"
#include "phy/pcd/pn532_reader_sequence.h"
#include "phy/pcd/reader_subcarrier_level_rx.h"
#include "phy/reader_frame.h"
#include "pn532/pn532_frontend.h"
#include "pn532_unleashed/integrity.h"
#include "pn532_unleashed/nfcv_frame.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <string.h>

#define NFCV_READER_ATTEMPTS 3u
#define NFCV_RETRY_DELAY_US 1000u
#define NFCV_HEADER_BYTES NFCV_REQUEST_HEADER_BYTES
#define NFCV_ADDRESSED_BYTES (NFCV_HEADER_BYTES + NFCV_UID_SIZE)
#define NFCV_MIN_REPLY_BYTES (1u + NFC_CRC_BYTES)
#define NFCV_INVENTORY_REPLY_BYTES                                             \
    (NFCV_HEADER_BYTES + NFCV_UID_SIZE + NFC_CRC_BYTES)
#define NFCV_BLOCK_REPLY_BYTES                                                 \
    (NFCV_HEADER_BYTES + NFCV_BLOCK_BYTES + NFC_CRC_BYTES)

static bool active;

nfc_status_t nfcv_reader_open(bool continuous_field)
{
    if (active)
    {
        return NFC_BUSY;
    }
    if (!pn532_frontend_prepare_reader(PN532_PROTOCOL_NFCV))
    {
        return NFC_RF_ERROR;
    }
    active = true;
    if (!nfcv_pcd_tx_init() || !nfcv_pcd_rx_init() ||
        (continuous_field && !pn532_frontend_set_field(true)))
    {
        (void)nfcv_reader_stop();
        return NFC_RF_ERROR;
    }
    return NFC_OK;
}

typedef struct
{
    bool sent;
    bool received;
} exchange_result_t;

static inline exchange_result_t transmit_capture(const reader_frame_t *tx,
                                                 nfcv_pcd_rx_result_t *rx,
                                                 uint32_t sof_timeout_us,
                                                 bool expect_response,
                                                 bool checked_capture)
{
    const uint32_t saved = save_and_disable_interrupts();
    exchange_result_t result;
    result.sent = nfcv_pcd_tx_send(tx);
    result.received =
        result.sent && expect_response &&
        (checked_capture ? nfcv_pcd_rx_capture(rx)
                         : nfcv_pcd_rx_capture_timed(rx, sof_timeout_us));
    restore_interrupts(saved);
    return result;
}

nfc_status_t nfcv_reader_transfer_frame(const reader_frame_t *tx,
                                        nfcv_pcd_rx_result_t *rx,
                                        uint32_t sof_timeout_us,
                                        bool expect_response)
{
    if (!active)
    {
        return NFC_INVALID_ARGUMENT;
    }
    *rx = (nfcv_pcd_rx_result_t){0};
    /* Rearm the PN532 comparator between commands without dropping the
     * field. Its internal decoder may leave Receive after a custom frame. */
    if (!pn532_frontend_set_field(true))
    {
        return NFC_RF_ERROR;
    }
    const exchange_result_t result =
        transmit_capture(tx, rx, sof_timeout_us, expect_response, false);
    board_activity_led_record(result.sent,
                              result.sent && rx->framing_valid &&
                                  rx->eof_seen && !rx->truncated);
    board_activity_led_task();
    if (!result.sent)
    {
        return NFC_RF_ERROR;
    }
    if (!expect_response)
    {
        return NFC_OK;
    }
    if (!result.received || !rx->framing_valid || !rx->eof_seen ||
        rx->truncated)
    {
        return !rx->sof_seen && rx->size_bytes == 0u && !rx->truncated
                   ? NFC_NO_RESPONSE
                   : NFC_INVALID_RESPONSE;
    }
    return NFC_OK;
}

bool nfcv_reader_stop(void)
{
    if (active)
    {
        reader_subcarrier_level_rx_release();
        pn532_reader_sequence_release();
        active = false;
        return pn532_frontend_stop();
    }
    return true;
}

static nfc_status_t
exchange(const reader_frame_t *tx, nfcv_pcd_rx_result_t *rx, uint8_t attempts)
{
    nfc_status_t status = NFC_NO_TAG;
    for (uint8_t attempt = 0u; attempt < attempts; attempt++)
    {
        *rx = (nfcv_pcd_rx_result_t){0};
        if (!pn532_frontend_set_field(true))
        {
            return NFC_RF_ERROR;
        }
        const exchange_result_t result =
            transmit_capture(tx, rx, 0u, true, true);
        const bool stopped = pn532_frontend_set_field(false);
        board_activity_led_record(result.sent,
                                  result.sent && rx->framing_valid &&
                                      rx->eof_seen && !rx->truncated);
        board_activity_led_task();
        if (!result.sent || !stopped)
        {
            return NFC_RF_ERROR;
        }
        if (result.received && rx->framing_valid && rx->sof_seen &&
            rx->eof_seen && rx->crc_ok && !rx->truncated &&
            rx->size_bytes >= NFCV_MIN_REPLY_BYTES &&
            rx->size_bytes <= sizeof(rx->bytes))
        {
            return rx->bytes[0] == NFCV_RES_OK ? NFC_OK : NFC_RF_ERROR;
        }
        if (rx->sof_seen || rx->size_bytes != 0u)
        {
            status = NFC_RF_ERROR;
        }
        if (attempt + 1u < attempts)
        {
            sleep_us(NFCV_RETRY_DELAY_US);
        }
    }
    return status;
}

static bool addressed(reader_frame_t *tx,
                      uint8_t command,
                      const uint8_t uid[NFCV_UID_SIZE],
                      const uint8_t *parameters,
                      uint8_t size_bytes,
                      bool option)
{
    if (!reader_frame_begin(tx,
                            NFCV_ADDRESSED_BYTES + size_bytes + NFC_CRC_BYTES))
    {
        return false;
    }
    tx->data[0] = NFCV_REQ_DATARATE_HIGH | NFCV_REQ_ADDRESS |
                  (option ? NFCV_REQ_OPTION : 0u);
    tx->data[1] = command;
    memcpy(&tx->data[NFCV_HEADER_BYTES], uid, NFCV_UID_SIZE);
    if (size_bytes != 0u)
    {
        memcpy(&tx->data[NFCV_ADDRESSED_BYTES], parameters, size_bytes);
    }
    nfcv_append_crc(tx->data, NFCV_ADDRESSED_BYTES + size_bytes);
    return true;
}

/* Retain the RF-order UID for addressed commands; the public identity uses
 * display order. Scan and SLIX read supply their own fixed retry bounds. */
static nfc_status_t
discover(nfcv_tag_t *tag, uint8_t uid[NFCV_UID_SIZE], uint8_t attempts)
{
    reader_frame_t tx;
    nfcv_pcd_rx_result_t rx;
    (void)reader_frame_begin(&tx, NFCV_HEADER_BYTES + 1u + NFC_CRC_BYTES);
    tx.data[0] = NFCV_REQ_DATARATE_HIGH | NFCV_REQ_INVENTORY | NFCV_REQ_SLOT1;
    tx.data[1] = NFCV_CMD_INVENTORY;
    tx.data[NFCV_HEADER_BYTES] = 0u; /* No UID mask. */
    nfcv_append_crc(tx.data, NFCV_HEADER_BYTES + 1u);
    nfc_status_t status = exchange(&tx, &rx, attempts);
    if (status != NFC_OK)
    {
        return status;
    }
    if (rx.size_bytes != NFCV_INVENTORY_REPLY_BYTES)
    {
        return NFC_RF_ERROR;
    }
    memcpy(uid, &rx.bytes[NFCV_HEADER_BYTES], NFCV_UID_SIZE);
    if (!addressed(&tx, NFCV_CMD_GET_SYSTEM_INFO, uid, NULL, 0u, false))
    {
        return NFC_INVALID_ARGUMENT;
    }
    status = exchange(&tx, &rx, attempts);
    if (status != NFC_OK)
    {
        return status;
    }
    if (rx.size_bytes != (NFCV_SYSINFO_PAYLOAD_BYTES + NFC_CRC_BYTES) ||
        rx.bytes[NFCV_SYSINFO_FLAGS_BYTE] != NFCV_SYSINFO_FIELDS_ALL ||
        memcmp(&rx.bytes[NFCV_SYSINFO_UID_BYTE], uid, NFCV_UID_SIZE) != 0)
    {
        return NFC_RF_ERROR;
    }
    for (uint8_t index = 0u; index < NFCV_UID_SIZE; index++)
    {
        tag->uid[index] = uid[NFCV_UID_SIZE - 1u - index];
    }
    const uint16_t count =
        (uint16_t)rx.bytes[NFCV_SYSINFO_BLOCK_COUNT_BYTE] + 1u;
    const uint8_t block_size_bytes =
        (uint8_t)((rx.bytes[NFCV_SYSINFO_BLOCK_SIZE_BYTE] &
                   NFCV_SYSINFO_BLOCK_SIZE_MASK) +
                  1u);
    if (tag->uid[0] != NFCV_UID_PREFIX || count > UINT8_MAX)
    {
        return NFC_UNSUPPORTED;
    }
    tag->dsfid = rx.bytes[NFCV_SYSINFO_DSFID_BYTE];
    tag->afi = rx.bytes[NFCV_SYSINFO_AFI_BYTE];
    tag->ic_ref = rx.bytes[NFCV_SYSINFO_IC_REF_BYTE];
    tag->block_count = (uint8_t)count;
    tag->block_size_bytes = block_size_bytes;
    return NFC_OK;
}

static nfc_status_t read_blocks(nfcv_slix_dump_t *dump,
                                const uint8_t uid[NFCV_UID_SIZE])
{
    const nfcv_tag_t *tag = &dump->tag;
    if (tag->uid[1] != NFCV_MANUFACTURER_NXP ||
        tag->block_size_bytes != NFCV_BLOCK_BYTES ||
        !((tag->uid[2] == SLIX_PRODUCT_CODE &&
           tag->block_count == SLIX_BLOCK_COUNT) ||
          (tag->uid[2] == SLIX_L_PRODUCT_CODE &&
           tag->block_count == SLIX_L_BLOCK_COUNT)))
    {
        return NFC_UNSUPPORTED;
    }
    reader_frame_t tx;
    nfcv_pcd_rx_result_t rx;
    for (uint8_t block = 0u; block < tag->block_count; block++)
    {
        if (!addressed(&tx, NFCV_CMD_READ_SINGLE, uid, &block, 1u, true))
        {
            return NFC_INVALID_ARGUMENT;
        }
        const nfc_status_t status = exchange(&tx, &rx, NFCV_READER_ATTEMPTS);
        if (status != NFC_OK)
        {
            return status;
        }
        if (rx.size_bytes != NFCV_BLOCK_REPLY_BYTES || rx.bytes[1] > 1u)
        {
            return NFC_RF_ERROR;
        }
        dump->security[block] = rx.bytes[1];
        memcpy(&dump->memory[block * NFCV_BLOCK_BYTES],
               &rx.bytes[NFCV_HEADER_BYTES],
               NFCV_BLOCK_BYTES);
    }
    return NFC_OK;
}

/* Cleanup takes precedence: a valid result must never escape a failed stop. */
static nfc_status_t finish(nfc_status_t status)
{
    return nfcv_reader_stop() ? status : NFC_RF_ERROR;
}

nfc_status_t nfcv_reader_scan(nfcv_tag_t *tag)
{
    if (tag == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    if (nfcv_reader_open(false) != NFC_OK)
    {
        (void)nfcv_reader_stop();
        return NFC_RF_ERROR;
    }
    nfcv_tag_t staged = {0};
    uint8_t uid[NFCV_UID_SIZE];
    const nfc_status_t status = finish(discover(&staged, uid, 1u));
    if (status == NFC_OK)
    {
        *tag = staged;
    }
    return status;
}

nfc_status_t nfcv_reader_slix_read(nfcv_slix_dump_t *dump)
{
    if (dump == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    if (nfcv_reader_open(false) != NFC_OK)
    {
        (void)nfcv_reader_stop();
        return NFC_RF_ERROR;
    }
    nfcv_slix_dump_t staged = {0};
    uint8_t uid[NFCV_UID_SIZE];
    nfc_status_t status = discover(&staged.tag, uid, NFCV_READER_ATTEMPTS);
    if (status == NFC_OK)
    {
        status = read_blocks(&staged, uid);
    }
    status = finish(status);
    if (status == NFC_OK)
    {
        staged.memory_size_bytes =
            staged.tag.block_count * staged.tag.block_size_bytes;
        *dump = staged;
    }
    return status;
}
