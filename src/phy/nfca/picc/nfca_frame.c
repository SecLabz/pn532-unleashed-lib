#include "phy/nfca/picc/nfca_frame.h"

#include "common/compiler.h"
#include "pn532_unleashed/integrity.h"

#include <string.h>

#ifdef PICO_ON_DEVICE
#include "pico.h"
#define NFCA_TIME_CRITICAL_DATA                                                \
    __attribute__((section(".time_critical.nfca_quarter_decode")))
#else
#define __not_in_flash_func(name) name
#define NFCA_TIME_CRITICAL_DATA
#endif

#define NFCA_RX_ZERO_MASK 0x08u
#define NFCA_RX_ONE_MASK 0x02u
#define NFCA_RX_QUARTER_MASK 0x0fu
#define NFCA_RX_FIRST_QUARTER_MASK 0x08u
#define NFCA_RX_PREVIOUS_ONE_INDEX 16u
#define NFCA_RX_PAIR_PREVIOUS_ONE_INDEX 256u
#define NFCA_RX_PAIR_VALUE_MASK 0x03u
#define NFCA_RX_PAIR_LAST_ONE_MASK 0x02u

/* 0 = invalid, 1 = decoded zero, 2 = decoded one. The upper half is used
 * when the preceding Miller bit was one and an empty pattern means zero. */
static const uint8_t quarter_decode[] NFCA_TIME_CRITICAL_DATA = {
    0, 0, 2, 2, 0, 0, 2, 0, 1, 1, 0, 0, 1, 0, 0, 0,
    1, 1, 2, 2, 1, 1, 2, 0, 1, 1, 0, 0, 1, 0, 0, 0,
};

/* Exact composition of two entries from quarter_decode. Index: previous
 * bit, second quarter mask, first quarter mask. Bit 2 marks validity; bits
 * 0..1 contain the two decoded values. This changes no accepted waveform. */
static const uint8_t quarter_decode_pair[] NFCA_TIME_CRITICAL_DATA = {
    0, 0, 5, 5, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 0, 0, 5, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 0, 0, 7, 0, 6, 6, 0, 0, 6, 0, 0, 0, 0, 0,
    7, 7, 0, 0, 7, 0, 6, 6, 0, 0, 6, 0, 0, 0, 0, 0, 5, 5, 0, 0, 5, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 5, 5, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7,
    0, 0, 7, 0, 6, 6, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 5, 5, 0, 0, 5, 0, 4, 4, 0, 0, 4, 0, 0, 0, 0, 0, 5, 5, 0, 0,
    5, 0, 4, 4, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 0, 0, 5, 0,
    4, 4, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5,
    5, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6, 7, 7, 6, 6, 7, 0, 6, 6, 0, 0,
    6, 0, 0, 0, 6, 6, 7, 7, 6, 6, 7, 0, 6, 6, 0, 0, 6, 0, 0, 0, 0, 0, 5, 5, 0,
    0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 6, 6, 7, 7, 6, 6, 7, 0, 6, 6, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4, 5, 5, 4, 4, 5, 0, 4, 4, 0, 0, 4, 0, 0, 0,
    4, 4, 5, 5, 4, 4, 5, 0, 4, 4, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 4,
    5, 5, 4, 4, 5, 0, 4, 4, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static uint16_t __not_in_flash_func(frame_end_slot)(uint16_t bits)
{
    return (uint16_t)(NFCA_RX_DATA_START_SLOT + bits * NFCA_RX_BIT_SLOTS);
}

void __not_in_flash_func(nfca_quarters_add_level_samples)(
    uint8_t *bit_quarter_masks,
    uint16_t first_slot,
    uint32_t level_samples,
    uint8_t sample_count)
{
    if (bit_quarter_masks == NULL || level_samples == 0u || sample_count == 0u)
    {
        return;
    }

    if (sample_count < NFCA_RX_SAMPLES_PER_WORD)
    {
        level_samples &= (1u << sample_count) - 1u;
        if (level_samples == 0u)
        {
            return;
        }
    }

    while (level_samples != 0u)
    {
        const uint8_t index = fast_ctz32(level_samples);
        level_samples &= level_samples - 1u;

        const uint16_t slot = (uint16_t)(first_slot + index);
        if (slot < NFCA_RX_DATA_START_SLOT)
        {
            continue;
        }

        const uint16_t data_slot = (uint16_t)(slot - NFCA_RX_DATA_START_SLOT);
        const uint16_t bit = data_slot >> 4u;
        if (bit >= NFCA_RX_MAX_BITS)
        {
            break;
        }
        const uint8_t quarter =
            (uint8_t)((data_slot & NFCA_RX_QUARTER_MASK) >> 2u);
        bit_quarter_masks[bit] |=
            (uint8_t)(NFCA_RX_FIRST_QUARTER_MASK >> quarter);
    }
}

static inline __attribute__((always_inline)) bool
decode_symbol(uint8_t pattern, bool first_bit, bool *previous_one, bool *value)
{
    pattern &= NFCA_RX_QUARTER_MASK;
    if (first_bit && (pattern & NFCA_RX_ONE_MASK) != 0u)
    {
        pattern &= (uint8_t)~NFCA_RX_ZERO_MASK;
    }

    const uint8_t decoded =
        quarter_decode[(*previous_one ? NFCA_RX_PREVIOUS_ONE_INDEX : 0u) |
                       pattern];
    if (decoded == 0u)
    {
        return false;
    }

    *value = decoded == 2u;
    *previous_one = *value;
    return true;
}

void __not_in_flash_func(nfca_frame_stream_init)(nfca_frame_stream_t *stream,
                                                 nfca_frame_t *out)
{
    memset(out, 0, sizeof(*out));
    out->parity_ok = true;
    stream->next_end_slot = frame_end_slot(NFCA_RX_BITS_PER_BYTE_WITH_PARITY);
    stream->crc = NFCA_CRC_INITIAL;
    stream->previous_one = false;
    stream->failed = false;
}

void __not_in_flash_func(nfca_frame_stream_consume)(
    nfca_frame_stream_t *stream,
    const uint8_t *bit_quarter_masks,
    uint16_t slot_count,
    nfca_frame_t *out)
{
    while (!stream->failed && out->size_bytes < NFCA_FRAME_MAX_BYTES &&
           slot_count >= stream->next_end_slot)
    {
        const uint8_t *symbols = bit_quarter_masks + out->size_bits;
        bool previous_one = stream->previous_one;
        uint8_t value_byte = 0u;
        for (uint8_t bit = 0u; bit < 8u; bit += 2u)
        {
            uint8_t first = symbols[0] & NFCA_RX_QUARTER_MASK;
            if (out->size_bytes == 0u && bit == 0u &&
                (first & NFCA_RX_ONE_MASK) != 0u)
            {
                first &= (uint8_t)~NFCA_RX_ZERO_MASK;
            }
            const uint16_t index =
                (previous_one ? NFCA_RX_PAIR_PREVIOUS_ONE_INDEX : 0u) |
                ((symbols[1] & NFCA_RX_QUARTER_MASK) << 4u) | first;
            const uint8_t decoded = quarter_decode_pair[index];
            if (decoded == 0u)
            {
                stream->failed = true;
                return;
            }
            value_byte |= (uint8_t)((decoded & NFCA_RX_PAIR_VALUE_MASK) << bit);
            previous_one = (decoded & NFCA_RX_PAIR_LAST_ONE_MASK) != 0u;
            symbols += 2;
        }

        bool parity;
        if (!decode_symbol(*symbols, false, &previous_one, &parity))
        {
            stream->failed = true;
            return;
        }
        out->bytes[out->size_bytes] = value_byte;
        out->parity[out->size_bytes] = parity ? 1u : 0u;
        if (parity != nfca_odd_parity_bit(value_byte))
        {
            out->parity_ok = false;
        }
        /* Delay CRC input by two bytes so the transmitted CRC itself is
         * excluded. This work also overlaps physical reception. */
        if (out->size_bytes >= NFC_CRC_BYTES)
        {
            stream->crc = nfca_crc16_update(
                stream->crc, out->bytes[out->size_bytes - NFC_CRC_BYTES]);
        }
        out->size_bytes++;
        out->size_bits += NFCA_RX_BITS_PER_BYTE_WITH_PARITY;
        stream->previous_one = previous_one;
        stream->next_end_slot +=
            NFCA_RX_BITS_PER_BYTE_WITH_PARITY * NFCA_RX_BIT_SLOTS;
    }
}

static bool __not_in_flash_func(decode_short_frame)(
    const uint8_t *bit_quarter_masks, uint16_t slot_count, nfca_frame_t *out)
{
    memset(out, 0, sizeof(*out));
    out->parity_ok = true;
    if (slot_count < frame_end_slot(NFCA_SHORT_REQUEST_BITS))
    {
        return false;
    }

    bool previous_one = false;
    uint8_t value_byte = 0u;
    for (uint8_t bit = 0u; bit < NFCA_SHORT_REQUEST_BITS; bit++)
    {
        bool value;
        if (!decode_symbol(
                bit_quarter_masks[bit], bit == 0u, &previous_one, &value))
        {
            return false;
        }
        if (value)
        {
            value_byte |= (uint8_t)(1u << bit);
        }
    }

    out->bytes[0] = value_byte;
    out->size_bits = NFCA_SHORT_REQUEST_BITS;
    out->size_bytes = 1u;
    out->short_frame = true;
    return true;
}

bool __not_in_flash_func(nfca_frame_stream_finish)(
    nfca_frame_stream_t *stream,
    const uint8_t *bit_quarter_masks,
    uint16_t slot_count,
    nfca_frame_t *out)
{
    /* EOF may credit fewer slots than the final FIFO word contained. */
    if (frame_end_slot(out->size_bits) > slot_count)
    {
        nfca_frame_stream_init(stream, out);
    }
    nfca_frame_stream_consume(stream, bit_quarter_masks, slot_count, out);
    if (out->size_bytes == 0u)
    {
        return decode_short_frame(bit_quarter_masks, slot_count, out);
    }

    /* Keep the longest structurally valid prefix, exactly as the batch
     * decoder did. Ciphertext parity is reported, not used to reject it. */
    if (out->size_bytes >= 1u + NFC_CRC_BYTES)
    {
        const uint16_t payload_len =
            (uint16_t)(out->size_bytes - NFC_CRC_BYTES);
        const uint16_t crc = (uint16_t)out->bytes[payload_len] |
                             ((uint16_t)out->bytes[payload_len + 1u] << 8u);
        out->crc_ok = stream->crc == crc;
    }
    return true;
}
