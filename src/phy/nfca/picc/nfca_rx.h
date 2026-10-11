#pragma once

#include "phy/nfca/picc/nfca_frame.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*nfca_rx_frame_handler_t)(const nfca_frame_t *frame);

/*
 * Initializes fixed-grid level capture and delivers each decoded NFC-A frame
 * to handler.
 */
bool nfca_rx_init(nfca_rx_frame_handler_t handler);

/*
 * Enables or disables the NFC-A receiver and its interrupt.
 * Call this only between RF frames when switching operating modes.
 */

/* Stops capture and releases the shared PIO IRQ handler. */
void nfca_rx_deinit(void);
