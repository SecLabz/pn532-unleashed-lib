#include "phy/pcd/reader_tx_waveform.h"

#include "common/compiler.h"

#include <limits.h>
#include <stddef.h>

bool RAM_FUNC(reader_tx_waveform_slots)(const reader_tx_waveform_t *waveform,
                                        uint16_t tick_fc,
                                        uint32_t *slots,
                                        uint16_t capacity_slots,
                                        uint16_t *count)
{
    if (count == NULL)
    {
        return false;
    }
    *count = 0u;
    if (waveform == NULL || slots == NULL || capacity_slots == 0u ||
        tick_fc == 0u || waveform->count > READER_TX_WAVEFORM_MAX_SEGMENTS)
    {
        return false;
    }
    const bool binary_tick = (tick_fc & (tick_fc - 1u)) == 0u;
    const uint8_t shift = fast_ctz32(tick_fc);
    for (uint16_t index = 0u; index < waveform->count; index++)
    {
        const reader_tx_segment_t *segment = &waveform->segments[index];
        /* Both physical protocols use binary carrier ticks. Avoid two
         * hardware-divider calls per segment without changing any slot. */
        const uint16_t length = binary_tick ? segment->duration_fc >> shift
                                            : segment->duration_fc / tick_fc;
        if ((uint32_t)length * tick_fc != segment->duration_fc ||
            segment->level > READER_TX_LEVEL_FULL)
        {
            return false;
        }
        if ((uint32_t)*count + length >= capacity_slots)
        {
            return false;
        }
        for (uint16_t slot = 0u; slot < length; slot++)
        {
            slots[(*count)++] = segment->level == READER_TX_LEVEL_FULL;
        }
    }
    slots[*count] = 1u;
    return *count != 0u;
}
