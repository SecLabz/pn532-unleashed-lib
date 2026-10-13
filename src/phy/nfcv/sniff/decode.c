#include "phy/nfcv/pcd/nfcv_pcd_decode.h"
#include "phy/nfcv/picc/nfcv_rx_decode.h"
#include "phy/nfcv/sniff/sniff_internal.h"

#include <string.h>

#define PCD_SYMBOL_CAPACITY (2u + NFCV_FRAME_MAX_BYTES * 4u)
#define PICC_WORD_CAPACITY (NFCV_SNIFF_MAX_BYTES * 16u + 32u)
#define PICC_QUIET_WORDS 5u
#define PICC_SEARCH_WORDS 16u
#define PICC_SEARCH_OVERLAP_WORDS 8u
static struct
{
    uint64_t slots;
    uint64_t pcd_start;
    uint64_t picc_start;
    uint32_t picc_words[PICC_WORD_CAPACITY];
    uint16_t picc_count;
    uint16_t pcd_symbols;
    uint16_t pcd_sample_slots;
    uint8_t symbols[PCD_SYMBOL_CAPACITY];
    nfcv_1of256_decoder_t slow;
    uint8_t sample, sample_bits, quiet;
    bool pcd_active, slow_coding, previous_level, picc_sof;
} decoder;

static void publish_request(bool complete)
{
    nfcv_frame_t decoded = {0};
    bool valid = false;
    if (decoder.slow_coding)
    {
        valid = nfcv_1of256_decoder_finish(&decoder.slow, &decoded);
    }
    else
    {
        valid = nfcv_rx_decode_samples(
            decoder.symbols, decoder.pcd_symbols, &decoded);
    }
    nfcv_sniff_frame_t frame = {
        .start_fc = decoder.pcd_start * 8u,
        .duration_fc =
            (uint32_t)((decoder.slots + 1u - decoder.pcd_start) * 8u),
        .size_bytes = decoded.size_bytes,
        .size_bits = decoded.size_bytes * 8u,
        .direction = NFCV_SNIFF_PCD,
        .flags = NFCV_SNIFF_CRC_CHECKED,
    };
    memcpy(frame.data, decoded.bytes, decoded.size_bytes);
    /* The existing request decoders return CRC validity. Physical symbols
     * are checked as they arrive, so a complete frame can have a bad CRC. */
    if (complete)
    {
        frame.flags |= NFCV_SNIFF_DECODED;
    }
    else
    {
        frame.flags |= NFCV_SNIFF_TRUNCATED;
    }
    if (valid)
    {
        frame.flags |= NFCV_SNIFF_CRC_VALID;
    }
    nfcv_sniff_publish(&frame);
    decoder.pcd_active = false;
}

static void publish_response(bool truncated);

static void reader_symbol(uint8_t sample)
{
    if (decoder.pcd_symbols == 0u)
    {
        if (sample != NFCV_1OF4_SOF && sample != NFCV_1OF256_SOF)
        {
            decoder.pcd_active = false;
            return;
        }
        if (decoder.picc_count != 0u)
        {
            publish_response(false);
        }
        decoder.slow_coding = sample == NFCV_1OF256_SOF;
        decoder.symbols[0] = sample;
        decoder.pcd_symbols = 1u;
        nfcv_1of256_decoder_reset(&decoder.slow);
        return;
    }
    if (decoder.slow_coding)
    {
        decoder.pcd_symbols++;
        const nfcv_1of256_step_t step =
            nfcv_1of256_decoder_push(&decoder.slow, sample);
        if (step != NFCV_1OF256_STEP_CONTINUE)
        {
            publish_request(step == NFCV_1OF256_STEP_COMPLETE);
        }
        return;
    }
    if (decoder.pcd_symbols == PCD_SYMBOL_CAPACITY)
    {
        publish_request(false);
        return;
    }
    decoder.symbols[decoder.pcd_symbols++] = sample;
    if (sample == NFCV_1OF4_EOF)
    {
        publish_request((decoder.pcd_symbols - 2u) % 4u == 0u);
        return;
    }
    uint8_t pair;
    if (!nfcv_decode_1of4_sample(sample, &pair))
    {
        publish_request(false);
    }
}

static void reader_sample(bool level)
{
    if (!decoder.pcd_active && decoder.previous_level && !level)
    {
        decoder.pcd_active = true;
        decoder.pcd_start = decoder.slots;
        decoder.pcd_sample_slots = 0u;
        decoder.pcd_symbols = 0u;
        decoder.sample = 0u;
        decoder.sample_bits = 0u;
        decoder.slow_coding = false;
    }
    if (decoder.pcd_active)
    {
        /* Same verified 96/fc phase and 128/fc period as tag-mode PPM RX. */
        if (decoder.pcd_sample_slots == 12u)
        {
            decoder.sample = (uint8_t)((decoder.sample << 1u) | level);
            if (++decoder.sample_bits == 8u)
            {
                reader_symbol(decoder.sample);
                decoder.sample_bits = 0u;
                decoder.sample = 0u;
            }
        }
        decoder.pcd_sample_slots = (decoder.pcd_sample_slots + 1u) & 15u;
    }
    decoder.previous_level = level;
    decoder.slots++;
}

static void publish_response(bool truncated)
{
    nfcv_sniff_frame_t frame = {.direction = NFCV_SNIFF_PICC};
    nfcv_level_result_t physical;
    (void)nfcv_decode_levels(decoder.picc_words,
                             decoder.picc_count * 32u,
                             frame.data,
                             sizeof(frame.data),
                             &physical);
    if (physical.sof_seen)
    {
        frame.start_fc = (decoder.picc_start + physical.start_sample) * 8u;
        const uint32_t end =
            physical.eof_seen ? physical.end_sample : decoder.picc_count * 32u;
        frame.duration_fc = (end - physical.start_sample) * 8u;
        frame.size_bytes = physical.size_bytes;
        frame.size_bits = physical.size_bytes * 8u;
        frame.flags = NFCV_SNIFF_CRC_CHECKED;
        if (physical.framing_valid)
        {
            frame.flags |= NFCV_SNIFF_DECODED;
        }
        if (physical.crc_ok)
        {
            frame.flags |= NFCV_SNIFF_CRC_VALID;
        }
        if (truncated || physical.truncated)
        {
            frame.flags |= NFCV_SNIFF_TRUNCATED;
            frame.flags &= (uint8_t)~NFCV_SNIFF_DECODED;
        }
        nfcv_sniff_publish(&frame);
    }
    decoder.picc_count = 0u;
    decoder.quiet = 0u;
    decoder.picc_sof = false;
}

void nfcv_sniff_decode_word(const uint32_t levels[3])
{
    const uint64_t start = decoder.slots;
    bool reader_seen = decoder.pcd_active && decoder.pcd_symbols != 0u;
    if (!decoder.pcd_active && (levels[0] == UINT32_MAX ||
                                (levels[0] == 0u && !decoder.previous_level)))
    {
        /* A constant idle envelope cannot add PPM edges. Preserve the first
         * falling edge and every active symbol on the scalar capture grid. */
        decoder.previous_level = levels[0] != 0u;
        decoder.slots += 32u;
    }
    else
    {
        for (unsigned bit = 0u; bit < 32u; bit++)
        {
            reader_sample(((levels[0] >> bit) & 1u) != 0u);
            reader_seen = reader_seen ||
                          (decoder.pcd_active && decoder.pcd_symbols != 0u);
        }
    }
    if (reader_seen)
    {
        decoder.picc_count = 0u;
        decoder.quiet = 0u;
        decoder.picc_sof = false;
        return;
    }
    const uint32_t word = levels[1];
    const bool constant = word == 0u || word == UINT32_MAX;
    if (decoder.picc_count == 0u)
    {
        if (constant)
        {
            return;
        }
        decoder.picc_start = start;
    }
    decoder.picc_words[decoder.picc_count++] = word;
    /* High-rate Manchester has at most two quiet half-bits (64 samples).
     * Five constant words cannot terminate data; leave room for full EOF.
     * Raw analog noise without a qualified physical SOF is not a frame. */
    decoder.quiet = constant ? decoder.quiet + 1u : 0u;
    if (decoder.quiet == PICC_QUIET_WORDS)
    {
        publish_response(false);
    }
    else if (!decoder.picc_sof && decoder.picc_count == PICC_SEARCH_WORDS)
    {
        uint32_t sof_start;
        if (nfcv_find_sof(
                decoder.picc_words, PICC_SEARCH_WORDS * 32u, &sof_start))
        {
            decoder.picc_sof = true;
        }
        else
        {
            /* Search bounded overlapping windows before retaining a frame.
             * Eight words preserve the complete 160-sample SOF and preceding
             * quiet half-bit across a DMA boundary. EAS/noise without SOF
             * cannot force a full 323-byte response scan in one poll. */
            memmove(decoder.picc_words,
                    decoder.picc_words + PICC_SEARCH_OVERLAP_WORDS,
                    PICC_SEARCH_OVERLAP_WORDS * sizeof(uint32_t));
            decoder.picc_count = PICC_SEARCH_OVERLAP_WORDS;
            decoder.picc_start += PICC_SEARCH_OVERLAP_WORDS * 32u;
        }
    }
    else if (decoder.picc_count == PICC_WORD_CAPACITY)
    {
        publish_response(true);
    }
}

void nfcv_sniff_decode_reset(void)
{
    memset(&decoder, 0, sizeof(decoder));
    decoder.previous_level = true;
}

void nfcv_sniff_decode_stop(void)
{
    if (decoder.pcd_active && decoder.pcd_symbols != 0u)
    {
        publish_request(false);
    }
    if (decoder.picc_count != 0u)
    {
        publish_response(true);
    }
}
