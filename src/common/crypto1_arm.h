#pragma once

/* Cortex-M0+ has only eight registers usable by most Thumb instructions.
 * Keep two LFSR halves, input/output, three temporaries and two filter table
 * pointers in those registers throughout each byte. The literal pools and
 * routines live in SRAM with their callers. Keep these routines out of line:
 * inlining the unrolled assembly can put literal pools out of Thumb range. */
#define CRYPTO1_ARM_FEEDBACK(odd_half, even_half)                              \
    "ldr %[a], =%c[poly_odd]\n"                                                \
    "ands %[a], %[" #odd_half "]\n"                                            \
    "ldr %[b], =%c[poly_even]\n"                                               \
    "ands %[b], %[" #even_half "]\n"                                           \
    "eors %[a], %[b]\n"                                                        \
    "lsrs %[b], %[a], #16\n"                                                   \
    "eors %[a], %[b]\n"                                                        \
    "lsrs %[b], %[a], #8\n"                                                    \
    "eors %[a], %[b]\n"                                                        \
    "uxtb %[a], %[a]\n"                                                        \
    "ldr %[b], =crypto1_parity_lut\n"                                          \
    "ldrb %[c], [%[b], %[a]]\n"

#define CRYPTO1_ARM_FILTER(half)                                               \
    "lsls %[a], %[" #half "], #20\n"                                           \
    "lsrs %[a], %[a], #20\n"                                                   \
    "ldrb %[a], [%[low], %[a]]\n"                                              \
    "lsrs %[b], %[" #half "], #12\n"                                           \
    "uxtb %[b], %[b]\n"                                                        \
    "ldrb %[b], [%[high], %[b]]\n"                                             \
    "lsrs %[a], %[b]\n"

static __attribute__((noinline)) void
__not_in_flash_func(crypto1_feed_byte_arm)(crypto1_state_t *state,
                                           uint8_t value)
{
    uint32_t odd = state->odd, even = state->even, input = value;
    uint32_t a, b, c;
    /* Only bit zero of feedback is needed. Shift it into carry and advance
     * alternating halves with ADC, avoiding masks and state swaps. */
    /* Keep one instruction per line despite adjacent macro/string tokens. */
    // clang-format off
    __asm volatile(
        ".syntax unified\n"
        ".rept 4\n"
        CRYPTO1_ARM_FEEDBACK(odd, even)
        CRYPTO1_ARM_FILTER(odd)
        "eors %[a], %[input]\n"
        "eors %[c], %[a]\n"
        "lsrs %[c], %[c], #1\n"
        "adcs %[even], %[even]\n"
        "lsrs %[input], %[input], #1\n"
        CRYPTO1_ARM_FEEDBACK(even, odd)
        CRYPTO1_ARM_FILTER(even)
        "eors %[a], %[input]\n"
        "eors %[c], %[a]\n"
        "lsrs %[c], %[c], #1\n"
        "adcs %[odd], %[odd]\n"
        "lsrs %[input], %[input], #1\n"
        ".endr\n"
        ".syntax divided\n"
        : [odd] "+&l"(odd),
          [even] "+&l"(even),
          [input] "+&l"(input),
          [a] "=&l"(a),
          [b] "=&l"(b),
          [c] "=&l"(c)
        : [low] "l"(crypto1_filter_low), [high] "l"(crypto1_filter_high),
          [poly_odd] "i"(CRYPTO1_POLY_ODD), [poly_even] "i"(CRYPTO1_POLY_EVEN)
        : "cc", "memory");
    // clang-format on
    state->odd = odd;
    state->even = even;
}

static __attribute__((noinline)) uint8_t
__not_in_flash_func(crypto1_stream_byte_arm)(crypto1_state_t *state)
{
    uint32_t odd = state->odd, even = state->even, stream = 0u;
    uint32_t feedback = crypto1_feedback8[0][odd & 0xffu] ^
                        crypto1_feedback8[1][(odd >> 8u) & 0xffu] ^
                        crypto1_feedback8[2][(odd >> 16u) & 0xffu] ^
                        crypto1_feedback8[3][even & 0xffu] ^
                        crypto1_feedback8[4][(even >> 8u) & 0xffu] ^
                        crypto1_feedback8[5][(even >> 16u) & 0xffu];
    uint32_t a, b;
    /* Alternating halves avoid swaps; carry inserts each feedback bit. */
    // clang-format off
    __asm volatile(
        ".syntax unified\n"
        ".rept 4\n"
        CRYPTO1_ARM_FILTER(odd)
        "lsls %[a], %[a], #31\n"
        "lsrs %[stream], %[stream], #1\n"
        "orrs %[stream], %[a]\n"
        "lsrs %[feedback], %[feedback], #1\n"
        "adcs %[even], %[even]\n"
        CRYPTO1_ARM_FILTER(even)
        "lsls %[a], %[a], #31\n"
        "lsrs %[stream], %[stream], #1\n"
        "orrs %[stream], %[a]\n"
        "lsrs %[feedback], %[feedback], #1\n"
        "adcs %[odd], %[odd]\n"
        ".endr\n"
        ".syntax divided\n"
        : [odd] "+&l"(odd),
          [even] "+&l"(even),
          [stream] "+&l"(stream),
          [feedback] "+&l"(feedback),
          [a] "=&l"(a),
          [b] "=&l"(b)
        : [low] "l"(crypto1_filter_low), [high] "l"(crypto1_filter_high)
        : "cc", "memory");
    // clang-format on
    state->odd = odd;
    state->even = even;
    return (uint8_t)(stream >> 24u);
}

#undef CRYPTO1_ARM_FEEDBACK
#undef CRYPTO1_ARM_FILTER
