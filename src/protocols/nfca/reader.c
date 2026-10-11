#include "protocols/nfca/reader.h"

#include "phy/pcd/reader_rf_trace.h"
#include "pn532/pn532_frontend.h"
#include "pn532_unleashed/integrity.h"
#include "pn532_unleashed/nfca_frame.h"

#include "pico/stdlib.h"

#include <string.h>

#define NFCA_READER_GUARD_US 100u
#define NFCA_SELECTION_ATTEMPTS 3u
#define NFCA_CASCADE_TAG_BYTES 1u

static bool active;

bool nfca_reader_stop(void)
{
    if (!active)
    {
        return true;
    }
    nfca_pcd_release();
    active = false;
    return pn532_frontend_stop();
}

nfc_status_t nfca_reader_transfer_at(const reader_frame_t *tx,
                                     nfca_pcd_response_t *rx,
                                     uint16_t max_bits,
                                     uint32_t deadline_ticks)
{
    if (!active || tx == NULL || rx == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    /* Keep field and comparator running. SPI must not separate a command
     * from its response, or restart an authenticated card session. */
    if (deadline_ticks == 0u)
    {
        sleep_us(NFCA_READER_GUARD_US);
    }
    const bool sent =
        deadline_ticks == 0u
            ? nfca_pcd_exchange(tx, rx, max_bits)
            : nfca_pcd_exchange_at(tx, rx, max_bits, deadline_ticks);
    READER_RF_TRACE_RESPONSE(sent,
                             rx->size_bits,
                             rx->framing_valid,
                             rx->parity_valid,
                             rx->collision);
    if (!sent)
    {
        return NFC_RF_ERROR;
    }
    if (!rx->framing_valid)
    {
        return rx->size_bits == 0u && !rx->collision ? NFC_NO_TAG
                                                     : NFC_RF_ERROR;
    }
    return NFC_OK;
}

nfc_status_t nfca_reader_transfer(const reader_frame_t *tx,
                                  nfca_pcd_response_t *rx,
                                  uint16_t max_bits)
{
    return nfca_reader_transfer_at(tx, rx, max_bits, 0u);
}

static nfc_status_t request_atqa(nfca_tag_t *tag, bool wakeup, uint8_t attempts)
{
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    (void)reader_frame_begin(&tx, 1u);
    tx.data[0] = wakeup ? NFCA_CMD_WUPA : NFCA_CMD_REQA;
    tx.last_byte_bits = NFCA_SHORT_REQUEST_BITS;
    READER_RF_TRACE_SELECT(READER_TRACE_SELECT_WAKEUP);
    nfc_status_t status = NFC_NO_TAG;
    for (uint8_t attempt = 0u; attempt < attempts; attempt++)
    {
        status = nfca_reader_transfer(&tx, &rx, sizeof(tag->atqa) * 8u);
        if (status != NFC_NO_TAG)
        {
            break;
        }
    }
    if (status != NFC_OK)
    {
        return status;
    }
    if (rx.size_bits != sizeof(tag->atqa) * 8u || !rx.parity_valid)
    {
        READER_RF_TRACE_REASON(READER_TRACE_RESPONSE_INVALID);
        return NFC_RF_ERROR;
    }
    memcpy(tag->atqa, rx.data, sizeof(tag->atqa));
    return NFC_OK;
}

static nfc_status_t select_cascade(nfca_tag_t *tag,
                                   const nfca_tag_t *known,
                                   uint8_t cascade,
                                   bool *complete)
{
    reader_frame_t tx;
    nfca_pcd_response_t rx;
    nfc_status_t status;
    /* CT is included in BCC, but never copied into the final UID. SAK's
     * cascade bit must agree with CT before another physical exchange. */
    static const uint8_t cascade_commands[NFCA_CASCADE_LEVELS] = {
        NFCA_CMD_ANTICOLL_CL1,
        NFCA_CMD_ANTICOLL_CL2,
        NFCA_CMD_ANTICOLL_CL3,
    };
    const uint8_t command = cascade_commands[cascade];
    if (known != NULL)
    {
        const bool more =
            (uint8_t)(known->uid_size_bytes - tag->uid_size_bytes) >
            NFCA_UID_FRAGMENT_BYTES;
        rx.data[0] = NFCA_CASCADE_TAG;
        memcpy(&rx.data[more ? NFCA_CASCADE_TAG_BYTES : 0u],
               &known->uid[tag->uid_size_bytes],
               more ? NFCA_CASCADE_UID_BYTES : NFCA_UID_FRAGMENT_BYTES);
        nfca_add_bcc(rx.data);
    }
    else
    {
        (void)reader_frame_begin(&tx, NFCA_COMMAND_HEADER_BYTES);
        tx.byte_parity = true;
        tx.data[0] = command;
        tx.data[1] = NFCA_ANTICOLLISION_NVB;
        READER_RF_TRACE_SELECT(READER_TRACE_SELECT_ANTICOLLISION);
        status = nfca_reader_transfer(&tx, &rx, NFCA_ANTICOLLISION_BYTES * 8u);
        if (status != NFC_OK)
        {
            return status;
        }
        if (rx.size_bits != NFCA_ANTICOLLISION_BYTES * 8u || !rx.parity_valid ||
            !nfca_bcc_valid(rx.data))
        {
            READER_RF_TRACE_REASON(READER_TRACE_RESPONSE_INVALID);
            return NFC_RF_ERROR;
        }
    }
    const bool more = rx.data[0] == NFCA_CASCADE_TAG;
    if (more && cascade + 1u == NFCA_CASCADE_LEVELS)
    {
        return NFC_RF_ERROR;
    }
    (void)reader_frame_begin(&tx, NFCA_SELECT_BYTES);
    tx.byte_parity = true;
    tx.data[0] = command;
    tx.data[1] = NFCA_SELECT_NVB;
    memcpy(
        &tx.data[NFCA_COMMAND_HEADER_BYTES], rx.data, NFCA_ANTICOLLISION_BYTES);
    nfca_append_crc(tx.data, NFCA_SELECT_BYTES - NFC_CRC_BYTES);
    const uint8_t uid_bytes =
        NFCA_UID_FRAGMENT_BYTES - (more ? NFCA_CASCADE_TAG_BYTES : 0u);
    memcpy(&tag->uid[tag->uid_size_bytes],
           &rx.data[more ? NFCA_CASCADE_TAG_BYTES : 0u],
           uid_bytes);
    tag->uid_size_bytes += uid_bytes;
    READER_RF_TRACE_SELECT(READER_TRACE_SELECT_SELECT);
    status = nfca_reader_transfer(&tx, &rx, NFCA_SAK_FRAME_BYTES * 8u);
    if (status != NFC_OK)
    {
        return status;
    }
    if (rx.size_bits != NFCA_SAK_FRAME_BYTES * 8u || !rx.parity_valid ||
        !nfca_check_crc(rx.data, NFCA_SAK_FRAME_BYTES) ||
        ((rx.data[0] & NFCA_SAK_CASCADE) != 0u) != more)
    {
        READER_RF_TRACE_REASON(READER_TRACE_RESPONSE_INVALID);
        return NFC_RF_ERROR;
    }
    *complete = !more;
    if (*complete)
    {
        tag->sak = rx.data[0];
    }
    return NFC_OK;
}

static nfc_status_t select_tag(nfca_tag_t *tag,
                               bool wakeup,
                               const nfca_tag_t *known,
                               uint8_t attempts)
{
    nfc_status_t status = request_atqa(tag, wakeup, attempts);
    if (status != NFC_OK)
    {
        return status;
    }
    for (uint8_t cascade = 0u; cascade < NFCA_CASCADE_LEVELS; cascade++)
    {
        bool complete;
        status = select_cascade(tag, known, cascade, &complete);
        if (status != NFC_OK)
        {
            return status;
        }
        if (complete)
        {
            return NFC_OK;
        }
    }
    return NFC_RF_ERROR;
}

static nfc_status_t start(nfca_tag_t *tag, uint8_t attempts, bool wakeup)
{
    if (tag == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t status = nfca_reader_open();
    if (status != NFC_OK)
    {
        return status;
    }
    *tag = (nfca_tag_t){0};
    return select_tag(tag, wakeup, NULL, attempts);
}

nfc_status_t nfca_reader_open(void)
{
    if (active)
    {
        return NFC_BUSY;
    }
    if (!pn532_frontend_prepare_reader(PN532_PROTOCOL_NFCA))
    {
        return NFC_RF_ERROR;
    }
    active = true;
    if (!nfca_pcd_init() || !pn532_frontend_set_field(true))
    {
        return NFC_RF_ERROR;
    }
    return NFC_OK;
}

nfc_status_t nfca_reader_select_tag(nfca_tag_t *tag)
{
    if (!active || tag == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    nfca_tag_t selected = {0};
    const nfc_status_t status = select_tag(&selected, true, NULL, 1u);
    if (status == NFC_OK)
    {
        *tag = selected;
    }
    return status;
}

nfc_status_t nfca_reader_transfer_frame(const reader_frame_t *tx,
                                        nfca_pcd_response_t *rx,
                                        uint32_t sof_timeout_us,
                                        bool expect_response)
{
    if (!active)
    {
        return NFC_INVALID_ARGUMENT;
    }
    sleep_us(NFCA_READER_GUARD_US);
    if (!expect_response)
    {
        return nfca_pcd_send(tx) ? NFC_OK : NFC_RF_ERROR;
    }
    if (!nfca_pcd_exchange_timed(
            tx, rx, NFCA_PCD_RX_MAX_BYTES * 8u, sof_timeout_us))
    {
        return NFC_RF_ERROR;
    }
    if (!rx->framing_valid || rx->collision)
    {
        return rx->size_bits == 0u && !rx->collision && !rx->sof_seen
                   ? NFC_NO_RESPONSE
                   : NFC_INVALID_RESPONSE;
    }
    return NFC_OK;
}

nfc_status_t nfca_reader_open_selected(nfca_tag_t *tag)
{
    return start(tag, NFCA_SELECTION_ATTEMPTS, false);
}

nfc_status_t nfca_reader_scan_start(nfca_tag_t *tag)
{
    /* WUPA also selects halted tags and permits repeated single probes. */
    return start(tag, 1u, true);
}

nfc_status_t nfca_reader_restart(const nfca_tag_t *expected)
{
    if (expected == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    if (!nfca_reader_stop())
    {
        return NFC_RF_ERROR;
    }
    sleep_us(5000u);
    const nfc_status_t status = nfca_reader_open();
    return status == NFC_OK ? nfca_reader_reselect(expected) : status;
}

nfc_status_t nfca_reader_reselect(const nfca_tag_t *expected)
{
    nfca_tag_t tag = {0};
    const nfc_status_t status =
        select_tag(&tag, true, NULL, NFCA_SELECTION_ATTEMPTS);
    if (status != NFC_OK)
    {
        return status;
    }
    READER_RF_TRACE_SELECT(READER_TRACE_SELECT_IDENTITY);
    return memcmp(&tag, expected, sizeof(tag)) == 0 ? NFC_OK : NFC_RF_ERROR;
}

nfc_status_t nfca_reader_reselect_known(const nfca_tag_t *expected)
{
    if (expected == NULL ||
        (expected->uid_size_bytes != NFCA_UID_SINGLE_BYTES &&
         expected->uid_size_bytes != NFCA_UID_DOUBLE_BYTES))
    {
        return NFC_INVALID_ARGUMENT;
    }
    nfca_tag_t tag = {0};
    const nfc_status_t status =
        select_tag(&tag, true, expected, NFCA_SELECTION_ATTEMPTS);
    if (status != NFC_OK)
    {
        return status;
    }
    READER_RF_TRACE_SELECT(READER_TRACE_SELECT_IDENTITY);
    return memcmp(&tag, expected, sizeof(tag)) == 0 ? NFC_OK : NFC_RF_ERROR;
}
