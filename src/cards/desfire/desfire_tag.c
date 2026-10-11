#include "cards/desfire/desfire_internal.h"
#include "protocols/nfca/nfca_profile.h"

static bool RAM_FUNC(parse_desfire_request)(const nfca_tag_request_t *frame,
                                            desfire_request_t *req)
{
    if (frame == NULL || req == NULL ||
        frame->size_bytes < ISODEP_MIN_NATIVE_FRAME_LEN)
    {
        return false;
    }

    const uint8_t pcb = frame->bytes[0];
    if (pcb != ISODEP_PCB_I_BLOCK_0 && pcb != ISODEP_PCB_I_BLOCK_1)
    {
        return false;
    }

    req->pcb = pcb;
    req->wrapped =
        (frame->bytes[DESFIRE_NATIVE_COMMAND_BYTE] == ISO7816_CLA_DESFIRE);
    req->params = NULL;
    req->params_len = 0u;

    if (req->wrapped)
    {
        if (frame->size_bytes < ISODEP_MIN_WRAPPED_FRAME_LEN ||
            frame->bytes[DESFIRE_WRAPPED_P1_BYTE] != 0u ||
            frame->bytes[DESFIRE_WRAPPED_P2_BYTE] != 0u ||
            frame->bytes[frame->size_bytes - NFC_CRC_BYTES - 1u] != 0u)
        {
            return false;
        }
        req->command = frame->bytes[DESFIRE_WRAPPED_COMMAND_BYTE];
        if (frame->size_bytes > ISODEP_MIN_WRAPPED_FRAME_LEN)
        {
            const uint8_t lc = frame->bytes[DESFIRE_WRAPPED_LC_BYTE];
            if ((uint16_t)lc + ISODEP_WRAPPED_DATA_OVERHEAD_BYTES !=
                frame->size_bytes)
            {
                return false;
            }
            req->params = &frame->bytes[DESFIRE_WRAPPED_PARAMS_BYTE];
            req->params_len = lc;
        }
    }
    else
    {
        req->command = frame->bytes[DESFIRE_NATIVE_COMMAND_BYTE];
        if (frame->size_bytes > ISODEP_MIN_NATIVE_FRAME_LEN)
        {
            req->params = &frame->bytes[DESFIRE_NATIVE_PARAMS_BYTE];
            req->params_len =
                (uint8_t)(frame->size_bytes - ISODEP_MIN_NATIVE_FRAME_LEN);
        }
    }
    return true;
}

bool RAM_FUNC(desfire_handle)(desfire_context_t *context,
                              const nfca_tag_request_t *frame,
                              nfca_tag_response_t *response)
{
    desfire_request_t request;
    if (!frame->crc_ok || !frame->parity_ok || frame->short_frame ||
        !parse_desfire_request(frame, &request))
    {
        return false;
    }
    /* Public discovery/read continuations carry no data. Reject unsupported
     * envelopes before consuming either a file fragment or GetVersion part. */
    if (request.command == DESFIRE_COMMAND_ADDITIONAL_FRAME &&
        request.params_len != 0u)
    {
        return false;
    }
    return desfire_handle_capture(context, &request, response);
}
