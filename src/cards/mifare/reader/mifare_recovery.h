#pragma once

#include "cards/mifare/mifare_product.h"
#include "pn532_unleashed/nfca_mifare.h"

/* Reserve 8192/fc (604 us) for TX preparation at either supported clock.
 * The 54.24 MHz bench measured 360 us before the first scheduled AUTH. */
#define MIFARE_TX_PREPARE_TICKS 512u

/* Owned by the one library RF instance, never by USB. */
bool mifare_recovery_prepare(void);
void mifare_recovery_reset(void);
const nfca_tag_t *mifare_recovery_tag(void);
nfc_status_t mifare_recovery_reselect(void);
/* Complete incomplete-auth failure path before the next selection. */
nfc_status_t mifare_recovery_expire(void);
void mifare_darkside_reset(void);

bool mifare_recovery_valid_block(uint8_t block);
