#include "api_internal.h"
#include "pn532_unleashed/nfca_desfire.h"
#include "protocols/nfca/reader.h"

#include "pico/stdlib.h"

#include <string.h>

#define ISODEP_BUDGET_US 750000u
#define ISODEP_MAX_FWT_US 100000u
#define ISODEP_MAX_BLOCKS 64u
#define ISODEP_MAX_WTX 8u
#define ISODEP_READER_FRAME_BYTES 32u

#define ISODEP_ATS_RESERVED_MASK 0x80u
#define ISODEP_ATS_FSCI_MASK 0x0fu
#define ISODEP_ATS_MAX_FSCI 8u
#define ISODEP_ATS_TA_PRESENT 0x10u
#define ISODEP_ATS_TB_PRESENT 0x20u
#define ISODEP_ATS_TC_PRESENT 0x40u
#define ISODEP_ATS_TC_SUPPORTED_MASK 0x03u
#define ISODEP_TIMING_EXPONENT_MASK 0x0fu
#define ISODEP_TIMING_RESERVED 15u
#define ISODEP_DEFAULT_FWI 4u
#define ISODEP_WTX_PCB 0xf2u
#define ISODEP_WTX_MAX_MULTIPLIER 59u
#define ISODEP_BLOCK_NUMBER_MASK 0x01u
#define ISODEP_CHAINING_MASK 0x10u
#define ISODEP_I_BLOCK_VARIABLE_MASK 0x11u
#define ISODEP_R_ACK_PCB 0xa2u
#define ISODEP_NATIVE_ADDITIONAL_FRAME 0xafu
#define ISODEP_CARRIER_HZ 13560000u
#define ISODEP_FRAME_DELAY_NUMERATOR 4096000000u

typedef struct
{
    uint32_t started_us;
    uint8_t blocks;
    uint8_t wtx;
} exchange_budget_t;

static nfc_status_t exchange(const uint8_t *bytes,
                             size_t size_bytes,
                             nfca_pcd_response_t *rx,
                             uint32_t timeout_us,
                             exchange_budget_t *budget)
{
    const uint32_t elapsed_us = time_us_32() - budget->started_us;
    if (budget->blocks >= ISODEP_MAX_BLOCKS || elapsed_us >= ISODEP_BUDGET_US)
    {
        return NFC_INVALID_RESPONSE;
    }
    budget->blocks++;
    const uint32_t remaining_us = ISODEP_BUDGET_US - elapsed_us;
    if (timeout_us > remaining_us)
    {
        timeout_us = remaining_us;
    }
    reader_frame_t tx;
    if (!reader_frame_begin(&tx, size_bytes + NFC_CRC_BYTES))
    {
        return NFC_INVALID_ARGUMENT;
    }
    memcpy(tx.data, bytes, size_bytes);
    nfca_append_crc(tx.data, size_bytes);
    tx.byte_parity = true;
    const nfc_status_t status =
        nfca_reader_transfer_frame(&tx, rx, timeout_us, true);
    if (status != NFC_OK)
    {
        return status;
    }
    if ((uint32_t)(time_us_32() - budget->started_us) >= ISODEP_BUDGET_US ||
        rx->size_bits % 8u != 0u || rx->size_bits < 24u || !rx->parity_valid ||
        !nfca_check_crc(rx->data, rx->size_bits / 8u))
    {
        return NFC_INVALID_RESPONSE;
    }
    return NFC_OK;
}

/* Preserve the existing rounding and unsynchronized-clock margin. */
static uint32_t frame_delay_us(uint8_t exponent)
{
    return (uint32_t)(((uint64_t)ISODEP_FRAME_DELAY_NUMERATOR << exponent) /
                          ISODEP_CARRIER_HZ +
                      1u);
}

static nfc_status_t parse_ats(desfire_session_t *session,
                              const nfca_pcd_response_t *rx)
{
    static const uint16_t sizes[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
    const size_t size_bytes = rx->size_bits / 8u - NFC_CRC_BYTES;
    const uint8_t *ats = rx->data;
    if (size_bytes < 2u || size_bytes > sizeof(session->ats) ||
        ats[0] != size_bytes || (ats[1] & ISODEP_ATS_RESERVED_MASK) != 0u ||
        (ats[1] & ISODEP_ATS_FSCI_MASK) > ISODEP_ATS_MAX_FSCI)
    {
        return NFC_INVALID_RESPONSE;
    }
    size_t offset = 2u;
    if ((ats[1] & ISODEP_ATS_TA_PRESENT) != 0u)
    {
        offset++;
    }
    uint8_t fwi = ISODEP_DEFAULT_FWI;
    uint8_t sfgi = 0u;
    if ((ats[1] & ISODEP_ATS_TB_PRESENT) != 0u)
    {
        if (offset >= size_bytes ||
            (ats[offset] & ISODEP_TIMING_EXPONENT_MASK) ==
                ISODEP_TIMING_RESERVED)
        {
            return NFC_INVALID_RESPONSE;
        }
        sfgi = ats[offset] & ISODEP_TIMING_EXPONENT_MASK;
        fwi = ats[offset++] >> 4;
    }
    if ((ats[1] & ISODEP_ATS_TC_PRESENT) != 0u)
    {
        if (offset >= size_bytes ||
            (ats[offset] & ~ISODEP_ATS_TC_SUPPORTED_MASK) != 0u)
        {
            return NFC_INVALID_RESPONSE;
        }
        offset++;
    }
    if (offset > size_bytes || fwi == ISODEP_TIMING_RESERVED)
    {
        return NFC_INVALID_RESPONSE;
    }
    /* ceil(4096/fc * 2^FWI), in microseconds; clock is not field-synchronous.
     */
    const uint32_t fwt_us = frame_delay_us(fwi);
    if (fwt_us > ISODEP_MAX_FWT_US)
    {
        return NFC_UNSUPPORTED;
    }
    if (sfgi != 0u)
    {
        const uint32_t guard_us = frame_delay_us(sfgi);
        if (guard_us > ISODEP_MAX_FWT_US)
        {
            return NFC_UNSUPPORTED;
        }
        sleep_us(guard_us);
    }
    memcpy(session->ats, ats, size_bytes);
    session->ats_size_bytes = (uint8_t)size_bytes;
    session->frame_size_bytes = sizes[ats[1] & ISODEP_ATS_FSCI_MASK];
    session->fwt_us = fwt_us;
    return NFC_OK;
}

nfc_status_t desfire_activate(desfire_session_t *session)
{
    if (session == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t ready = nfc_session_require(NFC_STATE_READING_NFCA);
    if (ready != NFC_OK)
    {
        return ready;
    }
    memset(session, 0, sizeof(*session));
    nfc_status_t status = nfca_reader_select(&session->tag);
    if (status != NFC_OK)
    {
        return status;
    }
    if ((session->tag.sak & 0x20u) == 0u)
    {
        return NFC_UNSUPPORTED;
    }
    /* FSDI=2 advertises 32 bytes, including PCB and CRC; CID=0. */
    const uint8_t rats[] = {0xe0u, 0x20u};
    nfca_pcd_response_t rx;
    exchange_budget_t budget = {.started_us = time_us_32()};
    status = exchange(rats, sizeof(rats), &rx, 5000u, &budget);
    if (status == NFC_OK)
    {
        status = parse_ats(session, &rx);
    }
    session->active = status == NFC_OK;
    return nfc_session_result(status);
}

static bool public_command(const uint8_t *command, size_t size_bytes)
{
    switch (command[0])
    {
    case 0x60: /* GetVersion */
    case 0x6e: /* FreeMemory */
    case 0x45: /* GetKeySettings */
    case 0x6a: /* GetApplicationIDs */
    case 0x6f: /* GetFileIDs */
        return size_bytes == 1u;
    case 0x64: /* GetKeyVersion */
        return size_bytes == 2u && command[1] < 14u;
    case 0xf5: /* GetFileSettings */
    case 0x6c: /* GetValue */
        return size_bytes == 2u && command[1] < 32u;
    case 0x5a: /* SelectApplication */
        return size_bytes == 4u;
    case 0xbd: /* ReadData */
    case 0xbb: /* ReadRecords */
        return size_bytes == 8u && command[1] < 32u;
    default:
        return false;
    }
}

static nfc_status_t receive_wtx(desfire_session_t *session,
                                nfca_pcd_response_t *rx,
                                exchange_budget_t *budget)
{
    while (rx->data[0] == ISODEP_WTX_PCB)
    {
        const uint8_t multiplier = rx->data[1];
        if (rx->size_bits != 32u || multiplier == 0u ||
            multiplier > ISODEP_WTX_MAX_MULTIPLIER ||
            budget->wtx >= ISODEP_MAX_WTX)
        {
            return NFC_INVALID_RESPONSE;
        }
        budget->wtx++;
        const uint8_t reply[] = {ISODEP_WTX_PCB, multiplier};
        const nfc_status_t status = exchange(
            reply, sizeof(reply), rx, session->fwt_us * multiplier, budget);
        if (status != NFC_OK)
        {
            return status;
        }
    }
    return NFC_OK;
}

static nfc_status_t native_exchange(desfire_session_t *session,
                                    const uint8_t *command,
                                    size_t command_size_bytes,
                                    uint8_t *reply,
                                    size_t capacity_bytes,
                                    size_t *size_bytes,
                                    uint8_t *native_status,
                                    exchange_budget_t *budget)
{
    uint8_t tx[ISODEP_READER_FRAME_BYTES] = {ISODEP_PCB_I_BLOCK_0 |
                                             session->tx_block};
    memcpy(&tx[1], command, command_size_bytes);
    nfca_pcd_response_t rx;
    nfc_status_t status =
        exchange(tx, command_size_bytes + 1u, &rx, session->fwt_us, budget);
    bool first = true;
    while (status == NFC_OK)
    {
        status = receive_wtx(session, &rx, budget);
        if (status != NFC_OK)
        {
            return status;
        }
        const size_t size = rx.size_bits / 8u;
        const uint8_t pcb = rx.data[0];
        if (size > ISODEP_READER_FRAME_BYTES ||
            (pcb & ~ISODEP_I_BLOCK_VARIABLE_MASK) != ISODEP_PCB_I_BLOCK_0)
        {
            return NFC_INVALID_RESPONSE;
        }
        if ((pcb & ISODEP_BLOCK_NUMBER_MASK) != session->rx_block)
        {
            /* Duplicate chained I-block: acknowledge again without copying.
             * An unexpected block before any fragment cannot be old data. */
            if (first)
            {
                return NFC_INVALID_RESPONSE;
            }
        }
        else
        {
            size_t start = 1u;
            if (first)
            {
                if (size < 4u)
                {
                    return NFC_INVALID_RESPONSE;
                }
                *native_status = rx.data[start++];
            }
            const size_t count = size - NFC_CRC_BYTES - start;
            if (count == 0u && (pcb & ISODEP_CHAINING_MASK) != 0u)
            {
                return NFC_INVALID_RESPONSE;
            }
            if (count > capacity_bytes - *size_bytes)
            {
                return NFC_BUFFER_TOO_SMALL;
            }
            memcpy(&reply[*size_bytes], &rx.data[start], count);
            *size_bytes += count;
            session->rx_block ^= 1u;
            first = false;
            if ((pcb & ISODEP_CHAINING_MASK) == 0u)
            {
                session->tx_block ^= 1u;
                return NFC_OK;
            }
        }
        const uint8_t ack[] = {ISODEP_R_ACK_PCB | session->rx_block};
        status = exchange(ack, sizeof(ack), &rx, session->fwt_us, budget);
    }
    return status;
}

nfc_status_t desfire_exchange(desfire_session_t *session,
                              const uint8_t *command,
                              size_t command_size_bytes,
                              uint8_t *reply,
                              size_t capacity_bytes,
                              size_t *size_bytes,
                              uint8_t *native_status)
{
    if (size_bytes == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    *size_bytes = 0u;
    if (session == NULL || !session->active || command == NULL ||
        command_size_bytes == 0u ||
        !public_command(command, command_size_bytes) || reply == NULL ||
        native_status == NULL || capacity_bytes == 0u ||
        capacity_bytes > DESFIRE_MAX_REPLY_BYTES)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t ready = nfc_session_require(NFC_STATE_READING_NFCA);
    if (ready != NFC_OK)
    {
        session->active = false;
        return ready;
    }
    exchange_budget_t budget = {.started_us = time_us_32()};
    const uint8_t additional_frame = ISODEP_NATIVE_ADDITIONAL_FRAME;
    const bool get_version = command[0] == 0x60u;
    for (uint8_t part = 0u; part < ISODEP_MAX_BLOCKS; part++)
    {
        const size_t previous_size = *size_bytes;
        const nfc_status_t status = native_exchange(session,
                                                    command,
                                                    command_size_bytes,
                                                    reply,
                                                    capacity_bytes,
                                                    size_bytes,
                                                    native_status,
                                                    &budget);
        if (status != NFC_OK)
        {
            session->active = false;
            *size_bytes = 0u;
            return nfc_session_result(status);
        }
        if (get_version && (*native_status == 0u || *native_status == 0xafu) &&
            ((part < 2u &&
              (*native_status != 0xafu || *size_bytes - previous_size != 7u)) ||
             (part == 2u &&
              (*native_status != 0u || *size_bytes - previous_size != 14u)) ||
             part > 2u))
        {
            session->active = false;
            *size_bytes = 0u;
            return nfc_session_result(NFC_INVALID_RESPONSE);
        }
        if (*native_status != 0xafu)
        {
            return NFC_OK;
        }
        if (*size_bytes == previous_size)
        {
            break;
        }
        command = &additional_frame;
        command_size_bytes = 1u;
    }
    session->active = false;
    *size_bytes = 0u;
    return nfc_session_result(NFC_INVALID_RESPONSE);
}
