#include "phy/pcd/reader_rf_trace.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

static struct
{
    uint16_t step_index;
    uint16_t size_bits;
    uint8_t stage;
    uint8_t select_phase;
    uint8_t reason;
    uint8_t status;
    bool sent;
    bool framing_valid;
    bool parity_valid;
    bool collision;
} trace;

void reader_rf_trace_reset(void)
{
    memset(&trace, 0, sizeof(trace));
}

void reader_rf_trace_step(uint16_t index)
{
    trace.step_index = index;
}

void __not_in_flash_func(reader_rf_trace_stage)(
    enum reader_rf_trace_stage stage)
{
    trace.stage = stage;
    trace.select_phase = READER_TRACE_SELECT_NONE;
    trace.reason = READER_TRACE_REASON_NONE;
    trace.sent = false;
    trace.size_bits = 0u;
    trace.framing_valid = false;
    trace.parity_valid = false;
    trace.collision = false;
}

void reader_rf_trace_select(enum reader_rf_trace_select phase)
{
    trace.select_phase = phase;
}

void __not_in_flash_func(reader_rf_trace_reason)(
    enum reader_rf_trace_reason reason)
{
    trace.reason = reason;
}

void __not_in_flash_func(reader_rf_trace_response)(bool sent,
                                                   uint16_t bit_count,
                                                   bool framing_valid,
                                                   bool parity_valid,
                                                   bool collision)
{
    trace.sent = sent;
    trace.size_bits = bit_count;
    trace.framing_valid = framing_valid;
    trace.parity_valid = parity_valid;
    trace.collision = collision;
}

void nfc_trace_finish(uint8_t status)
{
    trace.status = status;
}

uint16_t nfc_trace_reader(char *output, uint16_t capacity)
{
    const int count =
        snprintf(output,
                 capacity,
                 "reader_status=step:%u,stage:%u,select:%u,status:%u,sent:%u,"
                 "bits:%u,framing:%u,parity_valid:%u,collision:%u,reason:%u\n",
                 trace.step_index,
                 trace.stage,
                 trace.select_phase,
                 trace.status,
                 trace.sent,
                 trace.size_bits,
                 trace.framing_valid,
                 trace.parity_valid,
                 trace.collision,
                 trace.reason);
    return count > 0 && count < capacity ? (uint16_t)count : 0u;
}
