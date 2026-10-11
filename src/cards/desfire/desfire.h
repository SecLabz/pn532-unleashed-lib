#pragma once

#include "pn532_unleashed/desfire_capture.h"
#include "pn532_unleashed/image.h"
#include "pn532_unleashed/nfca_desfire.h"
#include "protocols/nfca/tag.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    desfire_capture_t capture;
    uint8_t response_data_bytes;
    uint8_t version_part;
    int8_t selected_app;
    const uint8_t *pending_data;
    uint32_t pending_len;
    uint32_t pending_offset;
    uint8_t pending_status;
    uint8_t scratch[DESFIRE_MAX_SCRATCH_BYTES];
} desfire_context_t;

typedef struct
{
    uint8_t atqa[2];
    uint8_t sak;
    uint8_t ats[DESFIRE_MAX_ATS_BYTES];
    uint8_t ats_len;
} desfire_identity_t;

bool desfire_capture_valid(const desfire_capture_t *capture);

void desfire_reset_session(desfire_context_t *context);

bool desfire_handle(desfire_context_t *context,
                    const nfca_tag_request_t *request,
                    nfca_tag_response_t *response);
