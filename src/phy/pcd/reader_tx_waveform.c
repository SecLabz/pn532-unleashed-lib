#include "phy/pcd/reader_tx_waveform.h"

#include <limits.h>
#include <stddef.h>

void reader_tx_waveform_init(reader_tx_waveform_t *waveform)
{
    if (waveform != NULL)
    {
        waveform->count = 0u;
    }
}

bool reader_tx_waveform_append(reader_tx_waveform_t *waveform,
                               reader_tx_level_t level,
                               uint16_t duration_fc)
{
    if (waveform == NULL || duration_fc == 0u || level > READER_TX_LEVEL_FULL)
    {
        return false;
    }

    if (waveform->count != 0u)
    {
        reader_tx_segment_t *last = &waveform->segments[waveform->count - 1u];
        if (last->level == level &&
            last->duration_fc <= (uint16_t)(UINT16_MAX - duration_fc))
        {
            last->duration_fc = (uint16_t)(last->duration_fc + duration_fc);
            return true;
        }
    }

    if (waveform->count >= READER_TX_WAVEFORM_MAX_SEGMENTS)
    {
        return false;
    }
    waveform->segments[waveform->count++] = (reader_tx_segment_t){
        .duration_fc = duration_fc,
        .level = level,
    };
    return true;
}
