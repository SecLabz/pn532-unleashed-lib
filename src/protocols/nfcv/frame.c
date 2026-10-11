#include "pn532_unleashed/nfcv_frame.h"

void nfcv_uid_to_wire(uint8_t *dst, const uint8_t *uid)
{
    for (uint8_t i = 0u; i < NFCV_UID_SIZE; i++)
    {
        dst[i] = uid[NFCV_UID_SIZE - 1u - i];
    }
}

bool nfcv_wire_uid_matches(const uint8_t *wire_uid, const uint8_t *uid)
{
    for (uint8_t i = 0u; i < NFCV_UID_SIZE; i++)
    {
        if (wire_uid[i] != uid[NFCV_UID_SIZE - 1u - i])
        {
            return false;
        }
    }
    return true;
}

bool nfcv_inventory_mask_matches(const uint8_t *wire_uid,
                                 uint8_t mask_bits,
                                 const uint8_t *mask_value,
                                 uint8_t mask_size_bytes)
{
    const uint8_t full_bytes = (uint8_t)(mask_bits / 8u);
    const uint8_t spare_bits = (uint8_t)(mask_bits % 8u);
    const uint8_t needed = (uint8_t)(full_bytes + (spare_bits != 0u ? 1u : 0u));

    if (wire_uid == NULL || mask_bits > NFCV_UID_SIZE * 8u ||
        mask_size_bytes < needed || (needed != 0u && mask_value == NULL))
    {
        return false;
    }

    for (uint8_t i = 0u; i < full_bytes; i++)
    {
        if (mask_value[i] != wire_uid[i])
        {
            return false;
        }
    }

    if (spare_bits != 0u)
    {
        const uint8_t mask = (uint8_t)((1u << spare_bits) - 1u);
        return (mask_value[full_bytes] & mask) == (wire_uid[full_bytes] & mask);
    }

    return true;
}
