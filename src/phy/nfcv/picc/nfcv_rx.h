#pragma once

#include "phy/nfcv/picc/nfcv_frame.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*nfcv_rx_frame_handler_t)(const nfcv_frame_t *frame,
                                        uint32_t eof_time_us);

/*
 * Initializes OOK capture and delivers decoded frames in the RX IRQ.
 * Capture stays stopped until handler returns after response transmission.
 */
bool nfcv_rx_init(nfcv_rx_frame_handler_t handler);

/* Stops capture and releases the shared PIO IRQ handler. */
void nfcv_rx_deinit(void);
