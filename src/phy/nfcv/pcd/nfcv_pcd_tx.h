#pragma once

#include "phy/reader_frame.h"

#include <stdbool.h>

/*
 * Initializes NFC-V 1-out-of-4 reader transmission.
 */
bool nfcv_pcd_tx_init(void);

/*
 * Sends one high-rate NFC-V reader frame.
 */
/* Returns false when the frame cannot be encoded or reader TX scratch is busy.
 */
bool nfcv_pcd_tx_send(const reader_frame_t *frame);
