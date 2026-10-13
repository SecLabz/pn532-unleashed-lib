#pragma once

#include "cards/mifare/mifare_access.h"
#include "common/crypto1.h"
#include "pn532_unleashed/nfca_mifare.h"
#include "protocols/nfca/reader.h"

/* Borrowed credential; nonce is optional. Deadlines use fc/16 recovery ticks,
 * zero is immediate. Crypto1 preparation and entropy happen before AUTH. */
nfc_status_t mifare_reader_authenticate(const nfca_tag_t *tag,
                                        uint8_t block,
                                        bool key_b,
                                        const uint8_t key[MIFARE_KEY_BYTES],
                                        crypto1_state_t *cipher,
                                        uint32_t *nonce,
                                        uint32_t deadline_ticks,
                                        uint32_t reader_nonce);
void mifare_reader_command(reader_frame_t *tx, uint8_t opcode, uint8_t block);
void mifare_reader_encrypt(crypto1_state_t *cipher,
                           reader_frame_t *frame,
                           uint8_t input_size_bytes);
/* Complete a known-key encrypted AUTH, replacing the authenticated cipher. */
nfc_status_t mifare_reader_authenticate_nested(const nfca_tag_t *tag,
                                               const mifare_source_t *source,
                                               crypto1_state_t *cipher,
                                               uint32_t *nonce,
                                               uint32_t reader_nonce);
/* Caller owns authenticated cipher and 16-byte block output; CRC/parity
 * checked. Output changes only on success. */
nfc_status_t mifare_reader_block(crypto1_state_t *cipher,
                                 uint8_t block,
                                 uint8_t output[MIFARE_BLOCK_SIZE]);
/* Opcode variants are restricted to the explicit GDM UID writer. */
nfc_status_t
mifare_reader_authenticate_opcode(const nfca_tag_t *tag,
                                  uint8_t block,
                                  uint8_t opcode,
                                  const uint8_t key[MIFARE_KEY_BYTES],
                                  crypto1_state_t *cipher,
                                  uint32_t *nonce,
                                  uint32_t deadline_ticks,
                                  uint32_t reader_nonce);
nfc_status_t mifare_reader_block_opcode(crypto1_state_t *cipher,
                                        uint8_t block,
                                        uint8_t opcode,
                                        uint8_t output[MIFARE_BLOCK_SIZE]);

/* One encrypted write handshake, no retries. WRITE or explicit GDM A8 only.
 * Caller owns the authenticated cipher; an error may mean unknown outcome. */
nfc_status_t mifare_reader_write_opcode(crypto1_state_t *cipher,
                                        uint8_t block,
                                        uint8_t opcode,
                                        const uint8_t data[16]);
