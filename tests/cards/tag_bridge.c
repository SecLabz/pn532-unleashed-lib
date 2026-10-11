#include "cards/image_internal.h"
#include "pn532_unleashed/image.h"
#include "protocols/nfca/nfca_profile.h"
#include "protocols/nfcv/nfcv_profile.h"

#include <string.h>

static const nfca_tag_profile_t *active_nfca;
static const nfcv_tag_profile_t *active_nfcv;

/* Physical timing is exercised on hardware, not imitated by host tests. */
void nfca_tx_set_fdt_base(uint32_t cycles)
{
    (void)cycles;
}

bool host_tag_load_view(const card_image_t *image)
{
    if (image_validate(image) != IMAGE_OK)
    {
        return false;
    }
    active_nfca = NULL;
    active_nfcv = NULL;
    if (card_model_is_nfca(image->model))
    {
        if (card_model_is_classic(image->model))
        {
            active_nfca = &nfca_mifare_profile;
            return nfca_mifare_load_image(image);
        }
        if (card_model_is_iso4(image->model))
        {
            active_nfca = &nfca_iso4_profile;
            return nfca_iso4_load_image(image);
        }
        active_nfca = &nfca_ntag_profile;
        return nfca_ntag_load_image(image);
    }
    active_nfcv = &nfcv_vicinity_profile;
    return nfcv_load_image(image);
}

size_t host_tag_exchange(const uint8_t *bytes,
                         uint16_t size,
                         bool short_frame,
                         bool valid_crc,
                         uint8_t *output,
                         size_t capacity)
{
    if (active_nfca != NULL)
    {
        const nfca_tag_request_t request = {
            .bytes = bytes,
            .size_bytes = size,
            .short_frame = short_frame,
            .parity_ok = true,
            .crc_ok = valid_crc,
            .timestamp_us = 200000u,
        };
        nfca_tag_response_t response;
        const bool handled = active_nfca->handle_frame(&request, &response);
        size_t result = 0u;
        if (handled &&
            response.frame.type == NFCA_TAG_RESPONSE_DEFERRED_PARITY &&
            response.prepare_bytes != NULL &&
            response.frame.size_bytes <= capacity)
        {
            /* Exercise the producer contract without simulating RF timing. */
            for (uint16_t index = 0u; index < response.frame.size_bytes;
                 index++)
            {
                response.prepare_bytes(&response, index, 1u);
            }
        }
        if (handled && response.frame.type == NFCA_TAG_RESPONSE_SHORT4 &&
            capacity >= 1u)
        {
            output[0] = response.frame.short4;
            result = 1u;
        }
        else if (handled &&
                 (response.frame.type == NFCA_TAG_RESPONSE_DATA ||
                  response.frame.type == NFCA_TAG_RESPONSE_DATA_WITH_PARITY ||
                  response.frame.type == NFCA_TAG_RESPONSE_DEFERRED_PARITY) &&
                 response.frame.size_bytes <= capacity)
        {
            memcpy(output, response.frame.bytes, response.frame.size_bytes);
            result = response.frame.size_bytes;
        }
        if (active_nfca->prepare_next_exchange != NULL)
        {
            active_nfca->prepare_next_exchange();
        }
        return result;
    }
    if (active_nfcv != NULL && size <= NFCV_FRAME_MAX_BYTES)
    {
        nfcv_frame_t frame = {
            .size_bytes = size,
            .crc_ok = valid_crc,
            .high_data_rate = true,
            .single_subcarrier = true,
        };
        memcpy(frame.bytes, bytes, size);
        nfcv_tag_response_t response;
        if (active_nfcv->handle_frame(&frame, &response) &&
            response.size_bytes <= capacity)
        {
            memcpy(output, response.bytes, response.size_bytes);
            return response.size_bytes;
        }
    }
    return 0u;
}
