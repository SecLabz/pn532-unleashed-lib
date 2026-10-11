#include "phy/nfcv/pcd/nfcv_pcd_encode.h"

#include "phy/nfcv/picc/nfcv_frame.h"

#include <stddef.h>
#include <stdint.h>

#define NFCV_GUARD_SLOTS 11u
#define NFCV_SOF_PATTERN NFCV_1OF4_SOF
#define NFCV_EOF_PATTERN NFCV_1OF4_EOF

/* Each pattern bit selects full carrier or a pause for one 128/fc slot. */
static const uint8_t nfcv_1_out_of_4[4] = {
    0xbfu,
    0xefu,
    0xfbu,
    0xfeu,
};

static bool append_pattern(reader_tx_waveform_t *waveform, uint8_t pattern)
{
    for (uint8_t bit = 0u; bit < 8u; bit++)
    {
        const reader_tx_level_t level = (pattern & 0x80u) != 0u
                                            ? READER_TX_LEVEL_FULL
                                            : READER_TX_LEVEL_OFF;
        if (!reader_tx_waveform_append(waveform, level, NFCV_PCD_SLOT_FC))
        {
            return false;
        }
        pattern <<= 1;
    }
    return true;
}

bool nfcv_pcd_encode(const reader_frame_t *frame,
                     reader_tx_waveform_t *waveform)
{
    if (frame == NULL || waveform == NULL || frame->length == 0u ||
        frame->length > READER_FRAME_MAX_BYTES)
    {
        return false;
    }

    reader_tx_waveform_init(waveform);
    if (!reader_tx_waveform_append(waveform,
                                   READER_TX_LEVEL_FULL,
                                   NFCV_GUARD_SLOTS * NFCV_PCD_SLOT_FC) ||
        !append_pattern(waveform, NFCV_SOF_PATTERN))
    {
        return false;
    }

    for (size_t byte_index = 0u; byte_index < frame->length; byte_index++)
    {
        uint8_t value = frame->data[byte_index];
        for (uint8_t symbol = 0u; symbol < 4u; symbol++)
        {
            if (!append_pattern(waveform, nfcv_1_out_of_4[value & 0x03u]))
            {
                return false;
            }
            value >>= 2;
        }
    }

    return append_pattern(waveform, NFCV_EOF_PATTERN);
}
