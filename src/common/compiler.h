#pragma once

#include <stdint.h>

/* Keep placement annotations available to firmware and invisible to host tests.
 */
#if defined(UNLEASHED_HOST_TEST) || !defined(PICO_ON_DEVICE)
#define RAM_FUNC(name) name
#define RAM_DATA(name)
#define FORCE_INLINE inline
#define NOINLINE __attribute__((noinline))
#else
#include "pico.h"
#define RAM_FUNC(name) __not_in_flash_func(name)
#define RAM_DATA(name) __not_in_flash(name)
#define FORCE_INLINE __force_inline
#define NOINLINE __attribute__((noinline))
#endif

static FORCE_INLINE uint8_t fast_clz32(uint32_t val)
{
    uint8_t n = 0u;
    if ((val & 0xffff0000u) == 0u)
    {
        n = (uint8_t)(n + 16u);
        val <<= 16u;
    }
    if ((val & 0xff000000u) == 0u)
    {
        n = (uint8_t)(n + 8u);
        val <<= 8u;
    }
    if ((val & 0xf0000000u) == 0u)
    {
        n = (uint8_t)(n + 4u);
        val <<= 4u;
    }
    if ((val & 0xc0000000u) == 0u)
    {
        n = (uint8_t)(n + 2u);
        val <<= 2u;
    }
    if ((val & 0x80000000u) == 0u)
    {
        n = (uint8_t)(n + 1u);
    }
    return n;
}

static FORCE_INLINE uint8_t fast_ctz32(uint32_t val)
{
    uint8_t n = 0u;
    if ((val & 0x0000ffffu) == 0u)
    {
        n = (uint8_t)(n + 16u);
        val >>= 16u;
    }
    if ((val & 0x000000ffu) == 0u)
    {
        n = (uint8_t)(n + 8u);
        val >>= 8u;
    }
    if ((val & 0x0000000fu) == 0u)
    {
        n = (uint8_t)(n + 4u);
        val >>= 4u;
    }
    if ((val & 0x00000003u) == 0u)
    {
        n = (uint8_t)(n + 2u);
        val >>= 2u;
    }
    if ((val & 0x00000001u) == 0u)
    {
        n = (uint8_t)(n + 1u);
    }
    return n;
}
