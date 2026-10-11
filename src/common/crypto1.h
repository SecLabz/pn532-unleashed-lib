/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2008-2014 bla <blapost@gmail.com>
 * Copyright (C) Proxmark3 contributors. See upstream AUTHORS.md for details.
 *
 * Adapted for the RP2040; original third-party notices are retained.
 * See docs/licenses/NOTICE.md and LICENSE.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CRYPTO1_KEY_BYTES 6u
#define CRYPTO1_NONCE_BYTES 4u

#define MIFARE_NONCE_PRNG_STEPS 32u
#define MIFARE_READER_ANSWER_PRNG_STEPS 64u
#define MIFARE_CARD_ANSWER_PRNG_STEPS 96u

typedef struct
{
    uint32_t odd;
    uint32_t even;
} crypto1_state_t;

/*
 * Builds the SRAM lookup tables used by the Crypto1 hot paths. Firmware
 * initializers call this before accepting RF frames; repeated calls are cheap.
 */
void crypto1_prepare(void);

static inline uint64_t
crypto1_key_from_bytes(const uint8_t key[CRYPTO1_KEY_BYTES])
{
    uint64_t value = 0u;
    for (uint8_t index = 0u; index < CRYPTO1_KEY_BYTES; index++)
    {
        value = (value << 8u) | key[index];
    }
    return value;
}

void crypto1_init(crypto1_state_t *state, uint64_t key);
void crypto1_clear(crypto1_state_t *state);
uint8_t crypto1_filter(const crypto1_state_t *state);
uint8_t crypto1_bit(crypto1_state_t *state, uint8_t input, bool encrypted);
uint8_t crypto1_byte(crypto1_state_t *state, uint8_t input, bool encrypted);
uint32_t crypto1_word(crypto1_state_t *state, uint32_t input, bool encrypted);
uint32_t crypto1_prng_successor(uint32_t value, uint32_t clocks);

void crypto1_bytes_plain(crypto1_state_t *state,
                         const uint8_t *input,
                         uint8_t *output,
                         uint8_t *filter,
                         uint16_t len);

/* Verify all of Ar before emitting any response; then encrypt At in chunks.
 * prepare/verification must precede encryption so the tables are initialized.
 */
bool crypto1_verify_authentication(crypto1_state_t *state,
                                   const uint8_t *reader_nonce_enc,
                                   const uint8_t *reader_answer_enc,
                                   const uint8_t *expected_reader_answer);
void crypto1_encrypt_authentication_response(crypto1_state_t *state,
                                             const uint8_t *plain,
                                             const uint8_t *parity,
                                             uint8_t *output,
                                             uint8_t *output_parity,
                                             uint8_t len);
