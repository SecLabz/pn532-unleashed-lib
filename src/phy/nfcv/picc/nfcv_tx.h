#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Initializes NFC-V tag load modulation.
 */
bool nfcv_tx_init(void);

/*
 * Sends response at the NFC-V response delay measured from eof_time_us.
 */
bool nfcv_tx_send_response(uint32_t eof_time_us,
                           const uint8_t *response,
                           uint16_t response_len);
