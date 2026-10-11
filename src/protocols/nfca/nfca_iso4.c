#include "cards/desfire/desfire.h"
#include "cards/image_copy.h"
#include "cards/image_internal.h"
#include "cards/iso4/iso4_image.h"
#include "common/compiler.h"
#include "emulation/snapshot.h"
#include "pn532_unleashed/nfca_activation.h"
#include "protocols/nfca/nfca_profile.h"

#define ISO4_PCB_BYTES 1u
#define ISO4_CID_HEADER_BYTES 2u
#define ISO4_STATUS_BYTES 2u
#define ISO4_MIN_FRAME_BYTES (ISO4_PCB_BYTES + NFC_CRC_BYTES)
#define ISO4_MIN_CID_FRAME_BYTES (ISO4_CID_HEADER_BYTES + NFC_CRC_BYTES)
#define ISO4_ACTIVATION_FRAME_BYTES (2u + NFC_CRC_BYTES)
#define ISO4_PPS_COMMAND_MASK 0xf0u
#define ISO4_PPS_CURRENT_RATE 0x01u
#define ISO7816_CLA_STANDARD 0x00u
#define ISO7816_INS_READ_BINARY 0xb0u
#define ISO7816_INS_VERIFY 0x20u
#define ISO7816_SW_OK_HI 0x90u
#define ISO7816_SW_OK_LO 0x00u
#define ISO7816_SW_VERIFY_FAIL_HI 0x63u
#define ST25TA_CLA 0xa2u
#define ST25TA_DEMO_NDEF_BYTES 0x1du
#define ST25TA_VERIFY_MIN_FRAME_BYTES 6u
#define ST25TA_VERIFY_FULL_FRAME_BYTES 22u
/* Block/reply mapping for the limited ISO4 profiles. */
#define ISO4_PCB_I_CID_0 0x0au
#define ISO4_PCB_I_CID_1 0x0bu
#define ISO4_PCB_I_CHAIN_CID_0 0x1au
#define ISO4_PCB_I_CHAIN_CID_1 0x1bu
#define ISO4_PCB_R_ACK_CID_0 0xaau
#define ISO4_PCB_R_ACK_CID_1 0xabu
#define ISO4_PCB_R_NAK_CID_0 0xbau
#define ISO4_PCB_R_NAK_CID_1 0xbbu
#define ISO4_PCB_S_DESELECT 0xc2u
#define ISO4_PCB_S_DESELECT_CID 0xcau
#define ISO4_PCB_BLOCK_NUMBER_MASK 0x01u
#define ISO4_PROFILE_R_REPLY_TOGGLE 0x11u

static struct
{
    nfca_activation_t activation;
    card_model_t model;
    uint8_t ats[ISO4_MAX_ATS_BYTES];
    uint8_t ats_len;
    bool rats_seen;
    desfire_context_t desfire;
    uint8_t capture_bytes[CARD_STORAGE_MAX_BYTES];
} context;

void nfca_iso4_get_image(card_image_t *image)
{
    *image =
        (card_image_t){.model = context.model,
                       .uid = context.activation.uid,
                       .uid_size_bytes = context.activation.uid_size_bytes};
    iso4_state_t *state = &image->state.iso4;
    memcpy(state->atqa, context.activation.atqa, sizeof(state->atqa));
    state->sak = context.activation.sak;
    state->ats_size_bytes = context.ats_len;
    memcpy(state->ats, context.ats, context.ats_len);
    if (context.model == CARD_DESFIRE)
    {
        state->capture = context.desfire.capture;
    }
}

static void init(void)
{
    memset(&context, 0, sizeof(context));
}

bool nfca_iso4_load_image(const card_image_t *image)
{
    if (image == NULL || !card_model_is_iso4(image->model) ||
        image_validate(image) != IMAGE_OK)
    {
        return false;
    }
    init();
    const iso4_state_t *state = &image->state.iso4;
    const uint8_t *atqa = state->atqa;
    const uint8_t sak = state->sak;
    context.ats_len = state->ats_size_bytes;
    memcpy(context.ats, state->ats, context.ats_len);
    if (image->model == CARD_DESFIRE)
    {
        /* One foreground RF owner; static scratch avoids a large stack frame.
         */
        static card_image_t owned;
        if (!card_image_copy(image,
                             &owned,
                             context.capture_bytes,
                             sizeof(context.capture_bytes)))
        {
            return false;
        }
        context.desfire.capture = owned.state.iso4.capture;
        memset(&owned, 0, sizeof(owned));
        desfire_reset_session(&context.desfire);
    }
    if (!nfca_activation_init(&context.activation,
                              image->uid,
                              (uint8_t)image->uid_size_bytes,
                              atqa,
                              sak))
    {
        return false;
    }
    context.model = image->model;
    return true;
}

static bool RAM_FUNC(reply_status)(nfca_tag_response_t *response,
                                   uint8_t pcb,
                                   uint8_t sw1,
                                   uint8_t sw2)
{
    response->frame.bytes[0] = pcb;
    response->frame.bytes[1] = sw1;
    response->frame.bytes[2] = sw2;
    nfca_tag_response_data_crc(response, ISO4_PCB_BYTES + ISO4_STATUS_BYTES);
    return true;
}

static bool RAM_FUNC(handle_st25ta)(const nfca_tag_request_t *request,
                                    nfca_tag_response_t *response)
{
    const uint8_t *command = request->bytes;
    /* This local reply is a fixed demonstration NDEF,
     * not a dump of ST25TA memory or a credential verification operation. */
    static const uint8_t read_ndef[] = {
        ST25TA_CLA, ISO7816_INS_READ_BINARY, 0u, 0u, ST25TA_DEMO_NDEF_BYTES};
    if (request->size_bytes ==
            ISO4_PCB_BYTES + sizeof(read_ndef) + NFC_CRC_BYTES &&
        memcmp(&command[ISO4_PCB_BYTES], read_ndef, sizeof(read_ndef)) == 0)
    {
        static const uint8_t ndef_reply[] = {
            0x00u, 0x1bu, 0xd1u, 0x01u, 0x17u, 0x54u, 0x02u, 0x7au,
            0x68u, 0xa2u, 0x34u, 0xcbu, 0xd0u, 0xe2u, 0x03u, 0xc7u,
            0x3eu, 0x62u, 0x0bu, 0xe8u, 0xc6u, 0x3cu, 0x85u, 0x2cu,
            0xc5u, 0x31u, 0x31u, 0x31u, 0x32u, 0x90u, 0x00u,
        };
        response->frame.bytes[0] = command[0];
        memcpy(&response->frame.bytes[1], ndef_reply, sizeof(ndef_reply));
        nfca_tag_response_data_crc(response, 1u + sizeof(ndef_reply));
        return true;
    }
    if (request->size_bytes >= ST25TA_VERIFY_MIN_FRAME_BYTES &&
        command[1] == ISO7816_CLA_STANDARD && command[2] == ISO7816_INS_VERIFY)
    {
        return reply_status(response,
                            command[0],
                            request->size_bytes >=
                                    ST25TA_VERIFY_FULL_FRAME_BYTES
                                ? ISO7816_SW_OK_HI
                                : ISO7816_SW_VERIFY_FAIL_HI,
                            ISO7816_SW_OK_LO);
    }
    return reply_status(
        response, command[0], ISO7816_SW_OK_HI, ISO7816_SW_OK_LO);
}

static bool RAM_FUNC(handle_transport)(const nfca_tag_request_t *request,
                                       nfca_tag_response_t *response)
{
    const uint8_t pcb = request->bytes[0];
    switch (pcb)
    {
    case ISODEP_PCB_I_BLOCK_0:
    case ISODEP_PCB_I_BLOCK_1:
        return reply_status(response, pcb, ISO7816_SW_OK_HI, ISO7816_SW_OK_LO);
    case ISO4_PCB_I_CID_0:
    case ISO4_PCB_I_CID_1:
        if (request->size_bytes < ISO4_MIN_CID_FRAME_BYTES)
        {
            return false;
        }
        response->frame.bytes[0] = pcb;
        response->frame.bytes[1] = request->bytes[1];
        response->frame.bytes[2] = ISO7816_SW_OK_HI;
        response->frame.bytes[3] = ISO7816_SW_OK_LO;
        nfca_tag_response_data_crc(response,
                                   ISO4_CID_HEADER_BYTES + ISO4_STATUS_BYTES);
        return true;
    case ISO4_PCB_I_CHAIN_CID_0:
    case ISO4_PCB_I_CHAIN_CID_1:
        if (request->size_bytes < ISO4_MIN_CID_FRAME_BYTES)
        {
            return false;
        }
        response->frame.bytes[0] =
            (uint8_t)(ISO4_PCB_R_ACK_CID_0 |
                      (pcb & ISO4_PCB_BLOCK_NUMBER_MASK));
        response->frame.bytes[1] = request->bytes[1];
        nfca_tag_response_data_crc(response, ISO4_CID_HEADER_BYTES);
        return true;
    case ISO4_PCB_R_ACK_CID_0:
    case ISO4_PCB_R_NAK_CID_1:
        nfca_reply_byte_crc(response,
                            (uint8_t)(pcb ^ ISO4_PROFILE_R_REPLY_TOGGLE));
        return true;
    case ISO4_PCB_R_NAK_CID_0:
        response->frame.bytes[0] = ISO4_PCB_R_ACK_CID_1;
        response->frame.bytes[1] = 0u;
        nfca_tag_response_data_crc(response, ISO4_CID_HEADER_BYTES);
        return true;
    case ISO4_PCB_S_DESELECT_CID:
    case ISO4_PCB_S_DESELECT:
        response->frame.bytes[0] = ISO4_PCB_S_DESELECT_CID;
        response->frame.bytes[1] = 0u;
        nfca_tag_response_data_crc(response, ISO4_CID_HEADER_BYTES);
        context.rats_seen = false;
        return true;
    default:
        return false;
    }
}

static bool RAM_FUNC(handle_frame)(const nfca_tag_request_t *request,
                                   nfca_tag_response_t *response)
{
    nfca_reply_silent(response);
    if (request->size_bytes > NFCA_FRAME_MAX_BYTES)
    {
        return false;
    }
    nfca_activation_event_t event;
    const nfca_activation_result_t result = nfca_activation_handle(
        &context.activation, request, &response->frame, &event);
    if (event != NFCA_ACTIVATION_EVENT_NONE)
    {
        context.rats_seen = false;
        if (context.model == CARD_DESFIRE)
        {
            desfire_reset_session(&context.desfire);
        }
    }
    if (result != NFCA_ACTIVATION_UNHANDLED)
    {
        return result == NFCA_ACTIVATION_RESPONSE;
    }
    if (!nfca_activation_is_active(&context.activation) ||
        request->short_frame || !request->parity_ok || !request->crc_ok)
    {
        return false;
    }
    if (match_cmd(request, NFCA_CMD_RATS, ISO4_ACTIVATION_FRAME_BYTES))
    {
        if (context.model == CARD_DESFIRE)
        {
            static const uint16_t sizes[] = {
                16, 24, 32, 40, 48, 64, 96, 128, 256};
            const uint8_t fsdi = request->bytes[1] >> 4;
            if (fsdi > 8u || (request->bytes[1] & 0x0fu) != 0u)
            {
                return false;
            }
            desfire_reset_session(&context.desfire);
            /* Reserve PCB, wrapped status and CRC even for native replies. */
            const uint16_t capacity = sizes[fsdi] - 5u;
            context.desfire.response_data_bytes =
                capacity < 58u ? (uint8_t)capacity : 58u;
        }
        context.rats_seen = true;
        memcpy(response->frame.bytes, context.ats, context.ats_len);
        nfca_tag_response_data_crc(response, context.ats_len);
        return true;
    }
    /* UID-only files preserve activation but imply no APDU implementation. */
    if (context.model == CARD_ISO_DEP || !context.rats_seen ||
        request->size_bytes < ISO4_MIN_FRAME_BYTES)
    {
        return false;
    }
    if ((request->bytes[0] & ISO4_PPS_COMMAND_MASK) == NFCA_CMD_PPS)
    {
        /* Accept only the current 106 kbit/s rate; PHY rates do not change. */
        if (request->size_bytes == ISO4_ACTIVATION_FRAME_BYTES &&
            request->bytes[1] == ISO4_PPS_CURRENT_RATE)
        {
            nfca_reply_byte_crc(response, request->bytes[0]);
            return true;
        }
        return false;
    }
    if (context.model == CARD_DESFIRE)
    {
        return desfire_handle(&context.desfire, request, response);
    }
    if (context.model == CARD_ST25TA)
    {
        return handle_st25ta(request, response);
    }
    return handle_transport(request, response);
}

const nfca_tag_profile_t nfca_iso4_profile = {
    .handle_frame = handle_frame,
};
