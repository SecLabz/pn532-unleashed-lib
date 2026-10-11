#include "phy/nfca/pcd/nfca_pcd.h"

#include "board/activity_led.h"
#include "phy/nfca/pcd/nfca_pcd_encode.h"
#include "phy/pcd/pn532_reader_sequence.h"
#include "phy/pcd/reader_rf_trace.h"
#include "phy/pcd/reader_subcarrier_level_rx.h"
#include "phy/rf_capture_scratch.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"

#define NFCA_RX_BLANK_US 15u
#define NFCA_TX_TICK_FC 32u
/* Type 2 allows a 5 ms reply delay; READ_SIG then occupies about 3 ms. */
#define NFCA_SOF_TIMEOUT_US 5000u
#define NFCA_RX_WINDOW_US 10000u

static reader_tx_waveform_t tx_waveform;

_Static_assert(NFCA_PCD_RX_WORDS_PER_HALF *NFCA_PCD_RX_HALVES <=
                   RF_CAPTURE_READER_RX_SAMPLE_WORDS,
               "NFC-A samples must fit the shared capture arena");

bool nfca_pcd_init(void)
{
    return pn532_reader_sequence_init() &&
           reader_subcarrier_level_rx_init(READER_RX_NFCA_SAMPLE_FC);
}

void nfca_pcd_release(void)
{
    reader_subcarrier_level_rx_release();
    pn532_reader_sequence_release();
}

static bool __not_in_flash_func(exchange)(const reader_frame_t *frame,
                                          nfca_pcd_response_t *response,
                                          uint16_t max_bits,
                                          uint32_t deadline_ticks,
                                          uint32_t sof_timeout_us)
{
    *response = (nfca_pcd_response_t){0};
    if (max_bits == 0u || max_bits > NFCA_PCD_RX_MAX_BYTES * 8u ||
        !nfca_pcd_encode(frame, &tx_waveform))
    {
        READER_RF_TRACE_REASON(READER_TRACE_FRAME_INVALID);
        return false;
    }
    const uint32_t saved = save_and_disable_interrupts();
    if (!pn532_reader_sequence_play_at(
            &tx_waveform, NFCA_TX_TICK_FC, deadline_ticks))
    {
        restore_interrupts(saved);
        return false;
    }
    rf_capture_scratch_t *scratch =
        rf_capture_scratch_acquire(RF_CAPTURE_SCRATCH_READER_RX);
    if (scratch == NULL)
    {
        READER_RF_TRACE_REASON(READER_TRACE_RX_SCRATCH_BUSY);
        restore_interrupts(saved);
        board_activity_led_record(true, false);
        return false;
    }
    const uint16_t bits_with_parity = max_bits + (max_bits + 7u) / 8u;
    const uint16_t words =
        NFCA_PCD_RX_WORDS_PER_HALF *
        (2u * bits_with_parity + NFCA_PCD_RX_BOUNDARY_HALVES);
    reader_subcarrier_level_rx_stats_t stats;
    reader_subcarrier_level_rx_capture(scratch->reader_rx_sample_words,
                                       words,
                                       NFCA_RX_BLANK_US,
                                       sof_timeout_us,
                                       NFCA_RX_WINDOW_US,
                                       &stats);
    restore_interrupts(saved);

    if (!stats.overflowed)
    {
        nfca_pcd_decode(scratch->reader_rx_sample_words,
                        stats.word_count * READER_RX_SAMPLES_PER_WORD,
                        max_bits,
                        response);
    }
    else
    {
        READER_RF_TRACE_REASON(READER_TRACE_RX_OVERFLOW);
    }
    rf_capture_scratch_release(RF_CAPTURE_SCRATCH_READER_RX);
    /* Only bounded RAM recording belongs here: recovery callers can still
     * have interrupts masked across a scheduled authentication pair. */
    board_activity_led_record(true,
                              !stats.overflowed && response->framing_valid);
    /* A stalled RX FIFO loses the fixed sample grid. It is a failed physical
     * exchange, never the empty response used to represent tag silence. */
    return !stats.overflowed;
}

bool nfca_pcd_exchange_at(const reader_frame_t *frame,
                          nfca_pcd_response_t *response,
                          uint16_t max_bits,
                          uint32_t deadline_ticks)
{
    return exchange(
        frame, response, max_bits, deadline_ticks, NFCA_SOF_TIMEOUT_US);
}

bool nfca_pcd_exchange_timed(const reader_frame_t *frame,
                             nfca_pcd_response_t *response,
                             uint16_t max_bits,
                             uint32_t sof_timeout_us)
{
    return exchange(frame, response, max_bits, 0u, sof_timeout_us);
}

bool nfca_pcd_exchange(const reader_frame_t *frame,
                       nfca_pcd_response_t *response,
                       uint16_t max_bits)
{
    return nfca_pcd_exchange_at(frame, response, max_bits, 0u);
}

bool nfca_pcd_send(const reader_frame_t *frame)
{
    if (!nfca_pcd_encode(frame, &tx_waveform))
    {
        return false;
    }
    const uint32_t saved = save_and_disable_interrupts();
    const bool sent = pn532_reader_sequence_play(&tx_waveform, NFCA_TX_TICK_FC);
    restore_interrupts(saved);
    /* This dummy-send path is used before a calibrated recovery wait. Do
     * not insert activity work between waveform completion and its caller. */
    return sent;
}
