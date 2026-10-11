#pragma once

#include "pn532_unleashed/nfcv_frame.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Transmitter capacity excluding the two CRC bytes appended by the PHY. */
#define NFCV_TAG_MAX_RESPONSE_BYTES (NFCV_RESPONSE_MAX_BYTES - NFC_CRC_BYTES)

    /* Decoded response payload, including flags but excluding CRC. The PHY
     * appends CRC privately. Only size_bytes bytes are meaningful; size_bytes
     * zero means silence. Helpers require a non-NULL response. */
    typedef struct
    {
        uint8_t bytes[NFCV_RESPONSE_MAX_BYTES];
        uint16_t size_bytes;
    } nfcv_tag_response_t;

    /*
     * Response helpers for NFC-V tags.
     */
    static inline void nfcv_reply_silent(nfcv_tag_response_t *response)
    {
        response->size_bytes = 0u;
    }

    static inline void nfcv_reply_ok(nfcv_tag_response_t *response)
    {
        response->bytes[0] = NFCV_RES_OK;
        response->size_bytes = 1u;
    }

    static inline void nfcv_reply_error(nfcv_tag_response_t *response,
                                        uint8_t error_code)
    {
        response->bytes[0] = NFCV_RES_ERROR;
        response->bytes[1] = error_code;
        response->size_bytes = 2u;
    }

    static inline bool nfcv_reply_data(nfcv_tag_response_t *response,
                                       const void *data,
                                       uint16_t size_bytes)
    {
        if (size_bytes > NFCV_TAG_MAX_RESPONSE_BYTES ||
            (size_bytes != 0u && data == NULL))
        {
            return false;
        }
        if (size_bytes != 0u)
        {
            memcpy(response->bytes, data, size_bytes);
        }
        response->size_bytes = size_bytes;
        return true;
    }

#ifdef __cplusplus
}
#endif
