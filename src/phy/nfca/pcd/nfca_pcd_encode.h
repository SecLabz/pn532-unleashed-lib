#pragma once

#include "phy/pcd/reader_tx_waveform.h"

/* Encode one bounded frame. Partial final bytes never carry parity. */
bool nfca_pcd_encode(const reader_frame_t *frame,
                     reader_tx_waveform_t *waveform);
