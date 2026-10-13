#pragma once
#include "pn532_unleashed/nfca_sniff.h"

void nfca_sniff_poll(void);
bool nfca_sniff_stop(void);
bool nfca_sniff_requires_stop(void);
#include "phy/sniff/capture.h"
void nfca_sniff_decode_reset(void);
void nfca_sniff_decode_word(const uint32_t levels[3]);
void nfca_sniff_decode_stop(void);
void nfca_sniff_publish(const nfca_sniff_frame_t *frame);

#ifdef PNU_RF_TRACE
void nfca_sniff_trace_reset(void);
void nfca_sniff_trace_word(const uint32_t levels[3], uint32_t state);
#endif
