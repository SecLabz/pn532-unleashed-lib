#include "api_internal.h"
#include "pn532_unleashed.h"
#include "protocols/nfcv/reader.h"

nfc_status_t nfcv_scan(nfcv_tag_t *tag)
{
    if (tag == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_status_t status = nfc_operation_begin(NFC_STATE_READING_NFCV);
    if (status != NFC_OK)
    {
        return status;
    }
    const nfc_status_t result = nfcv_reader_scan(tag);
    nfc_operation_end(result);
    return result;
}

nfc_status_t nfcv_slix_read(nfcv_slix_dump_t *dump)
{
    if (dump == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    dump->memory_size_bytes = 0u;
    const nfc_status_t status = nfc_operation_begin(NFC_STATE_READING_NFCV);
    if (status != NFC_OK)
    {
        return status;
    }
    const nfc_status_t result = nfcv_reader_slix_read(dump);
    nfc_operation_end(result);
    return result;
}
