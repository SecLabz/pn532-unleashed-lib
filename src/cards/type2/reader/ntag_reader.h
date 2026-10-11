#pragma once

#include "cards/type2/ntag_config.h"
#include "protocols/nfca/reader.h"

/* Requires a selected Type 2 tag. Output is invalid unless NFC_OK is returned.
 * PWD/PACK pages remain RF-masked; no hidden state is fabricated. */
nfc_status_t ntag_reader_read(const ntag_config_t *config,
                              const uint8_t *password,
                              nfca_dump_t *dump);
