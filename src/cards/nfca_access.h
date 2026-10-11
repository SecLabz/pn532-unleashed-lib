#pragma once
#include "pn532_unleashed/nfca.h"

void nfca_access_reset(void);
void mifare_access_reader_reset(void);
void mifare_access_reader_selected(void);
void ntag_access_reader_reset(void);
nfc_status_t ntag_access_reader_prepare(const uint8_t *password,
                                        nfca_dump_t *dump);
const nfca_tag_t *nfca_selected_tag(void);
card_model_t nfca_selected_model(void);
nfc_status_t nfca_select_validate(const nfca_tag_t *expected,
                                  card_model_t model);
nfc_status_t mifare_access_reader(uint8_t block,
                                  bool key_b,
                                  const uint8_t key[6],
                                  const uint8_t *before,
                                  const uint8_t *after,
                                  uint8_t data[16],
                                  bool *writable);
nfc_status_t ntag_access_reader(uint8_t page,
                                const uint8_t *password,
                                const uint8_t *before,
                                const uint8_t *after,
                                uint8_t data[4],
                                bool *writable);
