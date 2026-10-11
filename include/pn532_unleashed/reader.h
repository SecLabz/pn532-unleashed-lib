#pragma once

#include "pn532_unleashed/status.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        READER_RESPONSE_REQUIRED = 0,
        READER_RESPONSE_NONE,
    } reader_response_expectation_t;

    /* NULL options or zero timeout use the protocol default. Timeouts may be
     * reduced, not increased beyond the published maximum. NONE transmits
     * without capture: success confirms TX only, never the card's action.
     * Exchanges never retry. A timeout after a command has an unknown outcome.
     * With REQUIRED, NO_RESPONSE/INVALID_RESPONSE/RF_ERROR enter ERROR; stop
     * before restarting. Discovery silence returns NO_TAG and keeps the reader
     * session open. Invalid arguments and BUFFER_TOO_SMALL preserve the role.
     * A valid card NACK/error is a successful exchange, not an RF failure. */
    typedef struct
    {
        reader_response_expectation_t expect_response;
        uint32_t sof_timeout_us;
    } reader_options_t;

    /* Start requires initialized IDLE, claims one foreground session on the
     * owning core and keeps the field on until nfc_stop(). No selection is
     * implied. No concurrent/reentrant calls or host work inside exchanges.
     * Rejection before acquisition preserves the current role. Hardware start
     * failure attempts cleanup and enters ERROR; stop before restarting.
     * Sessions do not expire when USB closes. Neither start nor discovery
     * retries automatically. */
    nfc_status_t nfca_reader_start(void);
    nfc_status_t nfcv_reader_start(void);

#ifdef __cplusplus
}
#endif
