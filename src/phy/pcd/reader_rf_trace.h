#pragma once

#include "pn532_unleashed/diagnostics.h"

#ifdef PNU_RF_TRACE
#include <stdbool.h>
#include <stdint.h>

/* One foreground bench record, retained across RF cleanup. No frame data or
 * authentication material is accepted. Read only after RF has stopped. */
enum reader_rf_trace_stage
{
    READER_TRACE_NONE,
    READER_TRACE_RESELECT_BEFORE,
    READER_TRACE_WAIT_CLOCK,
    READER_TRACE_EXCHANGE_FIRST,
    READER_TRACE_EXCHANGE_SECOND,
    READER_TRACE_RESELECT_AFTER,
};

enum reader_rf_trace_select
{
    READER_TRACE_SELECT_NONE,
    READER_TRACE_SELECT_WAKEUP,
    READER_TRACE_SELECT_ANTICOLLISION,
    READER_TRACE_SELECT_SELECT,
    READER_TRACE_SELECT_IDENTITY,
};

enum reader_rf_trace_reason
{
    READER_TRACE_REASON_NONE,
    READER_TRACE_NOT_READY,
    READER_TRACE_TX_SCRATCH_BUSY,
    READER_TRACE_FRAME_INVALID,
    READER_TRACE_FIFO_TIMEOUT,
    READER_TRACE_DEADLINE_LATE,
    READER_TRACE_DEADLINE_TOO_FAR,
    READER_TRACE_CLOCK_STALLED,
    READER_TRACE_TX_TIMEOUT,
    READER_TRACE_RX_SCRATCH_BUSY,
    READER_TRACE_RX_OVERFLOW,
    READER_TRACE_RESPONSE_INVALID,
};

/* Begin invalidates the cached profile and enables unscheduled measurement.
 * Seven non-NULL foreground timestamps delimit
 * prepare/select/entropy/auth/target/expire. No RF data is retained. */
void reader_rf_trace_collect_begin(bool enabled);
void reader_rf_trace_collect(const uint32_t marks_us[7]);
bool reader_rf_trace_collect_active(void);
void reader_rf_trace_exchange(const uint32_t marks_us[4],
                              uint32_t capture_us,
                              uint32_t first_edge_us);
void reader_rf_trace_reset(void);
void reader_rf_trace_step(uint16_t index);
void reader_rf_trace_stage(enum reader_rf_trace_stage stage);
void reader_rf_trace_select(enum reader_rf_trace_select phase);
void reader_rf_trace_reason(enum reader_rf_trace_reason reason);
void reader_rf_trace_response(bool sent,
                              uint16_t bit_count,
                              bool framing_valid,
                              bool parity_valid,
                              bool collision);
#define READER_RF_TRACE_RESET(...) reader_rf_trace_reset(__VA_ARGS__)
#define READER_RF_TRACE_STEP(...) reader_rf_trace_step(__VA_ARGS__)
#define READER_RF_TRACE_STAGE(...) reader_rf_trace_stage(__VA_ARGS__)
#define READER_RF_TRACE_SELECT(...) reader_rf_trace_select(__VA_ARGS__)
#define READER_RF_TRACE_REASON(...) reader_rf_trace_reason(__VA_ARGS__)
#define READER_RF_TRACE_RESPONSE(...) reader_rf_trace_response(__VA_ARGS__)
#define READER_RF_TRACE_FINISH(...) nfc_trace_finish(__VA_ARGS__)
#else
#define READER_RF_TRACE_RESET(...) ((void)0)
#define READER_RF_TRACE_STEP(...) ((void)0)
#define READER_RF_TRACE_STAGE(...) ((void)0)
#define READER_RF_TRACE_SELECT(...) ((void)0)
#define READER_RF_TRACE_REASON(...) ((void)0)
#define READER_RF_TRACE_RESPONSE(...) ((void)0)
#define READER_RF_TRACE_FINISH(...) ((void)0)
#endif
