#pragma once

#include "phy/nfca/picc/nfca_frame.h"
#include "pn532_unleashed/image.h"
#include "pn532_unleashed/integrity.h"
#include "protocols/nfca/tag.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Contract implemented by every NFC-A tag profile.
 * Image loading fully resets profile state. handle_frame inspects only request,
 * fills response, and returns without logging, allocating, or calling the PHY.
 * The backend transmits response after handle_frame returns. A deferred-parity
 * response supplies the first ciphertext bit and a bounded byte producer;
 * reader authentication must be fully verified before returning that response.
 */
typedef struct
{
    bool (*handle_frame)(const nfca_tag_request_t *frame,
                         nfca_tag_response_t *response);
    /* Optional: called after handling each frame and transmitting any response,
     * still in the RX IRQ with capture stopped. Must finish before the reader's
     * next frame; this is bounded preparation, not a background idle task.
     * The backend must call it even when handle_frame returns no response. */
    void (*prepare_next_exchange)(void);
} nfca_tag_profile_t;

extern const nfca_tag_profile_t nfca_mifare_profile;
extern const nfca_tag_profile_t nfca_ntag_profile;
extern const nfca_tag_profile_t nfca_iso4_profile;
bool nfca_iso4_load_image(const card_image_t *image);
bool nfca_mifare_load_image(const card_image_t *image);
bool nfca_ntag_load_image(const card_image_t *image);

/*
 * Semantic response helpers for NFC-A tags.
 */
static inline void nfca_reply_silent(nfca_tag_response_t *response)
{
    response->frame.type = NFCA_TAG_RESPONSE_NONE;
    response->frame.size_bytes = 0u;
    response->frame.short4 = 0u;
}

static inline void nfca_reply_short(nfca_tag_response_t *response,
                                    uint8_t value)
{
    response->frame.type = NFCA_TAG_RESPONSE_SHORT4;
    response->frame.size_bytes = 0u;
    response->frame.short4 = (uint8_t)(value & NFCA_SHORT_RESPONSE_MASK);
}

static inline void nfca_reply_ack(nfca_tag_response_t *response)
{
    nfca_reply_short(response, NFCA_ACK);
}

static inline void nfca_reply_nack(nfca_tag_response_t *response,
                                   uint8_t nack_code)
{
    nfca_reply_short(response, nack_code);
}

/*
 * Configures a byte response for which the PHY generates standard parity.
 */
static inline void nfca_tag_response_data(nfca_tag_response_t *response,
                                          uint16_t len)
{
    response->frame.type = NFCA_TAG_RESPONSE_DATA;
    response->frame.size_bytes = len;
    response->frame.short4 = 0u;
}

/*
 * Configures a byte response using the explicit parity buffer.
 */
static inline void
nfca_tag_response_data_with_parity(nfca_tag_response_t *response, uint16_t len)
{
    response->frame.type = NFCA_TAG_RESPONSE_DATA_WITH_PARITY;
    response->frame.size_bytes = len;
    response->frame.short4 = 0u;
}

/*
 * Appends CRC-A and configures the complete byte response length.
 */
static inline void nfca_tag_response_data_crc(nfca_tag_response_t *response,
                                              uint16_t payload_len)
{
    nfca_append_crc(response->frame.bytes, payload_len);
    nfca_tag_response_data(response, (uint16_t)(payload_len + NFC_CRC_BYTES));
}

static inline void nfca_reply_byte_crc(nfca_tag_response_t *response,
                                       uint8_t byte)
{
    response->frame.bytes[0] = byte;
    nfca_tag_response_data_crc(response, 1u);
}

/*
 * Returns true when frame is a parity- and CRC-valid command with expected
 * length.
 */
static inline bool
match_cmd(const nfca_tag_request_t *frame, uint8_t command, uint16_t len)
{
    return !frame->short_frame && frame->size_bytes == len &&
           frame->parity_ok && frame->crc_ok && frame->bytes[0] == command;
}
