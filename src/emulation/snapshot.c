#include "emulation/snapshot.h"

#include "api_internal.h"
#include "cards/image_copy.h"
#include "cards/image_internal.h"
#include "emulation/emulation.h"

nfc_status_t emulation_snapshot(card_image_t *output,
                                uint8_t *storage,
                                size_t capacity_bytes)
{
    if (output == NULL || storage == NULL)
    {
        return NFC_INVALID_ARGUMENT;
    }
    const nfc_state_t state = nfc_state();
    const nfc_status_t status = nfc_operation_require(state);
    if (status != NFC_OK)
    {
        return status;
    }
    if (state != NFC_STATE_EMULATING_NFCA && state != NFC_STATE_EMULATING_NFCV)
    {
        return NFC_BUSY;
    }
    const card_model_t model = emulation_model();
    if (model == 0)
    {
        return NFC_UNSUPPORTED;
    }
    static card_image_t image; /* Foreground-only, no RF callbacks. */
    if (card_model_is_classic(model))
    {
        nfca_mifare_get_image(&image);
    }
    else if (card_model_is_iso4(model))
    {
        nfca_iso4_get_image(&image);
    }
    else if (card_model_is_nfca(model))
    {
        nfca_ntag_get_image(&image);
    }
    else if (!nfcv_get_image(&image))
    {
        return NFC_UNSUPPORTED;
    }
    if (image_validate(&image) != IMAGE_OK)
    {
        return NFC_INVALID_IMAGE;
    }
    return card_image_copy(&image, output, storage, capacity_bytes)
               ? NFC_OK
               : NFC_BUFFER_TOO_SMALL;
}
