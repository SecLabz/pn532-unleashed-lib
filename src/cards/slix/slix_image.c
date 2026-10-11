#include "cards/image_internal.h"
#include "cards/slix/slix_tag.h"
#include "common/endian.h"
#include "emulation/snapshot.h"
#include "protocols/nfcv/nfcv_profile.h"

#include <string.h>

#ifdef PICO_ON_DEVICE
#include "pico.h"
#else
#define __not_in_flash_func(name) name
#endif

static nfcv_memory_store_t memory;
static nfcv_standard_t standard;
static slix_tag_t slix;

bool nfcv_get_image(card_image_t *image)
{
    if (slix.destroyed)
    {
        return false;
    }
    *image = (card_image_t){.model = slix.slix_l    ? CARD_SLIX_L
                                     : slix.nxp_eas ? CARD_SLIX
                                                    : CARD_ISO15693,
                            .uid = standard.uid,
                            .uid_size_bytes = sizeof(standard.uid),
                            .memory = memory.memory,
                            .memory_size_bytes =
                                memory.block_count * memory.block_size_bytes};
    nfcv_state_t *state = &image->state.nfcv;
    *state =
        (nfcv_state_t){.locks = memory.locks,
                       .block_count = memory.block_count,
                       .dsfid = standard.dsfid,
                       .afi = standard.afi,
                       .ic_ref = standard.ic_ref,
                       .dsfid_locked = standard.dsfid_locked,
                       .afi_locked = standard.afi_locked,
                       .eas_active = slix.eas_active,
                       .eas_locked = slix.eas_locked,
                       .privacy_active = slix.privacy_mode,
                       .eas_password_protected = slix.eas_password_protected,
                       .afi_password_protected = slix.afi_password_protected};
    if (slix.nxp_eas)
    {
        write_le32(state->eas_afi_password,
                   slix.passwords[SLIX_PASSWORD_TYPE_EAS_AFI]);
    }
    if (slix.slix_l)
    {
        write_le32(state->privacy_password,
                   slix.passwords[SLIX_PASSWORD_TYPE_PRIVACY]);
        write_le32(state->destroy_password,
                   slix.passwords[SLIX_PASSWORD_TYPE_DESTROY]);
    }
    return true;
}
static void init_slix(void)
{
    memset(&memory, 0, sizeof(memory));
    memset(&standard, 0, sizeof(standard));
    memset(&slix, 0, sizeof(slix));
}
bool nfcv_load_image(const card_image_t *image)
{
    if (image == NULL ||
        (image->model != CARD_SLIX && image->model != CARD_SLIX_L &&
         image->model != CARD_ISO15693) ||
        image_validate(image) != IMAGE_OK)
    {
        return false;
    }
    init_slix();
    const bool generic = image->model == CARD_ISO15693;
    const nfcv_state_t *state = &image->state.nfcv;
    if (!nfcv_memory_store_init(&memory,
                                NFCV_BLOCK_BYTES,
                                state->block_count,
                                true,
                                image->memory,
                                image->memory_size_bytes,
                                state->locks))
    {
        return false;
    }
    if (!nfcv_standard_init(&standard,
                            &memory,
                            image->uid,
                            state->dsfid,
                            state->afi,
                            state->ic_ref,
                            state->dsfid_locked,
                            state->afi_locked))
    {
        return false;
    }
    uint32_t passwords[SLIX_PASSWORD_TYPE_COUNT] = {0};
    passwords[SLIX_PASSWORD_TYPE_PRIVACY] = read_le32(state->privacy_password);
    passwords[SLIX_PASSWORD_TYPE_DESTROY] = read_le32(state->destroy_password);
    passwords[SLIX_PASSWORD_TYPE_EAS_AFI] = read_le32(state->eas_afi_password);
    slix_tag_init(&slix,
                  !generic,
                  image->model == CARD_SLIX_L,
                  state->eas_locked,
                  state->privacy_active,
                  passwords);
    slix.eas_active = state->eas_active;
    slix.eas_password_protected = state->eas_password_protected;
    slix.afi_password_protected = state->afi_password_protected;
    return true;
}

static bool __not_in_flash_func(handle_frame)(const nfcv_frame_t *frame,
                                              nfcv_tag_response_t *response)
{
    nfcv_request_t request;
    nfcv_reply_silent(response);
    if (!nfcv_standard_prepare(&standard, frame, &request) ||
        !slix_tag_allows_request(&slix, &request))
    {
        return false;
    }
    const nfcv_standard_result_t result =
        nfcv_standard_handle(&standard, &request, response);
    if (result != NFCV_STANDARD_UNHANDLED)
    {
        return result == NFCV_STANDARD_RESPONSE;
    }
    slix_tag_handle(&slix, &request, response);
    return response->size_bytes != 0u;
}

const nfcv_tag_profile_t nfcv_vicinity_profile = {
    .handle_frame = handle_frame,
};
