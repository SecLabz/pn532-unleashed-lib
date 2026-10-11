#pragma once

#include "pn532_unleashed/image.h"
#include "pn532_unleashed/nfcv_tag.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Contract implemented by every NFC-V tag profile.
 * Image loading resets all state. handle_frame only builds responses and must
 * not log, allocate, wait, or drive the physical layer.
 */
typedef struct
{
    bool (*handle_frame)(const nfcv_frame_t *frame,
                         nfcv_tag_response_t *response);
} nfcv_tag_profile_t;

extern const nfcv_tag_profile_t nfcv_vicinity_profile;
bool nfcv_load_image(const card_image_t *image);
