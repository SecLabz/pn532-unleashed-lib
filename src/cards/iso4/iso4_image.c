#include "cards/iso4/iso4_image.h"

#include <string.h>

bool iso4_activation_valid(uint16_t uid_size_bytes,
                           const uint8_t atqa[NFCA_ATQA_BYTES],
                           uint8_t sak,
                           const uint8_t *ats,
                           uint8_t ats_size_bytes)
{
    if (atqa == NULL || ats == NULL || ats_size_bytes < ISO4_MIN_ATS_BYTES ||
        ats_size_bytes > ISO4_MAX_ATS_BYTES)
    {
        return false;
    }
    uint8_t uid_bits;
    switch (uid_size_bytes)
    {
    case NFCA_UID_SINGLE_BYTES:
        uid_bits = NFCA_ATQA_UID_SINGLE;
        break;
    case NFCA_UID_DOUBLE_BYTES:
        uid_bits = NFCA_ATQA_UID_DOUBLE;
        break;
    case NFCA_UID_TRIPLE_BYTES:
        uid_bits = NFCA_ATQA_UID_TRIPLE;
        break;
    default:
        return false;
    }
    if ((atqa[0] & NFCA_ATQA_UID_SIZE_MASK) != uid_bits ||
        (sak & (NFCA_SAK_ISODEP | NFCA_SAK_CASCADE)) != NFCA_SAK_ISODEP ||
        ats[0] != ats_size_bytes)
    {
        return false;
    }
    const uint8_t t0 = ats[1];
    /* Validate optional interface bytes before accepting historical bytes. */
    const uint8_t minimum =
        ISO4_MIN_ATS_BYTES + ((t0 & ISO4_T0_TA_PRESENT) != 0u) +
        ((t0 & ISO4_T0_TB_PRESENT) != 0u) + ((t0 & ISO4_T0_TC_PRESENT) != 0u);
    return (t0 & ISO4_T0_RESERVED) == 0u &&
           (t0 & ISO4_T0_FSCI_MASK) <= ISO4_MAX_FSCI &&
           ats_size_bytes >= minimum;
}

bool iso4_image_valid(const card_image_t *image)
{
    if (image == NULL || image->uid == NULL || image->memory_size_bytes != 0u)
    {
        return false;
    }
    const iso4_state_t *state = &image->state.iso4;
    return iso4_activation_valid(image->uid_size_bytes,
                                 state->atqa,
                                 state->sak,
                                 state->ats,
                                 state->ats_size_bytes);
}
