/* Actual card emulators exercise encrypted Classic and Type 2 transactions. */
#include "api_internal.h"
#include "cards/nfca_access.h"
#include "pn532_unleashed/reader.h"
#include "protocols/nfca/reader.h"

#ifndef UNLEASHED_MAGIC_HOST_TEST
static bool owned;
nfc_status_t nfc_operation_begin(nfc_state_t state)
{
    if (owned)
    {
        return NFC_BUSY;
    }
    owned = state == NFC_STATE_READING_NFCA;
    return owned ? NFC_OK : NFC_INVALID_ARGUMENT;
}
nfc_status_t nfc_operation_require(nfc_state_t state)
{
    return owned && state == NFC_STATE_READING_NFCA ? NFC_OK : NFC_BUSY;
}
nfc_status_t nfc_stop(void)
{
    owned = false;
    nfca_access_reset();
    return nfca_reader_stop() ? NFC_OK : NFC_RF_ERROR;
}

#endif
