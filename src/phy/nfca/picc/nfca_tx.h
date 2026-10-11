#pragma once

#include "protocols/nfca/tag.h"

#include <stdbool.h>
#include <stdint.h>

/* Stop and configure the stopwatch before receiving a new frame. */
void nfca_tx_fdt_prepare(void);

/* Starts the prepared stopwatch, crediting elapsed RX pipeline cycles. */
void nfca_tx_fdt_start_elapsed(uint16_t elapsed_cycles);

/* Sets the nominal FDT base delay in system clock cycles. */
void nfca_tx_set_fdt_base(uint32_t base_cycles);

/* Sets the last bit polarity of the received PCD frame (0 or 1) for ISO 14443-3
 * FDT grid alignment. */
void nfca_tx_fdt_set_last_bit(bool last_bit_1);

void nfca_tx_send_response(nfca_tag_response_t *response);
