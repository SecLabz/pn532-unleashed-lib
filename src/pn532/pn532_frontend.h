#pragma once

#include "pn532_unleashed/core.h"
#include "pn532_unleashed/diagnostics.h"
#include "pn532_unleashed/nfca.h"

#include <stdbool.h>
#include <stdint.h>

#define PN532_PROTOCOL_NFCA 1u
#define PN532_PROTOCOL_NFCV 3u

/* PN532 configuration belongs to role transitions, never RF timing paths. */
bool pn532_frontend_init(void);
bool pn532_frontend_prepare_tag(uint8_t protocol);
bool pn532_frontend_arm_tag(void);
bool pn532_frontend_prepare_reader(uint8_t protocol);
bool pn532_frontend_set_field(bool enabled);
bool pn532_frontend_stop(void);
#ifdef PNU_RF_TRACE
/* Read the bounded register snapshot before tag RX is armed. */
bool pn532_frontend_trace_snapshot(void);
/* Bench only: select an existing reference NFC-A load setting while idle.
 * Applied and read back before the next tag is armed. NFC-V is unchanged. */
bool pn532_frontend_trace_set_gsn(uint8_t value);
#endif
