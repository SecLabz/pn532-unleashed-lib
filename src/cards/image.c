#include "pn532_unleashed/image.h"

#include "cards/desfire/desfire.h"
#include "cards/image_internal.h"
#include "cards/iso4/iso4_image.h"
#include "cards/mifare/mifare_product.h"
#include "pn532_unleashed/nfcv_frame.h"

#include <string.h>

static bool classic_valid(const card_image_t *image)
{
    const uint16_t block_count = image->memory_size_bytes / MIFARE_BLOCK_SIZE;
    for (uint16_t block = 0u; block < block_count; block++)
    {
        if (!mifare_is_sector_trailer((uint8_t)block))
        {
            continue;
        }
        const uint8_t *trailer = &image->memory[block * MIFARE_BLOCK_SIZE];
        if (!mifare_trailer_access_bits_valid(trailer))
        {
            return false;
        }
    }
    return true;
}

static bool zero_password(const uint8_t p[4])
{
    return (p[0] | p[1] | p[2] | p[3]) == 0u;
}
image_status_t image_validate(const card_image_t *image)
{
    if (image == NULL)
    {
        return IMAGE_INVALID_ARGUMENT;
    }
    if (image->uid == NULL ||
        (image->memory_size_bytes != 0u && image->memory == NULL))
    {
        return IMAGE_MISSING_STATE;
    }
    if (card_model_is_iso4(image->model))
    {
        if (!iso4_image_valid(image))
        {
            return IMAGE_INVALID_STATE;
        }
        return image->model != CARD_DESFIRE ||
                       desfire_capture_valid(&image->state.iso4.capture)
                   ? IMAGE_OK
                   : IMAGE_INVALID_CAPTURE;
    }
    const mifare_product_config_t *classic = mifare_model_config(image->model);
    const ntag_config_t *type2 = ntag_model_config(image->model);
    if (classic != NULL)
    {
        if (image->uid_size_bytes != NFCA_UID_SINGLE_BYTES)
        {
            return IMAGE_INVALID_UID_SIZE;
        }
        if (image->memory_size_bytes !=
            classic->block_count * MIFARE_BLOCK_SIZE)
        {
            return IMAGE_INVALID_MEMORY_SIZE;
        }
        return classic_valid(image) ? IMAGE_OK : IMAGE_INVALID_ACCESS_BITS;
    }
    if (type2 != NULL)
    {
        if (image->uid_size_bytes != NTAG_UID_BYTES)
        {
            return IMAGE_INVALID_UID_SIZE;
        }
        if (image->memory_size_bytes != type2->page_count * NTAG_PAGE_BYTES)
        {
            return IMAGE_INVALID_MEMORY_SIZE;
        }
        const type2_state_t *state = &image->state.type2;
        if (!type2->ev1)
        {
            return IMAGE_OK;
        }
        if (memcmp(state->version, type2->version, TYPE2_VERSION_BYTES) != 0)
        {
            return IMAGE_INVALID_STATE;
        }
        for (uint8_t index = 0u; index < TYPE2_COUNTER_COUNT; index++)
        {
            if (state->counters[index] > NTAG_COUNTER_MAX)
            {
                return IMAGE_INVALID_STATE;
            }
        }
        return IMAGE_OK;
    }
    const uint8_t count = image->model == CARD_ISO15693 ? 64u
                          : image->model == CARD_SLIX   ? 28u
                          : image->model == CARD_SLIX_L ? 8u
                                                        : 0u;
    if (count == 0u)
    {
        return IMAGE_UNSUPPORTED_MODEL;
    }
    if (image->uid_size_bytes != NFCV_UID_BYTES)
    {
        return IMAGE_INVALID_UID_SIZE;
    }
    if (image->memory_size_bytes != count * NFCV_BLOCK_BYTES ||
        image->state.nfcv.block_count != count)
    {
        return IMAGE_INVALID_MEMORY_SIZE;
    }
    if (image->uid[0] != NFCV_UID_PREFIX ||
        (image->model != CARD_ISO15693 &&
         (image->uid[1] != NFCV_MANUFACTURER_NXP ||
          image->uid[2] != (image->model == CARD_SLIX_L ? SLIX_L_PRODUCT_CODE
                                                        : SLIX_PRODUCT_CODE))))
    {
        return IMAGE_INVALID_UID;
    }
    const nfcv_state_t *state = &image->state.nfcv;
    if (state->locks == NULL)
    {
        return IMAGE_MISSING_STATE;
    }
    for (uint8_t index = 0u; index < count; index++)
    {
        if (state->locks[index] > 1u)
        {
            return IMAGE_INVALID_STATE;
        }
    }
    if ((image->model != CARD_SLIX_L &&
         (state->privacy_active || !zero_password(state->privacy_password) ||
          !zero_password(state->destroy_password))) ||
        (image->model == CARD_ISO15693 &&
         (state->eas_active || state->eas_locked ||
          state->eas_password_protected || state->afi_password_protected ||
          !zero_password(state->eas_afi_password))))
    {
        return IMAGE_INVALID_STATE;
    }
    return IMAGE_OK;
}
const char *image_status_string(image_status_t status)
{
    static const char *const messages[] = {"Valid card state",
                                           "NULL card or input",
                                           "Unsupported card model",
                                           "Missing card state storage",
                                           "Invalid UID size for model",
                                           "Invalid memory size for model",
                                           "UID does not match NFC-V model",
                                           "Invalid Classic access bits",
                                           "Invalid card state",
                                           "Invalid DESFire public capture"};
    return (unsigned)status < sizeof(messages) / sizeof(messages[0])
               ? messages[status]
               : "Unknown image status";
}
