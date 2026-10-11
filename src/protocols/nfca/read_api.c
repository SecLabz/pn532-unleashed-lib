#include "api_internal.h"
#include "pn532_unleashed/nfca.h"
#include "protocols/nfca/reader.h"

nfc_status_t nfca_scan(nfca_tag_t *tag)
{
    if (tag == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t status = nfc_operation_begin(NFC_STATE_READING_NFCA);
    if (status != NFC_OK)
    {
        return status;
    }
    nfca_tag_t staged;
    nfc_status_t result = nfca_reader_scan_start(&staged);
    if (!nfca_reader_stop())
    {
        result = NFC_RF_ERROR;
    }
    nfc_operation_end(result);
    if (result != NFC_OK)
    {
        return result;
    }
    *tag = staged;
    return NFC_OK;
}
