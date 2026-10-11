#pragma once

#include "pn532_unleashed/endian.h"

#include <stdint.h>

static inline uint32_t read_be32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24u | (uint32_t)bytes[1] << 16u |
           (uint32_t)bytes[2] << 8u | bytes[3];
}

static inline void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
}
