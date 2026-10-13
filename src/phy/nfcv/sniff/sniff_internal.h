#pragma once
#include "phy/sniff/capture.h"
#include "pn532_unleashed/nfcv_sniff.h"
void nfcv_sniff_poll(void);
bool nfcv_sniff_stop(void);
bool nfcv_sniff_requires_stop(void);
void nfcv_sniff_decode_reset(void);
void nfcv_sniff_decode_word(const uint32_t levels[3]);
void nfcv_sniff_decode_stop(void);
void nfcv_sniff_publish(const nfcv_sniff_frame_t *frame);
