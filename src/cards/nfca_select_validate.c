#include "cards/mifare/mifare_product.h"
#include "cards/nfca_access.h"
#include "cards/type2/ntag_config.h"

nfc_status_t nfca_select_validate(const nfca_tag_t *expected,
                                  card_model_t model)
{
    if (expected == NULL ||
        (expected->uid_size_bytes != NFCA_UID_SINGLE_BYTES &&
         expected->uid_size_bytes != NFCA_UID_DOUBLE_BYTES))
    {
        return NFC_INVALID_ARGUMENT;
    }
    const mifare_product_config_t *classic = mifare_model_config(model);
    const ntag_config_t *type2 = ntag_model_config(model);
    if (classic == NULL && type2 == NULL)
    {
        return NFC_UNSUPPORTED;
    }
    if ((classic != NULL &&
         (expected->sak & (uint8_t)~NFCA_SAK_ISODEP) != classic->sak) ||
        (type2 != NULL && (expected->sak != 0u ||
                           expected->uid_size_bytes != NFCA_UID_DOUBLE_BYTES)))
    {
        return NFC_INVALID_ARGUMENT;
    }
    return NFC_OK;
}
