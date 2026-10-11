#include "pn532_unleashed/nfca_activation.h"

#include "common/compiler.h"
#include "pn532_unleashed/integrity.h"

#include <string.h>

#define NFCA_HALT_REQA_IGNORE_US 100000u

static inline void reply_silent(nfca_response_t *response)
{
    response->type = NFCA_TAG_RESPONSE_NONE;
    response->size_bytes = 0u;
    response->short4 = 0u;
}

static inline void response_data(nfca_response_t *response, uint16_t len)
{
    response->type = NFCA_TAG_RESPONSE_DATA;
    response->size_bytes = len;
    response->short4 = 0u;
}

static inline void reply_byte_crc(nfca_response_t *response, uint8_t byte)
{
    response->bytes[0] = byte;
    nfca_append_crc(response->bytes, 1u);
    response_data(response, NFCA_SAK_FRAME_BYTES);
}

bool nfca_activation_init(nfca_activation_t *activation,
                          const uint8_t *uid,
                          uint8_t uid_size_bytes,
                          const uint8_t atqa[NFCA_ATQA_BYTES],
                          uint8_t sak)
{
    if (activation == NULL || uid == NULL || atqa == NULL ||
        (uid_size_bytes != NFCA_UID_SINGLE_BYTES &&
         uid_size_bytes != NFCA_UID_DOUBLE_BYTES &&
         uid_size_bytes != NFCA_UID_TRIPLE_BYTES))
    {
        return false;
    }
    memset(activation, 0, sizeof(*activation));
    activation->uid_size_bytes = uid_size_bytes;
    memcpy(activation->uid, uid, uid_size_bytes);
    memcpy(activation->atqa, atqa, sizeof(activation->atqa));
    activation->sak = sak;
    return true;
}

bool nfca_activation_set_uid(nfca_activation_t *activation,
                             const uint8_t *uid,
                             uint8_t uid_size_bytes)
{
    if (activation == NULL || uid == NULL ||
        (uid_size_bytes != NFCA_UID_SINGLE_BYTES &&
         uid_size_bytes != NFCA_UID_DOUBLE_BYTES &&
         uid_size_bytes != NFCA_UID_TRIPLE_BYTES))
    {
        return false;
    }
    memset(activation->uid, 0, sizeof(activation->uid));
    memcpy(activation->uid, uid, uid_size_bytes);
    activation->uid_size_bytes = uid_size_bytes;
    nfca_activation_reset(activation);
    return true;
}

void RAM_FUNC(nfca_activation_reset)(nfca_activation_t *activation)
{
    activation->state = NFCA_ACTIVATION_IDLE;
    activation->selected_cascade = 0u;
    activation->halt_started_us = 0u;
}

void RAM_FUNC(nfca_activation_halt)(nfca_activation_t *activation,
                                    uint32_t timestamp_us)
{
    activation->state = NFCA_ACTIVATION_HALTED;
    activation->selected_cascade = 0u;
    activation->halt_started_us = timestamp_us;
}

static void RAM_FUNC(reply_atqa)(const nfca_activation_t *activation,
                                 nfca_response_t *response)
{
    memcpy(response->bytes, activation->atqa, sizeof(activation->atqa));
    response_data(response, sizeof(activation->atqa));
}

static void RAM_FUNC(cascade_uid)(const nfca_activation_t *activation,
                                  uint8_t cascade,
                                  uint8_t bytes[NFCA_ANTICOLLISION_BYTES])
{
    uint8_t level;
    if (cascade == NFCA_CMD_ANTICOLL_CL1)
    {
        level = 0u;
    }
    else
    {
        level = cascade == NFCA_CMD_ANTICOLL_CL2 ? 1u : 2u;
    }
    const uint8_t offset = level * NFCA_CASCADE_UID_BYTES;
    if (activation->uid_size_bytes > offset + NFCA_UID_FRAGMENT_BYTES)
    {
        bytes[0] = NFCA_CASCADE_TAG;
        memcpy(&bytes[1], &activation->uid[offset], NFCA_CASCADE_UID_BYTES);
    }
    else
    {
        memcpy(bytes, &activation->uid[offset], NFCA_UID_FRAGMENT_BYTES);
    }
    bytes[NFCA_UID_FRAGMENT_BYTES] =
        nfca_bcc(bytes[0], bytes[1], bytes[2], bytes[3]);
}

static void RAM_FUNC(reply_anticoll)(const nfca_activation_t *activation,
                                     uint8_t cascade,
                                     nfca_response_t *response)
{
    cascade_uid(activation, cascade, response->bytes);
    response_data(response, NFCA_ANTICOLLISION_BYTES);
}

static inline bool match_short(const nfca_tag_request_t *frame, uint8_t command)
{
    return frame->short_frame && frame->size_bytes == 1u &&
           frame->bytes[0] == command;
}

static inline bool is_reqa(const nfca_tag_request_t *frame)
{
    return match_short(frame, NFCA_CMD_REQA);
}

static inline bool is_wupa(const nfca_tag_request_t *frame)
{
    return match_short(frame, NFCA_CMD_WUPA);
}

static inline bool is_anticoll(const nfca_tag_request_t *frame, uint8_t cascade)
{
    return !frame->short_frame &&
           frame->size_bytes == NFCA_COMMAND_HEADER_BYTES && frame->parity_ok &&
           frame->bytes[0] == cascade &&
           frame->bytes[1] == NFCA_ANTICOLLISION_NVB;
}

static inline bool is_halt(const nfca_tag_request_t *frame)
{
    return !frame->short_frame && frame->size_bytes == NFCA_HALT_FRAME_BYTES &&
           frame->parity_ok && frame->crc_ok &&
           frame->bytes[0] == NFCA_CMD_HALT && frame->bytes[1] == 0u;
}

static bool RAM_FUNC(select_matches)(const nfca_activation_t *activation,
                                     const nfca_tag_request_t *request,
                                     uint8_t cascade)
{
    if (request->short_frame || request->size_bytes != NFCA_SELECT_BYTES ||
        !request->parity_ok || !request->crc_ok ||
        request->bytes[0] != cascade || request->bytes[1] != NFCA_SELECT_NVB)
    {
        return false;
    }
    uint8_t bytes[NFCA_ANTICOLLISION_BYTES];
    cascade_uid(activation, cascade, bytes);
    return memcmp(&request->bytes[NFCA_COMMAND_HEADER_BYTES],
                  bytes,
                  NFCA_ANTICOLLISION_BYTES) == 0;
}

nfca_activation_result_t
RAM_FUNC(nfca_activation_handle)(nfca_activation_t *activation,
                                 const nfca_tag_request_t *request,
                                 nfca_response_t *response,
                                 nfca_activation_event_t *event)
{
    *event = NFCA_ACTIVATION_EVENT_NONE;
    if (is_reqa(request) || is_wupa(request))
    {
        if (is_reqa(request) && activation->state == NFCA_ACTIVATION_HALTED &&
            (uint32_t)(request->timestamp_us - activation->halt_started_us) <
                NFCA_HALT_REQA_IGNORE_US)
        {
            reply_silent(response);
            return NFCA_ACTIVATION_SILENT;
        }
        activation->state = NFCA_ACTIVATION_READY;
        activation->selected_cascade = 0u;
        *event = NFCA_ACTIVATION_EVENT_READY;
        reply_atqa(activation, response);
        return NFCA_ACTIVATION_RESPONSE;
    }
    if (is_halt(request))
    {
        nfca_activation_halt(activation, request->timestamp_us);
        *event = NFCA_ACTIVATION_EVENT_HALTED;
        reply_silent(response);
        return NFCA_ACTIVATION_SILENT;
    }
    if (is_anticoll(request, NFCA_CMD_ANTICOLL_CL1))
    {
        reply_anticoll(activation, NFCA_CMD_ANTICOLL_CL1, response);
        return NFCA_ACTIVATION_RESPONSE;
    }
    if (select_matches(activation, request, NFCA_CMD_ANTICOLL_CL1))
    {
        if (activation->uid_size_bytes > NFCA_UID_SINGLE_BYTES)
        {
            activation->selected_cascade = 1u;
            reply_byte_crc(response, NFCA_SAK_CASCADE);
        }
        else
        {
            activation->state = NFCA_ACTIVATION_ACTIVE;
            *event = NFCA_ACTIVATION_EVENT_ACTIVE;
            reply_byte_crc(response, activation->sak);
        }
        return NFCA_ACTIVATION_RESPONSE;
    }
    if (activation->uid_size_bytes > NFCA_UID_SINGLE_BYTES &&
        activation->selected_cascade == 1u &&
        is_anticoll(request, NFCA_CMD_ANTICOLL_CL2))
    {
        reply_anticoll(activation, NFCA_CMD_ANTICOLL_CL2, response);
        return NFCA_ACTIVATION_RESPONSE;
    }
    if (activation->uid_size_bytes > NFCA_UID_SINGLE_BYTES &&
        activation->selected_cascade == 1u &&
        select_matches(activation, request, NFCA_CMD_ANTICOLL_CL2))
    {
        if (activation->uid_size_bytes == NFCA_UID_TRIPLE_BYTES)
        {
            activation->selected_cascade = 2u;
            reply_byte_crc(response, NFCA_SAK_CASCADE);
        }
        else
        {
            activation->state = NFCA_ACTIVATION_ACTIVE;
            *event = NFCA_ACTIVATION_EVENT_ACTIVE;
            reply_byte_crc(response, activation->sak);
        }
        return NFCA_ACTIVATION_RESPONSE;
    }
    if (activation->uid_size_bytes == NFCA_UID_TRIPLE_BYTES &&
        activation->selected_cascade == 2u &&
        is_anticoll(request, NFCA_CMD_ANTICOLL_CL3))
    {
        reply_anticoll(activation, NFCA_CMD_ANTICOLL_CL3, response);
        return NFCA_ACTIVATION_RESPONSE;
    }
    if (activation->uid_size_bytes == NFCA_UID_TRIPLE_BYTES &&
        activation->selected_cascade == 2u &&
        select_matches(activation, request, NFCA_CMD_ANTICOLL_CL3))
    {
        activation->state = NFCA_ACTIVATION_ACTIVE;
        *event = NFCA_ACTIVATION_EVENT_ACTIVE;
        reply_byte_crc(response, activation->sak);
        return NFCA_ACTIVATION_RESPONSE;
    }
    return NFCA_ACTIVATION_UNHANDLED;
}
