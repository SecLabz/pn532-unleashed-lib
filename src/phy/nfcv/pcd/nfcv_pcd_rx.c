#include "phy/nfcv/pcd/nfcv_pcd_rx.h"

#include "phy/nfcv/pcd/nfcv_pcd_decode.h"
#include "phy/pcd/reader_subcarrier_level_rx.h"
#include "phy/rf_capture_scratch.h"

#include <stddef.h>

/* Blank carrier recovery before the response's first subcarrier burst.
 * Capture high-rate responses on the reference reader's fixed 8/fc grid. */
#define NFCV_RX_BLANK_US 180u
#define NFCV_RX_WINDOW_US 8000u
#define NFCV_RX_SAMPLE_WORDS 1024u

_Static_assert(NFCV_RX_SAMPLE_WORDS <= RF_CAPTURE_READER_RX_SAMPLE_WORDS,
               "NFC-V samples must fit the shared capture arena");
static reader_subcarrier_level_rx_stats_t capture_stats;

bool nfcv_pcd_rx_init(void)
{
    return reader_subcarrier_level_rx_init(READER_RX_NFCV_SAMPLE_FC);
}

bool nfcv_pcd_rx_capture_timed(nfcv_pcd_rx_result_t *result,
                               uint32_t sof_timeout_us)
{
    if (result == NULL)
    {
        return false;
    }
    rf_capture_scratch_t *scratch =
        rf_capture_scratch_acquire(RF_CAPTURE_SCRATCH_READER_RX);
    if (scratch == NULL)
    {
        *result = (nfcv_pcd_rx_result_t){.truncated = true};
        return false;
    }

    reader_subcarrier_level_rx_capture(scratch->reader_rx_sample_words,
                                       NFCV_RX_SAMPLE_WORDS,
                                       NFCV_RX_BLANK_US,
                                       sof_timeout_us,
                                       NFCV_RX_WINDOW_US,
                                       &capture_stats);

    (void)nfcv_pcd_decode_level_samples(
        scratch->reader_rx_sample_words,
        capture_stats.overflowed
            ? 0u
            : (uint16_t)(capture_stats.word_count * READER_RX_SAMPLES_PER_WORD),
        result);
    if (capture_stats.truncated || capture_stats.overflowed)
    {
        result->truncated = true;
        rf_capture_scratch_release(RF_CAPTURE_SCRATCH_READER_RX);
        return false;
    }
    rf_capture_scratch_release(RF_CAPTURE_SCRATCH_READER_RX);
    return result->framing_valid && result->eof_seen;
}

bool nfcv_pcd_rx_capture(nfcv_pcd_rx_result_t *result)
{
    return nfcv_pcd_rx_capture_timed(result, NFCV_RX_WINDOW_US) &&
           result->crc_ok;
}
