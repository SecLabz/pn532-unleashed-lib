#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define READER_FRAME_MAX_BYTES 32u

typedef struct
{
    uint8_t data[READER_FRAME_MAX_BYTES];
    size_t length;
    uint8_t last_byte_bits; /* Zero means a complete final byte. */
    bool byte_parity;
    bool supplied_parity;
    uint8_t parity[READER_FRAME_MAX_BYTES];
} reader_frame_t;

static inline bool reader_frame_begin(reader_frame_t *frame, size_t length)
{
    if (frame == NULL || length > READER_FRAME_MAX_BYTES)
    {
        return false;
    }
    memset(frame, 0, sizeof(*frame));
    frame->length = length;
    return true;
}
