#include "phy/nfca/pcd/nfca_pcd_encode.h"

#include "pn532_unleashed/integrity.h"

#include <stddef.h>
#include <stdint.h>

#define NFCA_BIT_FC 128u
#define NFCA_HALF_BIT_FC 64u
#define NFCA_PAUSE_FC 32u
#define NFCA_GUARD_FC 320u
#define NFCA_PRE_SOF_FC 32u

static bool append_x(reader_tx_waveform_t *waveform)
{
    return reader_tx_waveform_append(
               waveform, READER_TX_LEVEL_FULL, NFCA_HALF_BIT_FC) &&
           reader_tx_waveform_append(
               waveform, READER_TX_LEVEL_OFF, NFCA_PAUSE_FC) &&
           reader_tx_waveform_append(waveform,
                                     READER_TX_LEVEL_FULL,
                                     NFCA_HALF_BIT_FC - NFCA_PAUSE_FC);
}

static bool append_y(reader_tx_waveform_t *waveform)
{
    return reader_tx_waveform_append(
        waveform, READER_TX_LEVEL_FULL, NFCA_BIT_FC);
}

static bool append_z(reader_tx_waveform_t *waveform)
{
    return reader_tx_waveform_append(
               waveform, READER_TX_LEVEL_OFF, NFCA_PAUSE_FC) &&
           reader_tx_waveform_append(
               waveform, READER_TX_LEVEL_FULL, NFCA_BIT_FC - NFCA_PAUSE_FC);
}

static bool
append_bit(reader_tx_waveform_t *waveform, bool bit, bool *previous_was_x)
{
    if (bit)
    {
        *previous_was_x = true;
        return append_x(waveform);
    }

    const bool result =
        *previous_was_x ? append_y(waveform) : append_z(waveform);
    *previous_was_x = false;
    return result;
}

bool nfca_pcd_encode(const reader_frame_t *frame,
                     reader_tx_waveform_t *waveform)
{
    if (frame == NULL || waveform == NULL || frame->length == 0u ||
        frame->length > READER_FRAME_MAX_BYTES || frame->last_byte_bits > 7u)
    {
        return false;
    }

    reader_tx_waveform_init(waveform);
    if (!reader_tx_waveform_append(
            waveform, READER_TX_LEVEL_FULL, NFCA_GUARD_FC + NFCA_PRE_SOF_FC) ||
        !append_z(waveform))
    {
        return false;
    }

    bool previous_was_x = false;
    for (size_t byte_index = 0u; byte_index < frame->length; byte_index++)
    {
        uint8_t bits = 8u;
        if (byte_index + 1u == frame->length && frame->last_byte_bits != 0u)
        {
            bits = frame->last_byte_bits;
        }

        for (uint8_t bit = 0u; bit < bits; bit++)
        {
            if (!append_bit(waveform,
                            ((frame->data[byte_index] >> bit) & 1u) != 0u,
                            &previous_was_x))
            {
                return false;
            }
        }

        if (frame->byte_parity && bits == 8u)
        {
            const bool parity =
                frame->supplied_parity
                    ? frame->parity[byte_index] != 0u
                    : (nfca_odd_parity_bit(frame->data[byte_index]) != 0u);
            if (!append_bit(waveform, parity, &previous_was_x))
            {
                return false;
            }
        }
    }

    return (previous_was_x ? append_y(waveform) : append_z(waveform)) &&
           append_y(waveform);
}
