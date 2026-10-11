#pragma once

#include "phy/nfca/picc/nfca_frame.h"
#include "pn532_unleashed/diagnostics.h"
#include "protocols/nfca/tag.h"

/* Optional bench build only. Records are appended after frame handling/TX,
 * never streamed during RF. Foreground may read them only with RX stopped.
 * The last 256 events are retained; cumulative counts cover the whole run.
 * No request password or response PACK is retained. */
#ifdef PNU_RF_TRACE
void nfca_trace_reset(void);
void nfca_trace_tx(uint16_t fdt_cycles, bool late, bool wrapped);
void nfca_trace_underrun(bool underrun);
void nfca_trace_response(const nfca_tag_response_t *response);
void nfca_trace_record(const nfca_frame_t *frame,
                       uint32_t timestamp_us,
                       uint16_t sampled_slots,
                       bool eof,
                       bool decoded,
                       bool symbol_error,
                       bool rx_stalled);
#define NFCA_TRACE_RESET(...) nfca_trace_reset(__VA_ARGS__)
#define NFCA_TRACE_TX(...) nfca_trace_tx(__VA_ARGS__)
#define NFCA_TRACE_UNDERRUN(...) nfca_trace_underrun(__VA_ARGS__)
#define NFCA_TRACE_RESPONSE(...) nfca_trace_response(__VA_ARGS__)
#define NFCA_TRACE_RECORD(...) nfca_trace_record(__VA_ARGS__)
#else
#define NFCA_TRACE_RESET(...) ((void)0)
#define NFCA_TRACE_TX(...) ((void)0)
#define NFCA_TRACE_UNDERRUN(...) ((void)0)
#define NFCA_TRACE_RESPONSE(...) ((void)0)
#define NFCA_TRACE_RECORD(...) ((void)0)
#endif
