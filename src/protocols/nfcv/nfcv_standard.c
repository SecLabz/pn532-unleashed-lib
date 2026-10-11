#include "pn532_unleashed/nfcv_standard.h"

#include <string.h>

void nfcv_standard_reset_session(nfcv_standard_t *standard)
{
    standard->state = NFCV_STANDARD_READY;
}

void nfcv_standard_quiet(nfcv_standard_t *standard)
{
    standard->state = NFCV_STANDARD_QUIET;
}

void nfcv_standard_set_uid(nfcv_standard_t *standard,
                           const uint8_t uid[NFCV_UID_SIZE])
{
    memcpy(standard->uid, uid, sizeof(standard->uid));
}

bool nfcv_standard_init(nfcv_standard_t *standard,
                        nfcv_memory_store_t *memory,
                        const uint8_t uid[NFCV_UID_SIZE],
                        uint8_t dsfid,
                        uint8_t afi,
                        uint8_t ic_ref,
                        bool dsfid_locked,
                        bool afi_locked)
{
    if (standard == NULL || memory == NULL || uid == NULL)
    {
        return false;
    }
    if (uid[0] != NFCV_UID_PREFIX ||
        memory->block_size_bytes != NFCV_MEMORY_MAX_BLOCK_SIZE)
    {
        return false;
    }
    if (memory->block_count != NFCV_MEMORY_MAX_BLOCKS)
    {
        uint8_t product;
        switch (memory->block_count)
        {
        case SLIX_BLOCK_COUNT:
            product = SLIX_PRODUCT_CODE;
            break;
        case SLIX_L_BLOCK_COUNT:
            product = SLIX_L_PRODUCT_CODE;
            break;
        default:
            return false;
        }
        if (uid[1] != NFCV_MANUFACTURER_NXP || uid[2] != product)
        {
            return false;
        }
    }
    standard->memory = memory;
    nfcv_standard_set_uid(standard, uid);
    standard->dsfid = dsfid;
    standard->afi = afi;
    standard->ic_ref = ic_ref;
    standard->dsfid_locked = dsfid_locked;
    standard->afi_locked = afi_locked;
    nfcv_standard_reset_session(standard);
    return true;
}

bool nfcv_standard_prepare(const nfcv_standard_t *standard,
                           const nfcv_frame_t *frame,
                           nfcv_request_t *request)
{
    if (standard == NULL || frame == NULL || request == NULL ||
        frame->size_bytes > sizeof(frame->bytes) ||
        frame->size_bytes <
            (uint16_t)(NFCV_REQUEST_HEADER_BYTES + NFCV_CRC_SIZE) ||
        !frame->crc_ok)
    {
        return false;
    }

    const uint16_t payload_len = (uint16_t)(frame->size_bytes - NFCV_CRC_SIZE);
    const uint8_t *bytes = frame->bytes;
    uint16_t index = NFCV_REQUEST_HEADER_BYTES;

    request->flags = bytes[0];
    request->command = bytes[1];
    request->inventory = (request->flags & NFCV_REQ_INVENTORY) != 0u;
    request->addressed = false;
    request->selected = false;
    request->option = (request->flags & NFCV_REQ_OPTION) != 0u;
    request->params = NULL;
    request->params_size_bytes = 0u;

    if (request->inventory)
    {
        if ((request->flags & NFCV_REQ_AFI) != 0u)
        {
            if (index >= payload_len || bytes[index] != standard->afi)
            {
                return false;
            }
            index++;
        }
        request->params = &bytes[index];
        request->params_size_bytes = (uint8_t)(payload_len - index);
        return true;
    }

    if (request->command >= NFCV_CMD_CUSTOM_MIN &&
        request->command <= NFCV_CMD_CUSTOM_MAX)
    {
        if (index >= payload_len || bytes[index] != standard->uid[1])
        {
            return false;
        }
        index++;
    }

    request->addressed = (request->flags & NFCV_REQ_ADDRESS) != 0u;
    request->selected = (request->flags & NFCV_REQ_SELECT) != 0u;
    if (request->addressed)
    {
        if (payload_len < index + NFCV_UID_SIZE ||
            !nfcv_wire_uid_matches(&bytes[index], standard->uid))
        {
            return false;
        }
        index = (uint16_t)(index + NFCV_UID_SIZE);
    }
    else if (request->selected && standard->state != NFCV_STANDARD_SELECTED)
    {
        return false;
    }

    request->params = &bytes[index];
    request->params_size_bytes = (uint8_t)(payload_len - index);
    return true;
}

static bool inventory_matches(const nfcv_standard_t *standard,
                              const nfcv_request_t *request)
{
    if (request->params_size_bytes < 1u)
    {
        return false;
    }
    uint8_t wire_uid[NFCV_UID_SIZE];
    nfcv_uid_to_wire(wire_uid, standard->uid);
    if ((request->flags & NFCV_REQ_SLOT1) == 0u && (wire_uid[0] & 0x0fu) != 0u)
    {
        return false;
    }
    return nfcv_inventory_mask_matches(
        wire_uid,
        request->params[0],
        &request->params[1],
        (uint8_t)(request->params_size_bytes - 1u));
}

static void reply_inventory(const nfcv_standard_t *standard,
                            nfcv_tag_response_t *response)
{
    response->bytes[0] = NFCV_RES_OK;
    response->bytes[1] = standard->dsfid;
    nfcv_uid_to_wire(&response->bytes[NFCV_INVENTORY_HEADER_BYTES],
                     standard->uid);
    response->size_bytes = NFCV_INVENTORY_HEADER_BYTES + NFCV_UID_SIZE;
}

static void reply_system_info(const nfcv_standard_t *standard,
                              nfcv_tag_response_t *response)
{
    response->bytes[0] = NFCV_RES_OK;
    response->bytes[NFCV_SYSINFO_FLAGS_BYTE] = NFCV_SYSINFO_FIELDS_ALL;
    nfcv_uid_to_wire(&response->bytes[NFCV_SYSINFO_UID_BYTE], standard->uid);
    response->bytes[NFCV_SYSINFO_DSFID_BYTE] = standard->dsfid;
    response->bytes[NFCV_SYSINFO_AFI_BYTE] = standard->afi;
    response->bytes[NFCV_SYSINFO_BLOCK_COUNT_BYTE] =
        (uint8_t)(standard->memory->block_count - 1u);
    response->bytes[NFCV_SYSINFO_BLOCK_SIZE_BYTE] =
        (uint8_t)(standard->memory->block_size_bytes - 1u);
    response->bytes[NFCV_SYSINFO_IC_REF_BYTE] = standard->ic_ref;
    response->size_bytes = NFCV_SYSINFO_PAYLOAD_BYTES;
}

static void write_parameter(bool locked,
                            uint8_t *value,
                            const nfcv_request_t *request,
                            nfcv_tag_response_t *response)
{
    if (locked)
    {
        nfcv_reply_error(response, NFCV_ERR_BLOCK_LOCKED);
        return;
    }
    if (request->params_size_bytes != 1u)
    {
        return;
    }
    *value = request->params[0];
    nfcv_reply_ok(response);
}

static nfcv_standard_result_t read_memory(nfcv_standard_t *standard,
                                          const nfcv_request_t *request,
                                          nfcv_tag_response_t *response,
                                          bool multiple)
{
    if (request->params_size_bytes != (multiple ? 2u : 1u))
    {
        return NFCV_STANDARD_NO_RESPONSE;
    }
    const uint8_t count = multiple ? (uint8_t)(request->params[1] + 1u) : 1u;
    nfcv_memory_store_read(
        standard->memory, response, request->option, request->params[0], count);
    return NFCV_STANDARD_RESPONSE;
}

nfcv_standard_result_t nfcv_standard_handle(nfcv_standard_t *standard,
                                            const nfcv_request_t *request,
                                            nfcv_tag_response_t *response)
{
    if (standard->state == NFCV_STANDARD_QUIET)
    {
        if (request->command == NFCV_CMD_RESET_TO_READY && request->addressed)
        {
            nfcv_standard_reset_session(standard);
            nfcv_reply_ok(response);
            return NFCV_STANDARD_RESPONSE;
        }
        return NFCV_STANDARD_NO_RESPONSE;
    }

    switch (request->command)
    {
    case NFCV_CMD_INVENTORY:
        if (!request->inventory || !inventory_matches(standard, request))
        {
            return NFCV_STANDARD_NO_RESPONSE;
        }
        reply_inventory(standard, response);
        return NFCV_STANDARD_RESPONSE;

    case NFCV_CMD_STAY_QUIET:
        if (request->addressed)
        {
            nfcv_standard_quiet(standard);
        }
        return NFCV_STANDARD_NO_RESPONSE;
    case NFCV_CMD_SELECT:
        if (!request->addressed || request->selected)
        {
            return NFCV_STANDARD_NO_RESPONSE;
        }
        standard->state = NFCV_STANDARD_SELECTED;
        nfcv_reply_ok(response);
        return NFCV_STANDARD_RESPONSE;

    case NFCV_CMD_RESET_TO_READY:
        nfcv_standard_reset_session(standard);
        nfcv_reply_ok(response);
        return NFCV_STANDARD_RESPONSE;
    case NFCV_CMD_GET_SYSTEM_INFO:
        reply_system_info(standard, response);
        return NFCV_STANDARD_RESPONSE;
    case NFCV_CMD_READ_SINGLE:
        return read_memory(standard, request, response, false);
    case NFCV_CMD_READ_MULTIPLE:
        return read_memory(standard, request, response, true);

    case NFCV_CMD_WRITE_SINGLE:
        nfcv_memory_store_write(standard->memory,
                                response,
                                request->params,
                                request->params_size_bytes);
        break;
    case NFCV_CMD_LOCK_BLOCK:
        nfcv_memory_store_lock(standard->memory,
                               response,
                               request->params,
                               request->params_size_bytes);
        break;
    case NFCV_CMD_GET_BLOCK_SECURITY:
        nfcv_memory_store_security(standard->memory,
                                   response,
                                   request->params,
                                   request->params_size_bytes);
        break;
    case NFCV_CMD_WRITE_AFI:
        write_parameter(
            standard->afi_locked, &standard->afi, request, response);
        break;
    case NFCV_CMD_LOCK_AFI:
        standard->afi_locked = true;
        nfcv_reply_ok(response);
        break;
    case NFCV_CMD_WRITE_DSFID:
        write_parameter(
            standard->dsfid_locked, &standard->dsfid, request, response);
        break;
    case NFCV_CMD_LOCK_DSFID:
        standard->dsfid_locked = true;
        nfcv_reply_ok(response);
        break;
    default:
        if (request->command >= NFCV_CMD_CUSTOM_MIN &&
            request->command <= NFCV_CMD_CUSTOM_MAX)
        {
            return NFCV_STANDARD_UNHANDLED;
        }
        if (request->addressed)
        {
            nfcv_reply_error(response, NFCV_ERR_CMD_NOT_SUPPORTED);
            return NFCV_STANDARD_RESPONSE;
        }
        return NFCV_STANDARD_NO_RESPONSE;
    }
    return response->size_bytes != 0u ? NFCV_STANDARD_RESPONSE
                                      : NFCV_STANDARD_NO_RESPONSE;
}
