#pragma once

#include "pn532_unleashed.h"
#include "pn532_unleashed/reader.h"

/* Built-in operations restore idle only after mandatory RF cleanup. */
nfc_status_t nfc_operation_begin(nfc_state_t state);
nfc_status_t nfc_operation_require(nfc_state_t state);
void nfc_operation_end(nfc_status_t status);

/* Foreground public reader ownership, distinct from built-in reads/recovery.
 * One owner shares the existing library state; stop always clears ownership. */
nfc_status_t nfc_session_begin(nfc_state_t state);
nfc_status_t nfc_session_require(nfc_state_t state);
void nfc_session_error(void);

static inline nfc_status_t nfc_session_result(nfc_status_t status)
{
    if (status == NFC_RF_ERROR || status == NFC_INVALID_RESPONSE ||
        status == NFC_NO_RESPONSE)
    {
        nfc_session_error();
    }
    return status;
}

static inline bool reader_options_resolve(const reader_options_t *options,
                                          uint32_t maximum_us,
                                          uint32_t *timeout_us,
                                          bool *expect_response)
{
    *timeout_us = maximum_us;
    *expect_response = true;
    if (options == NULL)
    {
        return true;
    }
    if ((options->expect_response != READER_RESPONSE_REQUIRED &&
         options->expect_response != READER_RESPONSE_NONE) ||
        options->sof_timeout_us > maximum_us)
    {
        return false;
    }
    *expect_response = options->expect_response == READER_RESPONSE_REQUIRED;
    if (options->sof_timeout_us != 0u)
    {
        *timeout_us = options->sof_timeout_us;
    }
    return true;
}
