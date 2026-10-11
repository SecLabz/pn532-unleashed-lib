#include "phy/nfcv/pcd/nfcv_pcd_tx.h"

#include "phy/nfcv/pcd/nfcv_pcd_encode.h"
#include "phy/pcd/pn532_reader_sequence.h"
#include "phy/pcd/reader_tx_waveform.h"
#include "phy/reader_frame.h"

#include "pico/stdlib.h"

#include <stdint.h>

static reader_tx_waveform_t tx_waveform;

/*
 * Initializes the timing source used by NFC-V reader TX.
 */
bool nfcv_pcd_tx_init(void)
{
    return pn532_reader_sequence_init();
}

/*
 * Sends one NFC-V frame with 1-out-of-4 coding.
 */
bool __not_in_flash_func(nfcv_pcd_tx_send)(const reader_frame_t *frame)
{
    if (frame == NULL || frame->length == 0u ||
        frame->length > READER_FRAME_MAX_BYTES)
    {
        return false;
    }

    if (!nfcv_pcd_encode(frame, &tx_waveform))
    {
        return false;
    }
    return pn532_reader_sequence_play(&tx_waveform, NFCV_PCD_SLOT_FC);
}
