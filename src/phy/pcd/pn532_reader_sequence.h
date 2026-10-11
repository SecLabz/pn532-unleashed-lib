#pragma once

#include "phy/pcd/reader_tx_waveform.h"

/* Initialize the SIGIN DMA/PIO player. The exchange owner masks IRQs during
 * bounded playback, then releases the program before returning to tag mode. */
bool pn532_reader_sequence_init(void);
bool pn532_reader_sequence_play(const reader_tx_waveform_t *waveform,
                                uint16_t tick_fc);
void pn532_reader_sequence_release(void);

/* Prepared DMA is armed at a reader recovery-clock deadline (fc/16 ticks).
 * Zero retains immediate playback. Caller excludes interrupts around TX/RX. */
bool pn532_reader_sequence_play_at(const reader_tx_waveform_t *waveform,
                                   uint16_t tick_fc,
                                   uint32_t deadline_ticks);
