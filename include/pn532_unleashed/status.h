#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        NFC_OK = 0,
        NFC_INVALID_ARGUMENT,
        NFC_INVALID_IMAGE,
        NFC_NOT_INITIALIZED,
        NFC_BUSY,
        NFC_WRONG_CLOCK,
        NFC_RF_ERROR,
        NFC_NO_TAG,
        NFC_RESOURCE_BUSY,
        NFC_UNSUPPORTED,
        NFC_ACCESS_DENIED,
        NFC_NO_RESPONSE,
        NFC_INVALID_RESPONSE,
        NFC_BUFFER_TOO_SMALL,
        NFC_CONTENT_CHANGED,
        NFC_WRITE_OUTCOME_UNKNOWN,
    } nfc_status_t;

#ifdef __cplusplus
}
#endif
