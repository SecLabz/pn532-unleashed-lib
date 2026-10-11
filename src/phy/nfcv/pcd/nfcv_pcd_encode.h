#pragma once

#include "phy/pcd/reader_tx_waveform.h"
#include "phy/reader_frame.h"

#include <stdbool.h>

#define NFCV_PCD_SLOT_FC 128u

/* Encodes one high-rate, 1-out-of-4 NFC-V reader frame. */
bool nfcv_pcd_encode(const reader_frame_t *frame,
                     reader_tx_waveform_t *waveform);
