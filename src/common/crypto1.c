/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2008-2014 bla <blapost@gmail.com>
 * Copyright (C) Proxmark3 contributors. See upstream AUTHORS.md for details.
 *
 * Adapted for the RP2040; original third-party notices are retained.
 * See docs/licenses/NOTICE.md and LICENSE.
 */

#include "common/crypto1.h"

#include <stddef.h>

#ifndef UNLEASHED_HOST_TEST
#include "pico.h"
#else
#define __not_in_flash_func(name) name
#define __scratch_x(name)
#define __scratch_y(name)
#endif

/*
 * Compact Crypto1 implementation derived from the public Proxmark3/crapto1
 * algorithm, rewritten here for the RP2040 firmware and its fixed state API.
 */
#define CRYPTO1_POLY_ODD 0x29CE5Cu
#define CRYPTO1_POLY_EVEN 0x870804u

#define CRYPTO1_FILTER_F0 0xF22C0u
#define CRYPTO1_FILTER_F1 0x6C9C0u
#define CRYPTO1_FILTER_F2 0x3C8B0u
#define CRYPTO1_FILTER_F3 0x1E458u
#define CRYPTO1_FILTER_F4 0x0D938u
#define CRYPTO1_FILTER_G 0xEC57E80Au
#define CRYPTO1_PARITY_NIBBLE_TRUTH_TABLE 0x6996u
#define CRYPTO1_FILTER_LOW_INPUT_BITS 12u
#define CRYPTO1_FILTER_LOW_VALUES (1u << CRYPTO1_FILTER_LOW_INPUT_BITS)
#define CRYPTO1_BYTE_VALUES 256u
#define CRYPTO1_FEEDBACK_LANES 6u
#define CRYPTO1_HALF_BYTES 3u

/* Two SRAM lookups per filter evaluation. The extra 3824 bytes avoid a
 * third load and nibble extraction on the 54.24 MHz authentication path. */
static uint8_t crypto1_filter_low[CRYPTO1_FILTER_LOW_VALUES];
static uint8_t crypto1_filter_high[CRYPTO1_BYTE_VALUES];
static uint8_t crypto1_parity_lut[CRYPTO1_BYTE_VALUES];
static bool crypto1_filter_tables_ready;
#if defined(__arm__) && !defined(UNLEASHED_HOST_TEST)
static uint8_t crypto1_feedback8[CRYPTO1_FEEDBACK_LANES][CRYPTO1_BYTE_VALUES];
#endif

static inline uint8_t parity32(uint32_t value)
{
    value ^= value >> 16u;
    value ^= value >> 8u;
    return crypto1_parity_lut[value & 0xffu];
}

static inline uint8_t crypto1_filter_odd_fast(uint32_t x)
{
    const uint8_t outcomes = crypto1_filter_low[x & 0xfffu];
    const uint8_t selector =
        crypto1_filter_high[(x >> CRYPTO1_FILTER_LOW_INPUT_BITS) & 0xffu];
    return (uint8_t)((outcomes >> selector) & 1u);
}

#if defined(__arm__) && !defined(UNLEASHED_HOST_TEST)
#include "common/crypto1_arm.h"
#endif

void crypto1_prepare(void)
{
    if (crypto1_filter_tables_ready)
    {
        return;
    }

    for (uint16_t index = 0u; index < CRYPTO1_BYTE_VALUES; index++)
    {
        crypto1_filter_high[index] =
            (uint8_t)(((CRYPTO1_FILTER_F3 >> (index & 0x0fu)) & 2u) |
                      ((CRYPTO1_FILTER_F4 >> (index >> 4u)) & 1u));

        uint16_t v = index;
        v ^= v >> 4u;
        crypto1_parity_lut[index] =
            (uint8_t)((CRYPTO1_PARITY_NIBBLE_TRUTH_TABLE >> (v & 0x0fu)) & 1u);
    }

    for (uint16_t index = 0u; index < CRYPTO1_FILTER_LOW_VALUES; index++)
    {
        const uint8_t low =
            (uint8_t)(((CRYPTO1_FILTER_F0 >> (index & 0x0fu)) & 16u) |
                      ((CRYPTO1_FILTER_F1 >> ((index >> 4u) & 0x0fu)) & 8u) |
                      ((CRYPTO1_FILTER_F2 >> (index >> 8u)) & 4u));
        crypto1_filter_low[index] =
            (uint8_t)((CRYPTO1_FILTER_G >> low) & 0x0fu);
    }

#if defined(__arm__) && !defined(UNLEASHED_HOST_TEST)
    /* Zero-input feedback is linear in the 48-bit LFSR state. */
    for (uint8_t lane = 0u; lane < CRYPTO1_FEEDBACK_LANES; lane++)
    {
        for (uint16_t value = 0u; value < CRYPTO1_BYTE_VALUES; value++)
        {
            uint32_t odd =
                lane < CRYPTO1_HALF_BYTES ? (uint32_t)value << (8u * lane) : 0u;
            uint32_t even = lane >= CRYPTO1_HALF_BYTES
                                ? (uint32_t)value
                                      << (8u * (lane - CRYPTO1_HALF_BYTES))
                                : 0u;
            uint8_t feedback = 0u;
            for (uint8_t bit = 0u; bit < 8u; bit++)
            {
                const uint8_t fb = parity32((odd & CRYPTO1_POLY_ODD) ^
                                            (even & CRYPTO1_POLY_EVEN));
                feedback |= (uint8_t)(fb << bit);
                const uint32_t previous_odd = odd;
                odd = (even << 1u) | fb;
                even = previous_odd;
            }
            crypto1_feedback8[lane][value] = feedback;
        }
    }
#endif

    crypto1_filter_tables_ready = true;
}

uint8_t __not_in_flash_func(crypto1_filter)(const crypto1_state_t *state)
{
    if (!crypto1_filter_tables_ready)
    {
        crypto1_prepare();
    }
    return crypto1_filter_odd_fast(state->odd);
}

void __not_in_flash_func(crypto1_init)(crypto1_state_t *state, uint64_t key)
{
    state->odd = 0u;
    state->even = 0u;

    /* Consume each byte with 32-bit shifts; variable 64-bit shifts require
     * a runtime helper on Cortex-M0+. Preserve the original key bit order. */
    for (int byte = CRYPTO1_KEY_BYTES - 1u; byte >= 0; byte--)
    {
        const uint32_t half =
            byte >= 4 ? (uint32_t)(key >> 32u) : (uint32_t)key;
        const uint32_t value = half >> ((byte & 3) * 8);
        for (uint8_t bit = 0u; bit < 8u; bit += 2u)
        {
            state->odd = (state->odd << 1u) | ((value >> (bit + 1u)) & 1u);
            state->even = (state->even << 1u) | ((value >> bit) & 1u);
        }
    }
}

void __not_in_flash_func(crypto1_clear)(crypto1_state_t *state)
{
    state->odd = 0u;
    state->even = 0u;
}

uint8_t __not_in_flash_func(crypto1_bit)(crypto1_state_t *state,
                                         uint8_t input,
                                         bool encrypted)
{
    if (!crypto1_filter_tables_ready)
    {
        crypto1_prepare();
    }
    const uint8_t output = crypto1_filter_odd_fast(state->odd);
    uint32_t feedback = state->odd & CRYPTO1_POLY_ODD;
    feedback ^= state->even & CRYPTO1_POLY_EVEN;
    feedback ^= input != 0u;
    if (encrypted)
    {
        feedback ^= output;
    }

    const uint32_t previous_odd = state->odd;
    state->odd = (state->even << 1u) | parity32(feedback);
    state->even = previous_odd;
    return output;
}

uint8_t __not_in_flash_func(crypto1_byte)(crypto1_state_t *state,
                                          uint8_t input,
                                          bool encrypted)
{
    if (!crypto1_filter_tables_ready)
    {
        crypto1_prepare();
    }
    uint32_t odd = state->odd;
    uint32_t even = state->even;
    uint8_t output = 0u;
    for (uint8_t bit = 0u; bit < 8u; bit++)
    {
        const uint8_t key_bit = crypto1_filter_odd_fast(odd);
        uint32_t feedback = odd & CRYPTO1_POLY_ODD;
        feedback ^= even & CRYPTO1_POLY_EVEN;
        feedback ^= (input >> bit) & 1u;
        if (encrypted)
        {
            feedback ^= key_bit;
        }

        const uint32_t previous_odd = odd;
        odd = (even << 1u) | parity32(feedback);
        even = previous_odd;
        output |= (uint8_t)(key_bit << bit);
    }
    state->odd = odd;
    state->even = even;
    return output;
}

void __not_in_flash_func(crypto1_bytes_plain)(crypto1_state_t *state,
                                              const uint8_t *input,
                                              uint8_t *output,
                                              uint8_t *filter,
                                              uint16_t len)
{
    if (!crypto1_filter_tables_ready)
    {
        crypto1_prepare();
    }

#if defined(__arm__) && !defined(UNLEASHED_HOST_TEST)
    if (input == NULL)
    {
        /* Reuse the verified zero-feedback byte path for cached commands and
         * READ output. Preparation must finish before the next reader SOF. */
        for (uint16_t byte = 0u; byte < len; byte++)
        {
            output[byte] = crypto1_stream_byte_arm(state);
            if (filter != NULL)
            {
                filter[byte] = crypto1_filter_odd_fast(state->odd);
            }
        }
        return;
    }
#endif

    uint32_t odd = state->odd;
    uint32_t even = state->even;

    for (uint16_t byte = 0u; byte < len; byte++)
    {
        const uint8_t value = input != NULL ? input[byte] : 0u;
        uint8_t stream = 0u;

        for (uint8_t bit = 0u; bit < 8u; bit++)
        {
            const uint8_t key_bit = crypto1_filter_odd_fast(odd);
            uint32_t feedback = odd & CRYPTO1_POLY_ODD;
            feedback ^= even & CRYPTO1_POLY_EVEN;
            feedback ^= (value >> bit) & 1u;

            const uint32_t previous_odd = odd;
            odd = (even << 1u) | parity32(feedback);
            even = previous_odd;
            stream |= (uint8_t)(key_bit << bit);
        }

        output[byte] = stream;
        if (filter != NULL)
        {
            filter[byte] = crypto1_filter_odd_fast(odd);
        }
    }

    state->odd = odd;
    state->even = even;
}

uint32_t __not_in_flash_func(crypto1_word)(crypto1_state_t *state,
                                           uint32_t input,
                                           bool encrypted)
{
    if (!crypto1_filter_tables_ready)
    {
        crypto1_prepare();
    }
    uint32_t odd = state->odd;
    uint32_t even = state->even;
    uint32_t output = 0u;
    for (uint8_t bit = 0u; bit < 32u; bit++)
    {
        const uint8_t wire_bit = (uint8_t)(bit ^ 24u);
        const uint8_t key_bit = crypto1_filter_odd_fast(odd);
        uint32_t feedback = odd & CRYPTO1_POLY_ODD;
        feedback ^= even & CRYPTO1_POLY_EVEN;
        feedback ^= (input >> wire_bit) & 1u;
        if (encrypted)
        {
            feedback ^= key_bit;
        }

        const uint32_t previous_odd = odd;
        odd = (even << 1u) | parity32(feedback);
        even = previous_odd;
        output |= (uint32_t)key_bit << wire_bit;
    }
    state->odd = odd;
    state->even = even;
    return output;
}

static inline uint32_t byte_swap32(uint32_t value)
{
    return __builtin_bswap32(value);
}

uint32_t __not_in_flash_func(crypto1_prng_successor)(uint32_t value,
                                                     uint32_t clocks)
{
    value = byte_swap32(value);
    while (clocks-- != 0u)
    {
        const uint32_t feedback =
            (value >> 16u) ^ (value >> 18u) ^ (value >> 19u) ^ (value >> 21u);
        value = (value >> 1u) | ((feedback & 1u) << 31u);
    }
    return byte_swap32(value);
}

#define CRYPTO1_FEED_BIT(byte_val, bit_idx)                                    \
    do                                                                         \
    {                                                                          \
        const uint8_t key_bit = crypto1_filter_odd_fast(odd);                  \
        const uint8_t in_bit = (uint8_t)(((byte_val) >> (bit_idx)) & 1u);      \
        uint32_t poly = (odd & CRYPTO1_POLY_ODD) ^ (even & CRYPTO1_POLY_EVEN); \
        poly ^= poly >> 16u;                                                   \
        poly ^= poly >> 8u;                                                    \
        const uint8_t fb =                                                     \
            (uint8_t)(crypto1_parity_lut[poly & 0xffu] ^ in_bit ^ key_bit);    \
        const uint32_t prev_odd = odd;                                         \
        odd = (even << 1u) | fb;                                               \
        even = prev_odd;                                                       \
    } while (0)

#define CRYPTO1_STREAM_BIT(stream_var, bit_idx)                                \
    do                                                                         \
    {                                                                          \
        const uint8_t key_bit = crypto1_filter_odd_fast(odd);                  \
        (stream_var) |= (uint8_t)(key_bit << (bit_idx));                       \
        uint32_t poly = (odd & CRYPTO1_POLY_ODD) ^ (even & CRYPTO1_POLY_EVEN); \
        poly ^= poly >> 16u;                                                   \
        poly ^= poly >> 8u;                                                    \
        const uint8_t fb = crypto1_parity_lut[poly & 0xffu];                   \
        const uint32_t prev_odd = odd;                                         \
        odd = (even << 1u) | fb;                                               \
        even = prev_odd;                                                       \
    } while (0)

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("O2")))
#endif
bool __not_in_flash_func(crypto1_verify_authentication)(
    crypto1_state_t *state,
    const uint8_t *reader_nonce_enc,
    const uint8_t *reader_answer_enc,
    const uint8_t *expected_reader_answer)
{
    if (!crypto1_filter_tables_ready)
    {
        crypto1_prepare();
    }

    uint32_t odd = state->odd;
    uint32_t even = state->even;

    /* Phase 1: Feed encrypted reader nonce (32 bits) */
    for (uint8_t i = 0u; i < CRYPTO1_NONCE_BYTES; i++)
    {
        const uint8_t b = reader_nonce_enc[i];
#if defined(__arm__) && !defined(UNLEASHED_HOST_TEST)
        crypto1_state_t byte_state = {odd, even};
        crypto1_feed_byte_arm(&byte_state, b);
        odd = byte_state.odd;
        even = byte_state.even;
#else
        for (uint8_t bit = 0u; bit < 8u; bit++)
        {
            CRYPTO1_FEED_BIT(b, bit);
        }
#endif
    }

    /* Phase 2: Verify reader answer (32 bits) */
    for (uint8_t i = 0u; i < CRYPTO1_NONCE_BYTES; i++)
    {
#if defined(__arm__) && !defined(UNLEASHED_HOST_TEST)
        crypto1_state_t byte_state = {odd, even};
        const uint8_t keystream = crypto1_stream_byte_arm(&byte_state);
        odd = byte_state.odd;
        even = byte_state.even;
#else
        uint8_t keystream = 0u;
        for (uint8_t bit = 0u; bit < 8u; bit++)
        {
            CRYPTO1_STREAM_BIT(keystream, bit);
        }
#endif

        if ((uint8_t)(reader_answer_enc[i] ^ keystream) !=
            expected_reader_answer[i])
        {
            state->odd = odd;
            state->even = even;
            return false;
        }
    }

    state->odd = odd;
    state->even = even;
    return true;
}

void __not_in_flash_func(crypto1_encrypt_authentication_response)(
    crypto1_state_t *state,
    const uint8_t *plain,
    const uint8_t *parity,
    uint8_t *output,
    uint8_t *output_parity,
    uint8_t len)
{
    uint32_t odd = state->odd;
    uint32_t even = state->even;
    for (uint8_t i = 0u; i < len; i++)
    {
#if defined(__arm__) && !defined(UNLEASHED_HOST_TEST)
        crypto1_state_t byte_state = {odd, even};
        const uint8_t keystream = crypto1_stream_byte_arm(&byte_state);
        odd = byte_state.odd;
        even = byte_state.even;
#else
        uint8_t keystream = 0u;
        for (uint8_t bit = 0u; bit < 8u; bit++)
        {
            CRYPTO1_STREAM_BIT(keystream, bit);
        }
#endif
        output[i] = (uint8_t)(plain[i] ^ keystream);
        output_parity[i] = (uint8_t)(parity[i] ^ crypto1_filter_odd_fast(odd));
    }
    state->odd = odd;
    state->even = even;
}
