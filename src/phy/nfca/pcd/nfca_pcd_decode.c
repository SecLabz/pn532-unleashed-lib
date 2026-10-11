#include "phy/nfca/pcd/nfca_pcd.h"
#include "pn532_unleashed/integrity.h"

#include <string.h>

#define ACTIVE_MIN_EDGES 2u

void nfca_pcd_decode(const uint32_t *words,
                     uint16_t sample_count,
                     uint16_t max_bits,
                     nfca_pcd_response_t *response)
{
    *response = (nfca_pcd_response_t){.parity_valid = true};
    if (words == NULL || sample_count < 2u * NFCA_PCD_RX_SAMPLES_PER_HALF ||
        max_bits == 0u || max_bits > NFCA_PCD_RX_MAX_BYTES * 8u)
    {
        return;
    }
    uint8_t edges[NFCA_PCD_RX_HALVES] = {0};
    uint16_t halves = sample_count / NFCA_PCD_RX_SAMPLES_PER_HALF;
    if (halves > NFCA_PCD_RX_HALVES)
    {
        halves = NFCA_PCD_RX_HALVES;
    }
    /* The first rise was observed by PIO before its first sample. */
    uint32_t previous_level = 1u;
    edges[0] = 1u;
    for (uint16_t word = 0u; word < halves * NFCA_PCD_RX_WORDS_PER_HALF; word++)
    {
        const uint32_t levels = words[word];
        const uint32_t previous_levels = (levels << 1u) | previous_level;
        const uint32_t rises = levels & ~previous_levels;
        edges[word / NFCA_PCD_RX_WORDS_PER_HALF] +=
            (uint8_t)__builtin_popcount(rises);
        previous_level = levels >> 31u;
    }
    if (edges[0] < ACTIVE_MIN_EDGES || edges[1] >= ACTIVE_MIN_EDGES)
    {
        return;
    }
    response->sof_seen = true;
    uint8_t bit_in_byte = 0u;
    for (uint16_t half = 2u; half + 1u < halves; half += 2u)
    {
        const bool first = edges[half] >= ACTIVE_MIN_EDGES;
        const bool second = edges[half + 1u] >= ACTIVE_MIN_EDGES;
        if (!first && !second)
        {
            response->framing_valid =
                response->size_bits != 0u && bit_in_byte != 8u;
            return;
        }
        if (first && second)
        {
            response->collision = true;
            return;
        }
        if (bit_in_byte == 8u)
        {
            const uint16_t byte = response->size_bits / 8u - 1u;
            response->parity[byte] = first;
            if (first != nfca_odd_parity_bit(response->data[byte]))
            {
                response->parity_valid = false;
            }
            bit_in_byte = 0u;
            continue;
        }
        if (response->size_bits == max_bits)
        {
            return;
        }
        if (first)
        {
            response->data[response->size_bits / 8u] |=
                (uint8_t)(1u << (response->size_bits % 8u));
        }
        response->size_bits++;
        bit_in_byte++;
    }
}
