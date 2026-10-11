#pragma once

#include "pn532_unleashed/config.h"

#include <stdbool.h>

/* Claim the fixed RF/control PIO state machines once, before frontend setup.
 * Call from the owning core's foreground initialization only. On conflict no
 * claims or hardware configuration change. Claims last until MCU reset. PWM0
 * must be inactive and PIO0 IRQ0 must have no installed handler at entry.
 * With PNU_ACTIVITY_LED, also require/claim PIO1 SM2 and preflight four
 * free LED instructions before making any claim. */
bool board_resources_claim(nfc_transport_t transport);
