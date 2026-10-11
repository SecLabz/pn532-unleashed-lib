#include "cards/type2/ntag_internal.h"
#include "common/endian.h"
#include "emulation/snapshot.h"

#include <string.h>

ntag_context_t ntag;

void nfca_ntag_get_image(card_image_t *image)
{
    *image = (card_image_t){.model = ntag.config.model,
                            .uid = ntag.activation.uid,
                            .uid_size_bytes = ntag.activation.uid_size_bytes,
                            .memory = &ntag.memory[0][0],
                            .memory_size_bytes =
                                ntag.config.page_count * NTAG_PAGE_BYTES};
    if (!ntag.config.ev1)
    {
        return;
    }
    type2_state_t *state = &image->state.type2;
    memcpy(state->version, ntag.config.version, sizeof(state->version));
    memcpy(state->signature, ntag.signature, sizeof(state->signature));
    memcpy(state->counters, ntag.counters, sizeof(state->counters));
    memcpy(state->tearing, ntag.tearing, sizeof(state->tearing));
    state->authentication_attempts = ntag.auth_attempts;
}

static void ntag_init(void)
{
    memset(&ntag, 0, sizeof(ntag));
}

bool nfca_ntag_load_image(const card_image_t *image)
{
    if (image_validate(image) != IMAGE_OK)
    {
        return false;
    }
    const ntag_config_t *config = ntag_model_config(image->model);
    if (config == NULL)
    {
        return false;
    }
    ntag_init();
    ntag.config = *config;
    const uint8_t atqa[] = {0x44u, 0u};
    nfca_activation_init(
        &ntag.activation, image->uid, NTAG_UID_BYTES, atqa, 0u);
    /* Keep the raw manufacturer pages intact, including Amiibo crypto input.
     * Anticollision identity is stored separately from the memory image. */
    memcpy(ntag.memory, image->memory, image->memory_size_bytes);
    if (!ntag.config.ev1)
    {
        return true;
    }
    /* Effective CFGLCK is fixed for this loaded image, not on selection. */
    ntag.config_locked =
        (ntag.memory[ntag.config.config_page + NTAG_CONFIG_ACCESS_PAGE]
                    [NTAG_CONFIG_ACCESS_BYTE] &
         NTAG_ACCESS_CFGLCK) != 0u;
    const type2_state_t *state = &image->state.type2;
    memcpy(ntag.signature, state->signature, sizeof(ntag.signature));
    memcpy(ntag.counters, state->counters, sizeof(ntag.counters));
    memcpy(ntag.tearing, state->tearing, sizeof(ntag.tearing));
    ntag.auth_attempts = state->authentication_attempts;
    return true;
}
