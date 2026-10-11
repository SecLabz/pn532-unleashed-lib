#include "phy/nfcv/picc/nfcv_frame.h"

#ifdef PICO_ON_DEVICE
#include "pico.h"
#else
#define __not_in_flash_func(name) name
#endif

bool __not_in_flash_func(nfcv_decode_1of4_sample)(uint8_t sample,
                                                  uint8_t *two_bits)
{
    switch ((uint8_t)~sample)
    {
    case 0x40u:
        *two_bits = 0u;
        return true;
    case 0x10u:
        *two_bits = 1u;
        return true;
    case 0x04u:
        *two_bits = 2u;
        return true;
    case 0x01u:
        *two_bits = 3u;
        return true;
    default:
        return false;
    }
}

bool __not_in_flash_func(nfcv_sample_is_sof)(uint8_t sample)
{
    return sample == NFCV_1OF4_SOF;
}

bool __not_in_flash_func(nfcv_sample_is_eof)(uint8_t sample)
{
    return sample == NFCV_1OF4_EOF;
}
