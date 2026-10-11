#include "phy/nfcv/pcd/nfcv_pcd_decode.h"

#include "phy/nfcv/picc/nfcv_frame.h"
#include "phy/pcd/reader_subcarrier_level_rx.h"
#include "pn532_unleashed/integrity.h"

#include <string.h>

#define NFCV_SYMBOL_TICKS 32u
#define NFCV_FIRST_ACTIVE_SYMBOL 3u
#define NFCV_MIN_ACTIVITY_CYCLES 4u
#define NFCV_MIN_CONTRAST_CYCLES 2u
#define NFCV_PULSE_MAX_SAMPLES 3u
#define NFCV_PERIOD_MIN_SAMPLES 3u
#define NFCV_PERIOD_MAX_SAMPLES 5u
#define NFCV_SOF_BURST_END_SAMPLE 96u
#define NFCV_SOF_QUIET_END_SAMPLE 128u
#define NFCV_SOF_END_SAMPLE 160u
#define NFCV_SOF_BURST_MIN_CYCLES 20u
#define NFCV_SOF_TAIL_MIN_CYCLES 5u
#define NFCV_QUIET_MAX_CYCLES 2u
#define NFCV_BOUNDARY_SYMBOLS 8u
#define NFCV_DATA_BYTE_SYMBOLS 16u

static bool level_sample_at(const uint32_t *words, uint16_t sample)
{
    return ((words[sample / READER_RX_SAMPLES_PER_WORD] >>
             (sample % READER_RX_SAMPLES_PER_WORD)) &
            1u) != 0u;
}

/* SIGOUT can stay high through a quiet interval. A subcarrier pulse must
 * return low within 1..3 samples and repeat every 3..5 samples at 8/fc.
 * Count complete physical cycles, excluding isolated comparator transitions. */
static unsigned
subcarrier_cycles(const uint32_t *words, uint16_t first, uint16_t end)
{
    uint16_t rise = UINT16_MAX;
    uint16_t last_pulse = UINT16_MAX;
    unsigned cycles = 0u;
    bool previous = first != 0u && level_sample_at(words, first - 1u);
    for (uint16_t sample = first; sample < end; sample++)
    {
        const bool current = level_sample_at(words, sample);
        if (current && !previous)
        {
            rise = sample;
        }
        else if (!current && previous && rise != UINT16_MAX)
        {
            const unsigned width = sample - rise;
            if (width <= NFCV_PULSE_MAX_SAMPLES)
            {
                if (last_pulse != UINT16_MAX)
                {
                    const unsigned period = rise - last_pulse;
                    cycles += period >= NFCV_PERIOD_MIN_SAMPLES &&
                              period <= NFCV_PERIOD_MAX_SAMPLES;
                }
                last_pulse = rise;
            }
            rise = UINT16_MAX;
        }
        previous = current;
    }
    return cycles;
}

/* Qualify the first physical SOF burst after a quiet half-bit. Its fixed
 * 96/32/32-sample windows represent 24 cycles, silence, then eight cycles.
 * Bounds allow comparator edge distortion; no phase ranking or CRC search.
 * Half-bit activity requires at least four complete fc/32 cycles. */
static bool find_sof(const uint32_t *words, uint16_t count, uint16_t *start)
{
    bool previous = false;
    for (uint16_t sample = 0u; (uint32_t)sample + NFCV_SOF_END_SAMPLE <= count;
         sample++)
    {
        const bool current = level_sample_at(words, sample);
        if (current && !previous)
        {
            uint16_t fall = sample + 1u;
            while (fall <= sample + NFCV_PULSE_MAX_SAMPLES &&
                   level_sample_at(words, fall))
            {
                fall++;
            }
            const uint16_t quiet_start =
                sample >= NFCV_SYMBOL_TICKS ? sample - NFCV_SYMBOL_TICKS : 0u;
            if (fall <= sample + NFCV_PULSE_MAX_SAMPLES &&
                subcarrier_cycles(words, quiet_start, sample) <=
                    NFCV_QUIET_MAX_CYCLES &&
                subcarrier_cycles(
                    words, sample, sample + NFCV_SOF_BURST_END_SAMPLE) >=
                    NFCV_SOF_BURST_MIN_CYCLES &&
                subcarrier_cycles(words,
                                  sample + NFCV_SOF_BURST_END_SAMPLE,
                                  sample + NFCV_SOF_QUIET_END_SAMPLE) <=
                    NFCV_QUIET_MAX_CYCLES &&
                subcarrier_cycles(words,
                                  sample + NFCV_SOF_QUIET_END_SAMPLE,
                                  sample + NFCV_SOF_END_SAMPLE) >=
                    NFCV_SOF_TAIL_MIN_CYCLES)
            {
                *start = sample;
                return true;
            }
        }
        previous = current;
    }
    return false;
}

static uint8_t activity_pattern(const uint8_t *cycles, uint16_t start)
{
    uint8_t pattern = 0u;
    for (uint8_t index = 0u; index < NFCV_BOUNDARY_SYMBOLS; index++)
    {
        if (cycles[start + index] >= NFCV_MIN_ACTIVITY_CYCLES)
        {
            pattern |= (uint8_t)(0x80u >> index);
        }
    }
    return pattern;
}

static bool eof_seen(const uint8_t *cycles)
{
    /* EOF is an active/quiet pair, three active half-bits, then three quiet
     * half-bits. Require contrast at the burst-to-quiet edge. Later quiet
     * half-bits below the activity floor cannot extend that burst. */
    if (cycles[0] < NFCV_MIN_ACTIVITY_CYCLES ||
        cycles[0] < cycles[1] + NFCV_MIN_CONTRAST_CYCLES)
    {
        return false;
    }
    uint8_t burst_min = cycles[2];
    for (uint8_t index = 3u; index <= 4u; index++)
    {
        if (cycles[index] < burst_min)
        {
            burst_min = cycles[index];
        }
    }
    if (burst_min < NFCV_MIN_ACTIVITY_CYCLES)
    {
        return false;
    }
    if (burst_min < cycles[5] + NFCV_MIN_CONTRAST_CYCLES)
    {
        return false;
    }
    for (uint8_t index = 6u; index < NFCV_BOUNDARY_SYMBOLS; index++)
    {
        if (cycles[index] >= NFCV_MIN_ACTIVITY_CYCLES &&
            burst_min < cycles[index] + NFCV_MIN_CONTRAST_CYCLES)
        {
            return false;
        }
    }
    return true;
}

static bool decode_cycles(const uint8_t *cycles,
                          uint16_t symbol_count,
                          nfcv_pcd_rx_result_t *result)
{
    if (symbol_count < 2u * NFCV_BOUNDARY_SYMBOLS)
    {
        return false;
    }
    result->sof_seen = activity_pattern(cycles, 0u) == NFCV_UPLINK_SOF;
    if (!result->sof_seen)
    {
        return false;
    }

    uint16_t pos = NFCV_BOUNDARY_SYMBOLS;
    result->framing_valid = true;
    while (true)
    {
        if (pos + NFCV_BOUNDARY_SYMBOLS > symbol_count)
        {
            break;
        }
        if (eof_seen(&cycles[pos]))
        {
            result->eof_seen = true;
            break;
        }
        if (result->size_bytes == NFCV_PCD_RX_MAX_BYTES)
        {
            break;
        }
        if (pos + NFCV_DATA_BYTE_SYMBOLS > symbol_count)
        {
            break;
        }
        uint8_t value = 0u;
        for (uint8_t bit = 0u; bit < 8u; bit++)
        {
            const unsigned first = cycles[pos++];
            const unsigned second = cycles[pos++];
            /* The comparator can ring during quiet half-bits. Manchester
             * polarity comes from measured cycle contrast on this one grid,
             * never from the expected byte, UID, or CRC. */
            if (second >= NFCV_MIN_ACTIVITY_CYCLES &&
                second >= first + NFCV_MIN_CONTRAST_CYCLES)
            {
                value |= (uint8_t)(1u << bit);
            }
            else if (first < NFCV_MIN_ACTIVITY_CYCLES ||
                     first < second + NFCV_MIN_CONTRAST_CYCLES)
            {
                result->framing_valid = false;
                break;
            }
        }
        if (!result->framing_valid)
        {
            break;
        }
        result->bytes[result->size_bytes++] = value;
    }
    if (!result->eof_seen)
    {
        result->framing_valid = false;
        result->truncated = result->size_bytes == NFCV_PCD_RX_MAX_BYTES;
    }
    result->crc_ok = result->size_bytes >= 1u + NFC_CRC_BYTES &&
                     nfcv_check_crc(result->bytes, result->size_bytes);
    return result->framing_valid && result->eof_seen && result->crc_ok;
}

bool nfcv_pcd_decode_level_samples(const uint32_t *sample_words,
                                   uint16_t sample_count,
                                   nfcv_pcd_rx_result_t *result)
{
    if (result == NULL)
    {
        return false;
    }
    memset(result, 0, sizeof(*result));
    uint16_t sof_start;
    if (sample_words == NULL ||
        !find_sof(sample_words, sample_count, &sof_start))
    {
        return false;
    }

    uint8_t cycles[NFCV_PCD_MAX_SYMBOLS] = {0};
    uint16_t symbol = NFCV_FIRST_ACTIVE_SYMBOL;
    uint16_t first = sof_start;
    while ((uint32_t)first + NFCV_SYMBOL_TICKS <= sample_count &&
           symbol < NFCV_PCD_MAX_SYMBOLS)
    {
        cycles[symbol] = (uint8_t)subcarrier_cycles(
            sample_words, first, first + NFCV_SYMBOL_TICKS);
        symbol++;
        first += NFCV_SYMBOL_TICKS;
    }
    return decode_cycles(cycles, symbol, result);
}
