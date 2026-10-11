#include "cards/type2/ntag_internal.h"
#include "common/endian.h"

#include <string.h>
static FORCE_INLINE void single_counter_try_increase(void)
{
    if (!ntag.config.is_ntag21x || ntag.single_counter_increased ||
        (ntag.memory[ntag.config.config_page + NTAG_CONFIG_ACCESS_PAGE]
                    [NTAG_CONFIG_ACCESS_BYTE] &
         NTAG_ACCESS_NFC_CNT_EN) == 0u)
    {
        return;
    }
    if (ntag.counters[NTAG21X_COUNTER_INDEX] < NTAG_COUNTER_MAX)
    {
        ntag.counters[NTAG21X_COUNTER_INDEX]++;
    }
    ntag.single_counter_increased = true;
}

static inline bool counter_is_readable(uint8_t index)
{
    if (!ntag_counter_index_is_valid(index))
    {
        return false;
    }
    if (!ntag.config.is_ntag21x)
    {
        return true;
    }
    /* NTAG21x: counter index must be 2, enabled in config, and authorized if
     * protected. */
    const uint8_t access =
        ntag.memory[ntag.config.config_page + NTAG_CONFIG_ACCESS_PAGE]
                   [NTAG_CONFIG_ACCESS_BYTE];
    if (index != NTAG21X_COUNTER_INDEX ||
        (access & NTAG_ACCESS_NFC_CNT_EN) == 0u)
    {
        return false;
    }
    if ((access & NTAG_ACCESS_NFC_CNT_PWD_PROT) != 0u && !ntag.authenticated)
    {
        return false;
    }
    return true;
}

static bool RAM_FUNC(handle_compat_write_data)(const nfca_tag_request_t *frame,
                                               nfca_tag_response_t *response)
{
    if (!ntag.compat_write_pending)
    {
        return false;
    }
    ntag.compat_write_pending = false;
    if (frame->size_bytes !=
            NTAG_READ_PAGES * NTAG_PAGE_BYTES + NFC_CRC_BYTES ||
        !frame->parity_ok || !frame->crc_ok)
    {
        nfca_reply_nack(response, NTAG_NACK_CRC);
        return true;
    }
    if (!ntag_write_page(ntag.compat_write_page, frame->bytes))
    {
        nfca_reply_nack(response, NTAG_NACK_INVALID);
        return true;
    }
    nfca_reply_ack(response);
    return true;
}

static bool RAM_FUNC(handle_read)(const nfca_tag_request_t *frame,
                                  nfca_tag_response_t *response)
{
    const uint8_t *bytes = frame->bytes;
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_INDEXED_COMMAND_BYTES + NFC_CRC_BYTES)
    {
        return false;
    }
    const uint8_t start = bytes[1];
    if (start >= ntag.config.page_count)
    {
        nfca_reply_nack(response, NTAG_NACK_INVALID);
        return true;
    }
    for (uint8_t i = 0u; i < NTAG_READ_PAGES; i++)
    {
        const uint8_t page = (uint8_t)((start + i) % ntag.config.page_count);
        if (!ntag_read_page(
                page, &response->frame.bytes[(uint16_t)i * NTAG_PAGE_BYTES]))
        {
            nfca_reply_nack(response, NTAG_NACK_AUTH);
            return true;
        }
    }
    nfca_tag_response_data_crc(response, NTAG_READ_PAGES * NTAG_PAGE_BYTES);
    single_counter_try_increase();
    return true;
}

static bool RAM_FUNC(handle_fast_read)(const nfca_tag_request_t *frame,
                                       nfca_tag_response_t *response)
{
    const uint8_t *bytes = frame->bytes;
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_FAST_READ_COMMAND_BYTES + NFC_CRC_BYTES ||
        !ntag.config.ev1)
    {
        return false;
    }
    const uint8_t start = bytes[1];
    const uint8_t end = bytes[2];
    const uint16_t count = end >= start ? (uint16_t)end - start + 1u : 0u;
    if (count == 0u || end >= ntag.config.page_count ||
        count > NTAG_MAX_FAST_READ_PAGES)
    {
        nfca_reply_nack(response, NTAG_NACK_INVALID);
        return true;
    }
    for (uint8_t i = 0u; i < count; i++)
    {
        if (!ntag_read_page(
                (uint8_t)(start + i),
                &response->frame.bytes[(uint16_t)i * NTAG_PAGE_BYTES]))
        {
            nfca_reply_nack(response, NTAG_NACK_AUTH);
            return true;
        }
    }
    nfca_tag_response_data_crc(response, (uint16_t)(count * NTAG_PAGE_BYTES));
    single_counter_try_increase();
    return true;
}

static bool RAM_FUNC(handle_write)(const nfca_tag_request_t *frame,
                                   nfca_tag_response_t *response)
{
    const uint8_t *bytes = frame->bytes;
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_WRITE_COMMAND_BYTES + NFC_CRC_BYTES)
    {
        return false;
    }
    if (!ntag_write_page(bytes[1], &bytes[2]))
    {
        nfca_reply_nack(response, NTAG_NACK_INVALID);
        return true;
    }
    nfca_reply_ack(response);
    return true;
}

static bool RAM_FUNC(handle_compat_write)(const nfca_tag_request_t *frame,
                                          nfca_tag_response_t *response)
{
    const uint8_t *bytes = frame->bytes;
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_INDEXED_COMMAND_BYTES + NFC_CRC_BYTES)
    {
        return false;
    }
    const uint8_t page = bytes[1];
    if (page >= ntag.config.page_count || ntag_page_locked(page) ||
        !ntag_access_allowed(page, true))
    {
        nfca_reply_nack(response, NTAG_NACK_INVALID);
        return true;
    }
    ntag.compat_write_page = page;
    ntag.compat_write_pending = true;
    nfca_reply_ack(response);
    return true;
}

static bool RAM_FUNC(handle_get_version)(const nfca_tag_request_t *frame,
                                         nfca_tag_response_t *response)
{
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_OPCODE_BYTES + NFC_CRC_BYTES || !ntag.config.ev1)
    {
        return false;
    }
    memcpy(response->frame.bytes,
           ntag.config.version,
           sizeof(ntag.config.version));
    nfca_tag_response_data_crc(response, sizeof(ntag.config.version));
    return true;
}

static bool RAM_FUNC(handle_read_sig)(const nfca_tag_request_t *frame,
                                      nfca_tag_response_t *response)
{
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_INDEXED_COMMAND_BYTES + NFC_CRC_BYTES ||
        !ntag.config.ev1)
    {
        return false;
    }
    memcpy(response->frame.bytes, ntag.signature, sizeof(ntag.signature));
    nfca_tag_response_data_crc(response, sizeof(ntag.signature));
    return true;
}

static bool RAM_FUNC(handle_read_cnt)(const nfca_tag_request_t *frame,
                                      nfca_tag_response_t *response)
{
    const uint8_t *bytes = frame->bytes;
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_INDEXED_COMMAND_BYTES + NFC_CRC_BYTES ||
        !ntag.config.ev1)
    {
        return false;
    }
    const uint8_t index = bytes[1];
    if (!counter_is_readable(index))
    {
        nfca_reply_nack(response, NTAG_NACK_INVALID);
        return true;
    }
    write_le24(response->frame.bytes, ntag.counters[index]);
    nfca_tag_response_data_crc(response, NTAG_COUNTER_BYTES);
    return true;
}

static bool RAM_FUNC(handle_incr_cnt)(const nfca_tag_request_t *frame,
                                      nfca_tag_response_t *response)
{
    const uint8_t *bytes = frame->bytes;
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_COUNTER_UPDATE_COMMAND_BYTES + NFC_CRC_BYTES ||
        !ntag.config.ev1 || ntag.config.is_ntag21x)
    {
        return false;
    }
    const uint8_t index = bytes[1];
    const uint32_t increment = read_le24(&bytes[2]);
    if (!ntag_counter_index_is_valid(index) ||
        increment > NTAG_COUNTER_MAX - ntag.counters[index])
    {
        nfca_reply_nack(response, NTAG_NACK_AUTH);
        return true;
    }
    ntag.counters[index] += increment;
    ntag.tearing[index] = NTAG_TEARING_OK;
    nfca_reply_ack(response);
    return true;
}

static bool RAM_FUNC(handle_check_tearing)(const nfca_tag_request_t *frame,
                                           nfca_tag_response_t *response)
{
    const uint8_t *bytes = frame->bytes;
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_INDEXED_COMMAND_BYTES + NFC_CRC_BYTES ||
        !ntag.config.ev1 || ntag.config.is_ntag21x)
    {
        return false;
    }
    const uint8_t index = bytes[1];
    if (!ntag_counter_index_is_valid(index))
    {
        nfca_reply_nack(response, NTAG_NACK_INVALID);
        return true;
    }
    nfca_reply_byte_crc(response, ntag.tearing[index]);
    return true;
}

static bool RAM_FUNC(handle_vcsl)(const nfca_tag_request_t *frame,
                                  nfca_tag_response_t *response)
{
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_OPCODE_BYTES + NFC_CRC_BYTES || !ntag.config.ev1 ||
        ntag.config.is_ntag21x)
    {
        return false;
    }
    nfca_reply_byte_crc(
        response,
        ntag.memory[ntag.config.config_page + NTAG_CONFIG_ACCESS_PAGE]
                   [NTAG_CONFIG_VCTID_BYTE]);
    return true;
}

static bool RAM_FUNC(handle_pwd_auth)(const nfca_tag_request_t *frame,
                                      nfca_tag_response_t *response)
{
    const uint8_t *bytes = frame->bytes;
    const uint16_t size_bytes = frame->size_bytes;
    if (size_bytes != NTAG_OPCODE_BYTES + NTAG_PASSWORD_BYTES + NFC_CRC_BYTES ||
        !ntag.config.ev1)
    {
        return false;
    }
    const uint8_t access =
        ntag.memory[ntag.config.config_page + NTAG_CONFIG_ACCESS_PAGE]
                   [NTAG_CONFIG_ACCESS_BYTE];
    const uint8_t limit = access & NTAG_ACCESS_AUTHLIM_MASK;
    const bool match =
        memcmp(&bytes[1],
               ntag.memory[ntag.config.config_page + NTAG_CONFIG_PASSWORD_PAGE],
               NTAG_PASSWORD_BYTES) == 0;
    if ((limit != 0u && ntag.auth_attempts >= limit) || !match)
    {
        if (!match && limit != 0u && ntag.auth_attempts < UINT32_MAX)
        {
            ntag.auth_attempts++;
        }
        nfca_reply_nack(response, NTAG_NACK_AUTH);
        return true;
    }
    ntag.authenticated = true;
    ntag.auth_attempts = 0u;
    memcpy(response->frame.bytes,
           ntag.memory[ntag.config.config_page + NTAG_CONFIG_PACK_PAGE],
           NTAG_PACK_BYTES);
    nfca_tag_response_data_crc(response, NTAG_PACK_BYTES);
    return true;
}

static bool RAM_FUNC(ntag_handle_frame)(const nfca_tag_request_t *frame,
                                        nfca_tag_response_t *response)
{
    if (handle_compat_write_data(frame, response))
    {
        return true;
    }

    nfca_activation_event_t event;
    const nfca_activation_result_t activation_result = nfca_activation_handle(
        &ntag.activation, frame, &response->frame, &event);
    if (activation_result != NFCA_ACTIVATION_UNHANDLED)
    {
        if (event != NFCA_ACTIVATION_EVENT_NONE)
        {
            ntag.authenticated = false;
        }
        if (event == NFCA_ACTIVATION_EVENT_READY)
        {
            /* There is no separate field-off callback; REQA/WUPA is the
             * observable start of the next activation sequence. */
            ntag.single_counter_increased = false;
        }
        if (event == NFCA_ACTIVATION_EVENT_HALTED)
        {
            ntag.compat_write_pending = false;
        }
        return activation_result == NFCA_ACTIVATION_RESPONSE;
    }
    if (!nfca_activation_is_active(&ntag.activation))
    {
        return false;
    }

    if (frame->short_frame || !frame->parity_ok || !frame->crc_ok ||
        frame->size_bytes < NFC_CRC_BYTES)
    {
        return false;
    }

    switch (frame->bytes[0])
    {
    case NTAG_CMD_READ:
        if (handle_read(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_FAST_READ:
        if (handle_fast_read(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_WRITE:
        if (handle_write(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_COMPAT_WRITE:
        if (handle_compat_write(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_GET_VERSION:
        if (handle_get_version(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_READ_SIG:
        if (handle_read_sig(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_READ_CNT:
        if (handle_read_cnt(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_INCR_CNT:
        if (handle_incr_cnt(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_CHECK_TEARING:
        if (handle_check_tearing(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_VCSL:
        if (handle_vcsl(frame, response))
        {
            return true;
        }
        break;
    case NTAG_CMD_PWD_AUTH:
        if (handle_pwd_auth(frame, response))
        {
            return true;
        }
        break;
    default:
        break;
    }
    nfca_reply_nack(response, NTAG_NACK_INVALID);
    return true;
}

const nfca_tag_profile_t nfca_ntag_profile = {
    .handle_frame = ntag_handle_frame,
};
