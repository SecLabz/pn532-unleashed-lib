#include "phy/nfca/sniff/sniff_internal.h"
#include "pn532_unleashed/core.h"
#include "pn532_unleashed/diagnostics.h"
#include "pn532_unleashed/endian.h"

#include <string.h>

#define TRACE_SEGMENTS 4u
#define TRACE_WORDS 1024u
#define TRACE_IDLE_WORDS 52970u /* One second on the 8/fc grid. */
#define TRACE_HEADER_BYTES 80u
#define TRACE_RECORD_BYTES 16u
static struct
{
    uint32_t records[TRACE_SEGMENTS * TRACE_WORDS][4];
    uint64_t words;
    uint64_t starts[TRACE_SEGMENTS];
    uint32_t edges[3];
    uint32_t highs[3];
    uint32_t previous[3];
    uint16_t sizes[TRACE_SEGMENTS];
    uint16_t idle_words;
    uint8_t segments;
    bool recording;
} trace;

void nfca_sniff_trace_reset(void)
{
    memset(&trace, 0, sizeof(trace));
}

void nfca_sniff_trace_word(const uint32_t levels[3], uint32_t state)
{
    for (unsigned i = 0u; i < 3u; i++)
    {
        const uint32_t shifted = (levels[i] << 1u) | trace.previous[i];
        trace.edges[i] += (uint32_t)__builtin_popcount(levels[i] ^ shifted);
        trace.highs[i] += (uint32_t)__builtin_popcount(levels[i]);
        trace.previous[i] = levels[i] >> 31u;
    }
    const bool idle =
        (levels[0] == 0u || levels[0] == UINT32_MAX) && levels[2] == 0u;
    if (idle)
    {
        if (trace.idle_words < TRACE_IDLE_WORDS)
        {
            trace.idle_words++;
        }
    }
    else
    {
        if (!trace.recording && trace.idle_words == TRACE_IDLE_WORDS &&
            trace.segments < TRACE_SEGMENTS)
        {
            trace.starts[trace.segments] = trace.words * 32u * 8u;
            trace.segments++;
            trace.recording = true;
        }
        trace.idle_words = 0u;
    }
    if (trace.recording)
    {
        const unsigned segment = trace.segments - 1u;
        const unsigned word = trace.sizes[segment]++;
        memcpy(trace.records[segment * TRACE_WORDS + word], levels, 12u);
        trace.records[segment * TRACE_WORDS + word][3] = state;
        if (word + 1u == TRACE_WORDS)
        {
            trace.recording = false;
        }
    }
    trace.words++;
}

uint16_t
nfc_trace_sniff(uint16_t offset, uint8_t *output, uint16_t capacity_bytes)
{
    if (output == NULL || capacity_bytes < TRACE_HEADER_BYTES ||
        (nfc_state() != NFC_STATE_IDLE && nfc_state() != NFC_STATE_ERROR) ||
        offset > TRACE_SEGMENTS * TRACE_WORDS)
    {
        return 0u;
    }
    memset(output, 0, TRACE_HEADER_BYTES);
    memcpy(output, "SNR1", 4u);
    write_le32(output + 4u, (uint32_t)trace.words);
    write_le32(output + 8u, (uint32_t)(trace.words >> 32u));
    output[12] = trace.segments;
    for (unsigned i = 0u; i < 3u; i++)
    {
        write_le32(output + 16u + i * 4u, trace.edges[i]);
        write_le32(output + 28u + i * 4u, trace.highs[i]);
    }
    for (unsigned i = 0u; i < TRACE_SEGMENTS; i++)
    {
        write_le32(output + 40u + i * 8u, (uint32_t)trace.starts[i]);
        write_le32(output + 44u + i * 8u, (uint32_t)(trace.starts[i] >> 32u));
        write_le16(output + 72u + i * 2u, trace.sizes[i]);
    }
    uint16_t size = TRACE_HEADER_BYTES;
    for (unsigned i = offset; i < TRACE_SEGMENTS * TRACE_WORDS &&
                              size + TRACE_RECORD_BYTES <= capacity_bytes;
         i++)
    {
        for (unsigned channel = 0u; channel < 4u; channel++)
        {
            write_le32(output + size + channel * 4u, trace.records[i][channel]);
        }
        size += TRACE_RECORD_BYTES;
    }
    return size;
}
